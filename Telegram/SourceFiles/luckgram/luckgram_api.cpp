/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_api.h"

#include "luckgram/luckgram_features.h"
#include "luckgram/luckgram_secure_store.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "core/version.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLocale>
#include <QtCore/QPointer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Luckgram::Api {
namespace {

constexpr auto kProbeIntervalSeconds = 6 * 60 * 60;
constexpr auto kTransferTimeoutMs = 120 * 1000;
constexpr auto kHealthPath = "/v1/health";
constexpr auto kRememberedBaseKey = "api_base";

[[nodiscard]] QStringList BuildBases() {
	// Same lists as mobile LUCKGRAM_API_URLS(_DEV).
	return Features::IsDevBuild()
		? QStringList{
			u"https://api-dev.luckgram.cc"_q,
			u"https://lg-dev.cat158.com"_q,
		}
		: QStringList{
			u"https://api.luckgram.cc"_q,
			u"https://lg.cat158.com"_q,
		};
}

struct Endpoints {
	QStringList bases = BuildBases();
	int current = -1;
	int64 lastProbe = 0;
	bool probing = false;
};

[[nodiscard]] Endpoints &CurrentEndpoints() {
	static auto result = Endpoints();
	if (result.current < 0) {
		const auto remembered = Settings::GetString(
			QString::fromLatin1(kRememberedBaseKey));
		result.current = std::max(int(result.bases.indexOf(remembered)), 0);
	}
	return result;
}

void MarkWorking(const QString &base) {
	auto &endpoints = CurrentEndpoints();
	const auto index = int(endpoints.bases.indexOf(base));
	if (index < 0 || index == endpoints.current) {
		return;
	}
	endpoints.current = index;
	Settings::SetString(QString::fromLatin1(kRememberedBaseKey), base);
	LOG(("Luckgram: API entry is now %1").arg(base));
}

[[nodiscard]] QStringList AttemptOrder() {
	const auto &endpoints = CurrentEndpoints();
	auto result = QStringList{ endpoints.bases[endpoints.current] };
	for (auto i = 0; i != endpoints.bases.size(); ++i) {
		if (i != endpoints.current) {
			result.push_back(endpoints.bases[i]);
		}
	}
	return result;
}

[[nodiscard]] QNetworkAccessManager *Manager() {
	static auto result = QPointer<QNetworkAccessManager>();
	if (!result) {
		result = new QNetworkAccessManager(QCoreApplication::instance());
	}
	return result.data();
}

// No answer from the server at all: try the next entry.
[[nodiscard]] bool IsNetworkFailure(QNetworkReply *reply) {
	if (reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).isValid()) {
		return false;
	}
	switch (reply->error()) {
	case QNetworkReply::NoError:
	case QNetworkReply::OperationCanceledError:
		return false;
	default:
		return true;
	}
}

[[nodiscard]] QString CodeForStatus(int status) {
	if (status == 401) return QString::fromLatin1(kInvalidToken);
	if (status == 413) return u"input_too_large"_q;
	if (status == 429) return u"rate_limited"_q;
	if (status >= 400 && status < 500) return u"invalid_request"_q;
	return QString::fromLatin1(kUpstreamUnavailable);
}

[[nodiscard]] Error ErrorFrom(const Response &response) {
	auto result = Error{ .status = response.status };
	const auto error = response.json().value(u"error"_q).toObject();
	result.code = error.value(u"code"_q).toString();
	if (result.code.isEmpty()) {
		result.code = CodeForStatus(response.status);
	} else if (result.code == u"unauthorized"_q) {
		result.code = QString::fromLatin1(kInvalidToken);
	}
	result.retryAfterSeconds = std::max(
		0,
		error.value(u"retry_after_seconds"_q).toInt());
	return result;
}

[[nodiscard]] QString Locale() {
	// "language[-Script][-REGION]" like mobile (#48).
	return QLocale::system().bcp47Name();
}

struct TokenState {
	QString token;
	bool loaded = false;
	bool registering = false;
	std::vector<std::pair<Fn<void()>, Fn<void(Error)>>> waiting;
};

[[nodiscard]] TokenState &Token() {
	static auto result = TokenState();
	if (!result.loaded) {
		result.loaded = true;
		result.token = SecureStore::Get(SecureStore::kDeviceToken);
	}
	return result;
}

void DropToken() {
	auto &state = Token();
	state.token = QString();
	SecureStore::Remove(SecureStore::kDeviceToken);
}

