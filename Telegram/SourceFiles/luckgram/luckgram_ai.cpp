/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_ai.h"

#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/show.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Luckgram::AI {
namespace {

constexpr auto kConsentKey = "ai_consent";

// Server-sent events from /v1/ai/chat: "data: {"delta":"…"}" lines, an
// "error" event or {"error":…}, "data: [DONE]" at the end. OpenAI-style
// chunks ({"choices":[{"delta":{"content":"…"}}]}) are accepted too.
class SseParser final {
public:
	SseParser(
		Fn<void(QString, QString)> onDelta,
		Fn<void(Api::Error)> onError)
	: _onDelta(std::move(onDelta))
	, _onError(std::move(onError)) {
	}

	void feed(const QByteArray &bytes) {
		_buffer += bytes;
		while (true) {
			const auto newline = _buffer.indexOf('\n');
			if (newline < 0) {
				break;
			}
			auto line = _buffer.left(newline);
			_buffer.remove(0, newline + 1);
			if (line.endsWith('\r')) {
				line.chop(1);
			}
			handleLine(line);
		}
	}

	void finish() {
		if (!_buffer.isEmpty()) {
			handleLine(base::take(_buffer));
		}
		dispatch();
	}

	[[nodiscard]] const QString &text() const {
		return _full;
	}
	[[nodiscard]] bool failed() const {
		return _failed;
	}

private:
	void handleLine(const QByteArray &line) {
		if (line.isEmpty()) {
			dispatch();
		} else if (line.startsWith("event:")) {
			_event = line.mid(6).trimmed();
		} else if (line.startsWith("data:")) {
			if (!_data.isEmpty()) {
				_data += '\n';
			}
			_data += line.mid(5).trimmed();
		}
	}

	void dispatch() {
		const auto data = base::take(_data);
		const auto event = base::take(_event);
		if (data.isEmpty() || data == "[DONE]" || _failed) {
			return;
		}
		const auto json = QJsonDocument::fromJson(data).object();
		if (event == "error" || json.contains(u"error"_q)) {
			_failed = true;
			const auto error = json.value(u"error"_q).toObject();
			auto code = error.value(u"code"_q).toString();
			if (code.isEmpty()) {
				code = QString::fromLatin1(Api::kUpstreamUnavailable);
			}
			if (_onError) {
				_onError({ .code = code });
			}
			return;
		}
		auto delta = json.value(u"delta"_q).toString();
		if (delta.isEmpty()) {
			const auto choices = json.value(u"choices"_q).toArray();
			delta = choices.isEmpty()
				? QString()
				: choices.first().toObject().value(
					u"delta"_q).toObject().value(u"content"_q).toString();
		}
		if (!delta.isEmpty()) {
			_full += delta;
			if (_onDelta) {
				_onDelta(delta, _full);
			}
		}
	}

	Fn<void(QString, QString)> _onDelta;
	Fn<void(Api::Error)> _onError;
	QByteArray _buffer;
	QByteArray _data;
	QByteArray _event;
	QString _full;
	bool _failed = false;

};

// Without consent nothing leaves the device: the call fails with
// consent_denied (asynchronously, like a network answer) and sends nothing.
class DeniedRequest final : public Api::Request {
public:
	void cancel() override {
		_cancelled = true;
	}
	[[nodiscard]] bool cancelled() const {
		return _cancelled;
	}

private:
	bool _cancelled = false;

};

[[nodiscard]] Api::RequestPtr Denied(Fn<void(Api::Error)> fail) {
	const auto result = std::make_shared<DeniedRequest>();
	crl::on_main([=] {
		if (!result->cancelled() && fail) {
			fail({ .code = QString::fromLatin1(Api::kConsentDenied) });
		}
	});
	return result;
}

} // namespace

Api::RequestPtr Chat(
		ChatArgs &&args,
		Fn<void(QString, QString)> onDelta,
		Fn<void(QString)> done,
		Fn<void(Api::Error)> fail) {
	if (!HasConsent()) {
		return Denied(fail);
	}
	auto messages = QJsonArray();
	for (const auto &message : args.messages) {
		auto object = QJsonObject();
		object.insert(u"role"_q, message.role);
		object.insert(u"content"_q, message.content);
		messages.push_back(object);
	}
	auto body = QJsonObject();
	body.insert(u"task"_q, args.task);
	body.insert(u"messages"_q, messages);
	body.insert(u"stream"_q, args.stream);

	const auto stream = args.stream;
	const auto parser = std::make_shared<SseParser>(onDelta, fail);
	return Api::Send({
		.method = "POST",
		.path = u"/v1/ai/chat"_q,
		.body = QJsonDocument(body).toJson(QJsonDocument::Compact),
		.contentType = "application/json",
		.accept = stream ? "text/event-stream" : "application/json",
		.onData = stream
			? Fn<void(const QByteArray&)>([=](const QByteArray &bytes) {
				parser->feed(bytes);
			})
			: Fn<void(const QByteArray&)>(),
	}, [=](Api::Response response) {
		if (stream && response.contentType.startsWith(u"text/event-stream"_q)) {
			// Anything not yet handed to onData (whole body if unread).
			if (!response.body.isEmpty()) {
				parser->feed(response.body);
			}
			parser->finish();
			if (!parser->failed() && done) {
				done(parser->text());
			}
			return;
		}
		// Plain JSON answer: {"text": "…"} (also when streaming wasn't used).
		const auto json = response.json();
		auto text = json.value(u"text"_q).toString();
		if (text.isEmpty()) {
			text = json.value(u"content"_q).toString();
		}
		if (onDelta && !text.isEmpty()) {
			onDelta(text, text);
		}
		if (done) {
			done(text);
		}
	}, fail);
}

