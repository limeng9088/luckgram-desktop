/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QJsonObject>

// D2 §5-6 (mobile 00 / S2): the Luckgram API client.
// - Dev builds talk to api-dev.luckgram.cc, release builds to api.luckgram.cc;
//   when the entry can't be reached the cat158 backup is used (same rule as
//   mobile, bugs #81/#82) and the primary is probed again every 6 hours.
// - Requests go through the app proxy (Qt application proxy, set by the
//   Sandbox from Settings → Advanced → Connection type).
// - The device registers once (POST /v1/devices, platform windows|macos);
//   the token lives in the secure store, requests carry
//   "Authorization: Bearer <token>", a 401 registers again once.
namespace Luckgram::Api {

struct Error {
	QString code; // quota_exceeded, content_rejected, network_error...
	int status = 0;
	int retryAfterSeconds = 0;

	// The message for the user (Chinese / English).
	[[nodiscard]] QString text() const;
};

// Codes produced by the client itself (others come from the server).
inline constexpr auto kNetworkError = "network_error";
inline constexpr auto kCancelled = "cancelled";
inline constexpr auto kConsentDenied = "consent_denied";
inline constexpr auto kUpstreamUnavailable = "upstream_unavailable";
inline constexpr auto kInvalidToken = "invalid_token";

[[nodiscard]] QString ErrorText(const QString &code);

struct Response {
	int status = 0;
	QByteArray body;
	QString contentType;

	[[nodiscard]] QJsonObject json() const;
};

struct RequestArgs {
	QByteArray method = "GET";
	QString path; // "/v1/config"
	QByteArray body;
	QByteArray contentType; // Of the body.
	QByteArray accept = "application/json";
	bool authorized = true;
	std::vector<std::pair<QByteArray, QByteArray>> headers;
	// Streaming (SSE): raw bytes of a successful response as they arrive.
	// Once something arrived the request is not retried elsewhere.
	Fn<void(const QByteArray&)> onData;
};

class Request {
public:
	virtual ~Request() = default;
	virtual void cancel() = 0;
};
using RequestPtr = std::shared_ptr<Request>;

// Callbacks run on the main thread; exactly one of them is called (none
// after cancel()).
RequestPtr Send(
	RequestArgs &&args,
	Fn<void(Response)> done,
	Fn<void(Error)> fail);

// GET of an absolute URL the API handed out (generated image), same proxy.
RequestPtr Download(
	const QString &url,
	Fn<void(QByteArray)> done,
	Fn<void(Error)> fail);

// PUT of a body to an absolute presigned URL the API handed out (Square
// media, D10), same proxy, no Authorization. `progress(sent, total)`.
RequestPtr Upload(
	const QString &url,
	const QByteArray &contentType,
	QByteArray body,
	Fn<void(qint64, qint64)> progress,
	Fn<void()> done,
	Fn<void(Error)> fail);

// Registers now if there is no token yet (startup, debug page).
void EnsureRegistered(Fn<void()> done = nullptr, Fn<void(Error)> fail = nullptr);
[[nodiscard]] bool HasToken();
// Logged out of every account: the token is removed (next use registers).
void ForgetToken();

// Entry list in order (primary first) and the one in use.
[[nodiscard]] QStringList Bases();
[[nodiscard]] QString CurrentBase();
[[nodiscard]] bool UsingBackup();

[[nodiscard]] QString Platform(); // windows, macos, linux

} // namespace Luckgram::Api