class RequestImpl final
	: public Request
	, public std::enable_shared_from_this<RequestImpl> {
public:
	RequestImpl(
		RequestArgs &&args,
		Fn<void(Response)> done,
		Fn<void(Error)> fail)
	: _args(std::move(args))
	, _done(std::move(done))
	, _fail(std::move(fail)) {
	}

	void start() {
		if (_args.authorized && Token().token.isEmpty()) {
			// The callbacks keep the request alive until it finishes.
			const auto strong = shared_from_this();
			EnsureRegistered([=] {
				strong->start();
			}, [=](Error error) {
				strong->finishWithError(std::move(error));
			});
			return;
		}
		_order = AttemptOrder();
		_attempt = 0;
		send();
	}

	void cancel() override {
		_cancelled = true;
		_done = nullptr;
		_fail = nullptr;
		_args.onData = nullptr;
		if (const auto reply = _reply.data()) {
			_reply = nullptr;
			reply->abort();
			reply->deleteLater();
		}
	}

private:
	void send() {
		if (_cancelled) {
			return;
		}
		const auto base = _order[_attempt];
		auto request = QNetworkRequest(QUrl(base + _args.path));
		request.setTransferTimeout(kTransferTimeoutMs);
		request.setRawHeader("Accept", _args.accept);
		request.setRawHeader(
			"X-Luckgram-App-Version",
			QByteArray(AppVersionStr));
		if (!_args.contentType.isEmpty()) {
			request.setRawHeader("Content-Type", _args.contentType);
		}
		if (!_args.body.isEmpty()) {
			// CloudFront OAC -> Lambda URL needs the payload hash (mobile).
			request.setRawHeader(
				"x-amz-content-sha256",
				QCryptographicHash::hash(
					_args.body,
					QCryptographicHash::Sha256).toHex());
		}
		if (_args.authorized) {
			request.setRawHeader(
				"Authorization",
				"Bearer " + Token().token.toUtf8());
		}
		for (const auto &[name, value] : _args.headers) {
			request.setRawHeader(name, value);
		}
		const auto reply = (_args.method == "GET")
			? Manager()->get(request)
			: Manager()->sendCustomRequest(request, _args.method, _args.body);
		_reply = reply;
		_received = false;
		// The connections (owned by the reply) keep the request alive until
		// the reply is deleted, so a caller may drop its handle.
		const auto strong = shared_from_this();
		if (_args.onData) {
			QObject::connect(reply, &QNetworkReply::readyRead, [=] {
				if (strong->_reply != reply) {
					return;
				}
				const auto status = reply->attribute(
					QNetworkRequest::HttpStatusCodeAttribute).toInt();
				if (status >= 200 && status < 300 && strong->_args.onData) {
					strong->_received = true;
					strong->_args.onData(reply->readAll());
				}
			});
		}
		QObject::connect(reply, &QNetworkReply::finished, [=] {
			reply->deleteLater();
			if (strong->_reply == reply) {
				strong->_reply = nullptr;
				strong->finished(reply, base);
			}
		});
	}

	void finished(QNetworkReply *reply, const QString &base) {
		if (_cancelled) {
			return;
		}
		if (IsNetworkFailure(reply)) {
			LOG(("Luckgram: %1 %2 failed (%3): %4"
				).arg(QString::fromLatin1(_args.method)
				).arg(base + _args.path
				).arg(int(reply->error())
				).arg(reply->errorString()));
			if (!_received && _attempt + 1 < _order.size()) {
				++_attempt;
				send();
			} else {
				finishWithError({ .code = QString::fromLatin1(kNetworkError) });
			}
			return;
		}
		MarkWorking(base);
		auto response = Response{
			.status = reply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute).toInt(),
			.body = reply->readAll(),
			.contentType = reply->header(
				QNetworkRequest::ContentTypeHeader).toString(),
		};
		if (response.status == 401 && _args.authorized && !_retriedToken) {
			_retriedToken = true;
			DropToken();
			start();
			return;
		}
		if (response.status < 200 || response.status >= 300) {
			finishWithError(ErrorFrom(response));
			return;
		}
		if (const auto done = base::take(_done)) {
			_fail = nullptr;
			done(std::move(response));
		}
	}

	void finishWithError(Error error) {
		if (const auto fail = base::take(_fail)) {
			_done = nullptr;
			fail(std::move(error));
		}
	}

	RequestArgs _args;
	Fn<void(Response)> _done;
	Fn<void(Error)> _fail;
	QStringList _order;
	int _attempt = 0;
	QPointer<QNetworkReply> _reply;
	bool _received = false;
	bool _retriedToken = false;
	bool _cancelled = false;

};

// While on a backup entry, check the primary again every 6 hours.
void MaybeProbePrimary() {
	auto &endpoints = CurrentEndpoints();
	const auto now = QDateTime::currentSecsSinceEpoch();
	if (endpoints.current == 0
		|| endpoints.probing
		|| (endpoints.lastProbe
			&& now >= endpoints.lastProbe
			&& now - endpoints.lastProbe < kProbeIntervalSeconds)) {
		return;
	}
	endpoints.probing = true;
	endpoints.lastProbe = now;
	const auto primary = endpoints.bases.front();
	auto request = QNetworkRequest(QUrl(primary + QString::fromLatin1(kHealthPath)));
	request.setTransferTimeout(10 * 1000);
	const auto reply = Manager()->get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		CurrentEndpoints().probing = false;
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (status >= 200 && status < 300) {
			MarkWorking(primary);
		}
	});
}

} // namespace

QString Error::text() const {
	return ErrorText(code);
}

