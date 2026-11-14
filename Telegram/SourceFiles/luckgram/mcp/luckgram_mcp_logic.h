/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

#include <functional>
#include <memory>
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

// Skills: groups of tools switched on per account on the settings page
// (all off at first). The "accounts" tools above every skill are always on.
inline constexpr auto kSkillFriends = "friends";
inline constexpr auto kSkillFriendsManage = "friends_manage";
inline constexpr auto kSkillChats = "chats";

struct SkillInfo {
	const char *key = "";
	const char *titleZh = "";
	const char *titleEn = "";
	const char *aboutZh = "";
	const char *aboutEn = "";
	// Changes something on the account (adds, removes): the settings page
	// asks before switching it on, and its tools are rate limited.
	bool write = false;
};
[[nodiscard]] const std::vector<SkillInfo> &Skills();
// The key of the setting that holds one account's switch for one skill.
[[nodiscard]] QString SkillSettingKey(const QString &skill, quint64 account);

// Write tools are limited per account so an agent in a loop can't get the
// account restricted by Telegram: at most perHour calls in any hour and
// perDay in any 24 hours. Zero: no limit (read tools).
struct RateLimit {
	int perHour = 0;
	int perDay = 0;
};
[[nodiscard]] RateLimit LimitOf(const QString &tool);
// `times` are the earlier calls (ms since epoch, any order). Allowed: 0 is
// returned and `now` belongs in the list. Otherwise how many seconds to
// wait. Entries older than a day are dropped from `times` either way.
[[nodiscard]] qint64 RateWaitSeconds(
	std::vector<qint64> &times,
	qint64 nowMs,
	RateLimit limit);
[[nodiscard]] QString RateSettingKey(const QString &tool, quint64 account);
[[nodiscard]] QString SerializeTimes(const std::vector<qint64> &times);
[[nodiscard]] std::vector<qint64> ParseTimes(const QString &text);

// How a skill tool ended: a value, or why it could not be done.
struct ToolOutcome {
	QJsonObject value;
	QString error;
};
using ToolDone = std::function<void(ToolOutcome)>;

// What the server needs from the app. Called on the main thread.
struct Backend {
	// Passcode lock: nothing is listed or switched while locked.
	std::function<bool()> locked;
	// Logged-in accounts that may be shown right now, in list order.
	std::function<std::vector<AccountInfo>()> accounts;
	// Makes the account active. Empty result on success, else why not.
	std::function<QString(quint64 id)> activate;
	// Is the skill switched on for this account.
	std::function<bool(quint64 account, const QString &skill)> skillEnabled;
	// Runs a skill tool on an account from accounts(). The arguments come
	// checked and with defaults filled in. done is called exactly once,
	// now or later (a tool may wait for the network).
	std::function<void(
		const QString &tool,
		quint64 account,
		const QJsonObject &arguments,
		ToolDone done)> call;
	// The rate limits: when a limited tool was called for an account
	// (kept across restarts), and the clock.
	std::function<std::vector<qint64>(const QString &key)> loadTimes;
	std::function<void(
		const QString &key,
		const std::vector<qint64> &times)> saveTimes;
	std::function<qint64()> nowMs;
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

// The whole answer to one HTTP request; done is called exactly once, now
// or when the last skill tool of the request has answered.
void HandleAsync(
	const Request &request,
	const Config &config,
	const Backend &backend,
	std::function<void(Response)> done);

// One JSON-RPC message. An empty object when nothing is sent back (a
// notification or a response).
void HandleMessageAsync(
	const QJsonObject &message,
	const Backend &backend,
	std::function<void(QJsonObject)> done);

// The same for a backend that answers at once (tests): status 500 / an
// empty object when it did not.
[[nodiscard]] Response Handle(
	const Request &request,
	const Config &config,
	const Backend &backend);
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
