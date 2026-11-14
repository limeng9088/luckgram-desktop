/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/mcp/luckgram_mcp_cloud_client.h"

#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Luckgram::Mcp {
namespace {

// The relay holds a poll for 25 s; anything longer is a dead connection.
constexpr auto kPollTimeoutMs = 45 * 1000;
// Answers that could not be handed in are kept for the next poll, not
// forever: the relay gave up on their calls after 40 s anyway.
constexpr auto kMaxKeptResults = 64;
constexpr auto kMaxReplyBytes = 1024 * 1024;

} // namespace

CloudClient::CloudClient(
	Backend backend,
	std::function<Config()> config,
	std::function<void()> changed)
: _backend(std::move(backend))
, _config(std::move(config))
, _changed(std::move(changed))
, _context(std::make_unique<QObject>())
, _network(std::make_unique<QNetworkAccessManager>()) {
}

CloudClient::~CloudClient() = default;

void CloudClient::start() {
	if (_running) {
		return;
	}
	_running = true;
	_failures = 0;
	poll();
}

void CloudClient::stop() {
	if (!_running) {
		return;
	}
	_running = false;
	++_generation; // Replies still on their way are ignored.
	_results.clear();
	setState(false, QString());
}

bool CloudClient::running() const {
	return _running;
}

bool CloudClient::connected() const {
	return _connected;
}

QString CloudClient::lastError() const {
	return _error;
}

void CloudClient::setState(bool connected, const QString &error) {
	if (_connected == connected && _error == error) {
		return;
	}
	_connected = connected;
	_error = error;
	if (_changed) {
		_changed();
	}
}

void CloudClient::retryLater() {
	if (_retryScheduled) {
		return;
	}
	_retryScheduled = true;
	QTimer::singleShot(
		int(CloudRetryDelayMs(_failures)),
		_context.get(),
		[=, this] {
			_retryScheduled = false;
			if (_running) {
				poll();
			}
		});
}

void CloudClient::poll() {
	if (!_running) {
		return;
	}
	const auto config = _config();
	if (!IsValidCloudKey(config.key) || !IsValidDeviceId(config.deviceId)) {
		setState(false, QStringLiteral("No cloud key."));
		return;
	}
	const auto generation = ++_generation;
	const auto sent = std::exchange(_results, {});
	const auto host = _host;
	auto request = QNetworkRequest(QUrl(
		CloudHostUrl(host) + QString::fromLatin1(kCloudPollPath)));
	request.setRawHeader("Authorization", "Bearer " + config.key.toLatin1());
	request.setRawHeader("Content-Type", "application/json");
	request.setTransferTimeout(kPollTimeoutMs);
	const auto reply = _network->post(request, CloudPollBody(
		config.deviceId,
		config.deviceName,
		config.version,
		sent));
	QObject::connect(reply, &QNetworkReply::finished, _context.get(), [=, this] {
		reply->deleteLater();
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto body = (reply->error() == QNetworkReply::NoError)
			? reply->read(kMaxReplyBytes)
			: QByteArray();
		auto ok = false;
		const auto requests = (status == 200)
			? ParseCloudRequests(body, &ok)
			: std::vector<CloudRequest>();
		const auto latest = (generation == _generation);
		if (!ok) {
			// The answers go again with the next poll, before newer ones.
			if (_running && !sent.empty()) {
				auto kept = sent;
				kept.insert(kept.end(), _results.begin(), _results.end());
				if (int(kept.size()) > kMaxKeptResults) {
					kept.erase(
						kept.begin(),
						kept.end() - kMaxKeptResults);
				}
				_results = std::move(kept);
			}
			if (latest && _running) {
				++_failures;
				// No answer from the relay at all (not a refusal by it):
				// the name may be blocked here, the other one is tried.
				if (!status && host == _host) {
					++_hostFailures;
					const auto next = NextCloudHost(_host, _hostFailures);
					if (next != _host) {
						_host = next;
						_hostFailures = 0;
					}
				}
				setState(false, status
					? QStringLiteral("The cloud answered %1.").arg(status)
					: reply->errorString());
				retryLater();
			}
			return;
		}
		if (!_running) {
			return;
		}
		_failures = 0;
		if (host == _host) {
			_hostFailures = 0;
		}
		setState(true, QString());
		const auto context = QPointer<QObject>(_context.get());
		for (const auto &entry : requests) {
			const auto id = entry.id;
			HandleMessageAsync(entry.message, _backend, [=, this](
					QJsonObject response) {
				// A tool may answer when the client is long gone.
				if (!context || !_running) {
					return;
				}
				_results.push_back({ id, std::move(response) });
				// Handed in right away: the relay ends the poll that is
				// still held open when this one arrives.
				QTimer::singleShot(0, _context.get(), [=, this] {
					if (_running && !_results.empty()) {
						poll();
					}
				});
			});
		}
		// An older poll that the relay ended: the newer one goes on.
		if (latest) {
			poll();
		}
	});
}

} // namespace Luckgram::Mcp