QString ErrorText(const QString &code) {
	// Same texts as mobile LuckgramStrings.errorMessage.
	if (code == u"quota_exceeded"_q) {
		return Tr("今天的 AI 额度已用完，请明天再试。", "You've used up today's AI quota. Please try again tomorrow.");
	} else if (code == u"content_rejected"_q) {
		return Tr("内容被 AI 服务拒绝。", "The content was rejected by the AI service.");
	} else if (code == u"rate_limited"_q) {
		return Tr("请求太频繁，请稍后再试。", "Too many requests. Please wait a moment and try again.");
	} else if (code == QString::fromLatin1(kNetworkError)) {
		return Tr("网络错误，请检查网络后重试。", "Network error. Please check your connection and try again.");
	} else if (code == u"input_too_large"_q) {
		return Tr("内容太长，无法处理。", "The content is too large to process.");
	} else if (code == QString::fromLatin1(kConsentDenied)) {
		return Tr("你没有同意使用 AI 服务。", "You didn't allow the AI service.");
	} else if (code == QString::fromLatin1(kCancelled)) {
		return Tr("已取消。", "Cancelled.");
	}
	return Tr("AI 服务暂时不可用，请稍后再试。", "The AI service is temporarily unavailable. Please try again later.");
}

QJsonObject Response::json() const {
	return QJsonDocument::fromJson(body).object();
}

RequestPtr Send(
		RequestArgs &&args,
		Fn<void(Response)> done,
		Fn<void(Error)> fail) {
	MaybeProbePrimary();
	const auto result = std::make_shared<RequestImpl>(
		std::move(args),
		std::move(done),
		std::move(fail));
	result->start();
	return result;
}

RequestPtr Download(
		const QString &url,
		Fn<void(QByteArray)> done,
		Fn<void(Error)> fail) {
	class DownloadImpl final : public Request {
	public:
		QPointer<QNetworkReply> reply;
		void cancel() override {
			if (const auto r = reply.data()) {
				reply = nullptr;
				r->abort();
			}
		}
	};
	auto result = std::make_shared<DownloadImpl>();
	auto request = QNetworkRequest(QUrl(url));
	request.setTransferTimeout(kTransferTimeoutMs);
	const auto reply = Manager()->get(request);
	result->reply = reply;
	const auto strong = result;
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (strong->reply != reply) {
			return;
		}
		strong->reply = nullptr;
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() == QNetworkReply::NoError
			&& status >= 200
			&& status < 300) {
			done(reply->readAll());
		} else {
			fail({ .code = QString::fromLatin1(status
				? kUpstreamUnavailable
				: kNetworkError), .status = status });
		}
	});
	return result;
}

void EnsureRegistered(Fn<void()> done, Fn<void(Error)> fail) {
	auto &state = Token();
	if (!state.token.isEmpty()) {
		if (done) {
			done();
		}
		return;
	}
	state.waiting.emplace_back(std::move(done), std::move(fail));
	if (state.registering) {
		return;
	}
	state.registering = true;
	auto body = QJsonObject();
	body.insert(u"platform"_q, Platform());
	body.insert(u"app_version"_q, QString::fromLatin1(AppVersionStr));
	body.insert(u"locale"_q, Locale());
	const auto finish = [](std::optional<Error> error) {
		auto &state = Token();
		state.registering = false;
		auto waiting = base::take(state.waiting);
		for (auto &[onDone, onFail] : waiting) {
			if (!error) {
				if (onDone) {
					onDone();
				}
			} else if (onFail) {
				onFail(*error);
			}
		}
	};
	// Keep the request alive until it finishes.
	static auto pending = RequestPtr();
	pending = Send({
		.method = "POST",
		.path = u"/v1/devices"_q,
		.body = QJsonDocument(body).toJson(QJsonDocument::Compact),
		.contentType = "application/json",
		.authorized = false,
	}, [=](Response response) {
		const auto json = response.json();
		auto token = json.value(u"token"_q).toString();
		if (token.isEmpty()) {
			token = json.value(u"device_token"_q).toString();
		}
		if (token.isEmpty()) {
			finish(Error{
				.code = QString::fromLatin1(kUpstreamUnavailable),
				.status = response.status,
			});
			return;
		}
		Token().token = token;
		SecureStore::Set(SecureStore::kDeviceToken, token);
		LOG(("Luckgram: device registered (%1).").arg(Platform()));
		finish(std::nullopt);
	}, [=](Error error) {
		LOG(("Luckgram: device registration failed, %1").arg(error.code));
		finish(std::move(error));
	});
}

bool HasToken() {
	return !Token().token.isEmpty();
}

void ForgetToken() {
	DropToken();
}

QStringList Bases() {
	return CurrentEndpoints().bases;
}

QString CurrentBase() {
	const auto &endpoints = CurrentEndpoints();
	return endpoints.bases[endpoints.current];
}

bool UsingBackup() {
	return CurrentEndpoints().current != 0;
}

QString Platform() {
#ifdef Q_OS_WIN
	return u"windows"_q;
#elif defined Q_OS_MAC // Q_OS_WIN
	return u"macos"_q;
#else // Q_OS_WIN || Q_OS_MAC
	return u"linux"_q;
#endif // Q_OS_WIN || Q_OS_MAC
}

} // namespace Luckgram::Api