Api::RequestPtr GenerateImage(
		const QString &prompt,
		Fn<void(Image)> done,
		Fn<void(Api::Error)> fail) {
	if (!HasConsent()) {
		return Denied(fail);
	}
	auto body = QJsonObject();
	body.insert(u"prompt"_q, prompt);
	const auto download = std::make_shared<Api::RequestPtr>();
	return Api::Send({
		.method = "POST",
		.path = u"/v1/ai/image"_q,
		.body = QJsonDocument(body).toJson(QJsonDocument::Compact),
		.contentType = "application/json",
	}, [=](Api::Response response) {
		// docs/api-server.md §3.1: {"url","mime","expires_at"} or "b64".
		const auto json = response.json();
		const auto image = json.contains(u"image"_q)
			? json.value(u"image"_q).toObject()
			: json;
		const auto mime = image.value(u"mime"_q).toString(u"image/png"_q);
		const auto b64 = image.value(u"b64"_q).toString();
		if (!b64.isEmpty()) {
			done({ QByteArray::fromBase64(b64.toLatin1()), mime });
			return;
		}
		const auto url = image.value(u"url"_q).toString();
		if (url.isEmpty()) {
			fail({ .code = QString::fromLatin1(Api::kUpstreamUnavailable) });
			return;
		}
		*download = Api::Download(url, [=](QByteArray data) {
			done({ std::move(data), mime });
		}, fail);
	}, fail);
}

Api::RequestPtr Transcribe(
		QByteArray audio,
		const QString &mime,
		const QString &language,
		Fn<void(QString)> done,
		Fn<void(Api::Error)> fail) {
	if (!HasConsent()) {
		return Denied(fail);
	}
	auto path = u"/v1/ai/transcribe"_q;
	// The server takes ISO-639-1 (2 letters) only, else it auto-detects.
	if (language.size() == 2
		&& language.at(0).isLower()
		&& language.at(1).isLower()) {
		path += u"?language="_q + language;
	}
	return Api::Send({
		.method = "POST",
		.path = path,
		.body = std::move(audio),
		.contentType = mime.toUtf8(),
	}, [=](Api::Response response) {
		done(response.json().value(u"text"_q).toString());
	}, fail);
}

Api::RequestPtr Me(Fn<void(QJsonObject)> done, Fn<void(Api::Error)> fail) {
	return Api::Send({ .path = u"/v1/me"_q }, [=](Api::Response response) {
		done(response.json());
	}, fail);
}

bool HasConsent() {
	return Settings::GetBool(QString::fromLatin1(kConsentKey), false);
}

void SetConsent(bool given) {
	Settings::SetBool(QString::fromLatin1(kConsentKey), given);
}

rpl::producer<bool> ConsentValue() {
	return rpl::single(QString()) | rpl::then(
		Settings::Changes()
	) | rpl::filter([](const QString &key) {
		return key.isEmpty() || key == QString::fromLatin1(kConsentKey);
	}) | rpl::map([] {
		return HasConsent();
	}) | rpl::distinct_until_changed();
}

void WithConsent(
		std::shared_ptr<Ui::Show> show,
		Fn<void()> granted,
		Fn<void()> denied) {
	if (HasConsent()) {
		if (granted) {
			granted();
		}
		return;
	}
	// Exactly one of the two runs.
	const auto answered = std::make_shared<bool>(false);
	show->showBox(Ui::MakeConfirmBox({
		.text = Tr(
			"你选中的内容会发送到 Luckgram 的 AI 服务，由 OpenAI 处理后返回。",
			"The content you select will be sent to Luckgram's AI service and processed by OpenAI."),
		.confirmed = [=](Fn<void()> close) {
			if (*answered) {
				return;
			}
			*answered = true;
			SetConsent(true);
			close();
			if (granted) {
				granted();
			}
		},
		.cancelled = [=](Fn<void()> close) {
			close();
			if (*answered) {
				return;
			}
			*answered = true;
			if (denied) {
				denied();
			}
		},
		.confirmText = Tr("同意", "Agree"),
		.cancelText = Tr("不同意", "Decline"),
		.title = Tr("使用 AI 服务", "Use AI service"),
	}));
}

void ShowRevokeConsent(std::shared_ptr<Ui::Show> show) {
	show->showBox(Ui::MakeConfirmBox({
		.text = Tr(
			"撤回后，下次使用 AI 功能时会重新询问。",
			"You'll be asked again the next time you use an AI feature."),
		.confirmed = [=](Fn<void()> close) {
			SetConsent(false);
			close();
		},
		.confirmText = Tr("撤回", "Revoke"),
		.cancelText = Tr("取消", "Cancel"),
		.title = Tr("撤回 AI 授权？", "Revoke AI consent?"),
	}));
}

} // namespace Luckgram::AI
