/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

#include <functional>
#include <utility>
#include <vector>

// Local MCP server (Model Context Protocol, the "Streamable HTTP" transport
// without the optional SSE stream): an agent on this computer lists the
// logged-in accounts and switches the active one.
//
// Pure logic, QtCore + std only (also in luckgram_tests): the HTTP request
// parser, the checks made before a request is looked at (token, Host,
// Origin) and the JSON-RPC methods. Sockets and the accounts themselves are
// in luckgram_mcp.cpp.
namespace Luckgram::Mcp {

inline constexpr auto kFeatureKey = "local_mcp";
inline constexpr auto kDefaultPort = 28765;
inline constexpr auto kPortAttempts = 20; // kDefaultPort .. +19.
inline constexpr auto kPath = "/mcp";
inline constexpr auto kMaxHeaderBytes = 16 * 1024;
inline constexpr auto kMaxBodyBytes = 256 * 1024;

struct AccountInfo {
	quint64 id = 0; // Telegram user id.
	int index = 0; // 1-based position in the account list.
	QString name;
	QString username;
	QString phone;
	int unread = 0;
	bool unreadMuted = false;
	bool premium = false;
	bool active = false;
};

// What the server needs from the app. Called on the main thread.
struct Backend {
	// Passcode lock: nothing is listed or switched while locked.
	std::function<bool()> locked;
	// Logged-in accounts that may be shown right now, in list order.
	std::function<std::vector<AccountInfo>()> accounts;
	// Makes the account active. Empty result on success, else why not.
	std::function<QString(quint64 id)> activate;
};

struct Request {
	QByteArray method; // Upper case.
	QByteArray path; // Without the query.
	std::vector<std::pair<QByteArray, QByteArray>> headers; // Lower-case names.
	QByteArray body;

	[[nodiscard]] QByteArray header(const QByteArray &lowerName) const;
};

enum class ParseResult {
	Incomplete, // Wait for more bytes.
	Ready,
	Bad, // Answer with *errorStatus and close.
};

// Parses one request from the start of the buffer.
[[nodiscard]] ParseResult ParseRequest(
	const QByteArray &buffer,
	Request *request,
	int *errorStatus);

struct Response {
	int status = 200;
	QByteArray contentType; // Empty: no body.
	QByteArray body;
	std::vector<std::pair<QByteArray, QByteArray>> headers;
};

[[nodiscard]] QByteArray Serialize(const Response &response);

struct Config {
	QString token;
	int port = 0;
};

// The whole answer to one HTTP request.
[[nodiscard]] Response Handle(
	const Request &request,
	const Config &config,
	const Backend &backend);

// One JSON-RPC message. An empty object when nothing is sent back (a
// notification or a response).
[[nodiscard]] QJsonObject HandleMessage(
	const QJsonObject &message,
	const Backend &backend);

// Which account "switch_account" means: exactly one of id / username /
// phone / index. Returns the user id, or 0 with *error set.
[[nodiscard]] quint64 ResolveAccount(
	const std::vector<AccountInfo> &accounts,
	const QJsonObject &arguments,
	QString *error);

// 32 random bytes as 43 URL-safe characters come from the caller; this only
// says whether a stored token is usable.
[[nodiscard]] bool IsValidToken(const QString &token);

// Same length and same bytes, in time that doesn't depend on where they
// differ.
[[nodiscard]] bool SameToken(const QByteArray &a, const QByteArray &b);

// The client settings shown on the settings page (Claude Code / Cursor style
// "mcpServers" entry).
[[nodiscard]] QString ClientConfigJson(const Config &config);
[[nodiscard]] QString ServerUrl(int port);

} // namespace Luckgram::Mcp
