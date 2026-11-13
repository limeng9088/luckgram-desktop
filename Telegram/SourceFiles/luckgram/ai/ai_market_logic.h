/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

#include <optional>
#include <vector>

// D7 (mobile 30, server S13): the agent market without UI. Parses the
// responses, builds request paths, keeps the local cache and the "used"
// list ("我的"). The server never sends an agent's system instruction and
// nothing here stores one. QtCore only (luckgram_tests); same rules as
// Android AgentMarketLogic.
namespace Luckgram::AI::Market {

// Page size of GET /v1/agents (server allows 1..50).
inline constexpr auto kPageSize = 20;
// Most agents kept in the "used" list.
inline constexpr auto kMaxUsed = 50;
// Most agent details kept in the cache.
inline constexpr auto kMaxCachedDetails = 100;
// Key prefix of the assistant conversation of a market agent (18 需求 8).
inline constexpr auto kConversationKeyPrefix = "market:";
// Field of POST /v1/ai/chat (task=agent) naming the market agent (S13).
inline constexpr auto kParamAgentId = "agent_id";
// Server error codes (S13 / S12).
inline constexpr auto kErrorNotPro = "not_pro";
inline constexpr auto kErrorAgentUnavailable = "agent_unavailable";
inline constexpr auto kCategoriesPath = "/v1/agents/categories";

// Avatar color when the server gives none (or an invalid one), ARGB.
inline constexpr quint32 kDefaultColor = 0xFF5B8DEFU;

// A string in both app languages.
struct Text {
	QString zh;
	QString en;

	[[nodiscard]] QString pick(bool chinese) const;
	[[nodiscard]] bool contains(const QString &lowerQuery) const;
	[[nodiscard]] bool empty() const;
	[[nodiscard]] QJsonObject toJson() const;
	[[nodiscard]] static Text Parse(const QJsonValue &value);
};

struct Agent {
	QString id;
	Text name;
	Text description;
	QString category;
	quint32 color = kDefaultColor;
	QString emoji;
	QString imageUrl; // https only.
	std::vector<Text> examples;
	QString author = QStringLiteral("luckgram");
	bool proOnly = false;
	QString modelTier = QStringLiteral("standard");
	qint64 users = 0;

	[[nodiscard]] bool isPremium() const;
	// What the round avatar shows: the emoji, else the name's first
	// character (upper case).
	[[nodiscard]] QString avatarText(bool chinese) const;
	[[nodiscard]] bool matches(const QString &query) const;
	[[nodiscard]] QJsonObject toJson() const;
};

struct Category {
	QString id;
	Text name;
	int count = 0;
};

struct Page {
	std::vector<Agent> items;
	QString nextCursor; // Empty on the last page.
};

// One agent of /v1/agents or /v1/agents/{id}; nothing without an id.
[[nodiscard]] std::optional<Agent> ParseAgent(const QJsonObject &object);
// {"items":[…],"next_cursor":…|null}
[[nodiscard]] Page ParsePage(const QJsonObject &object);
// {"categories":[{id,name{zh,en},count}]}, in the server's order.
[[nodiscard]] std::vector<Category> ParseCategories(const QJsonObject &object);

// "#RRGGBB" -> ARGB, or def.
[[nodiscard]] quint32 ParseColor(const QString &value, quint32 def);
[[nodiscard]] QString ColorString(quint32 color);

// /v1/agents?limit=&category=&q=&cursor= (empty values left out, q cut to
// 50 characters).
[[nodiscard]] QString ListPath(
	const QString &category,
	const QString &query,
	const QString &cursor,
	int limit);
[[nodiscard]] QString DetailPath(const QString &id);

// Extra fields of POST /v1/ai/chat for a market agent (task=agent).
[[nodiscard]] QJsonObject ChatParams(const QString &agentId);
[[nodiscard]] QString ConversationKey(const QString &agentId);

// 30 需求 5: a free user can see a Pro agent but has to upgrade to chat.
[[nodiscard]] bool NeedsUpgrade(const Agent &agent, bool isPro);

// "1.2万人使用" / "1.2K users".
[[nodiscard]] QString FormatUsers(qint64 users, bool chinese);

// 30 需求 7: categories, the first page of each list ("" = all) and details,
// as one JSON object. Search results are not cached; offline, search
// filters the cached agents.
class Cache final {
public:
	explicit Cache(const QString &stored = QString());

	[[nodiscard]] QString serialize() const;

	void putCategories(const QJsonObject &response);
	[[nodiscard]] std::vector<Category> categories() const;

	void putFirstPage(const QString &category, const QJsonObject &response);
	[[nodiscard]] std::optional<Page> firstPage(const QString &category) const;

	// Every cached agent (lists and details), each once.
	[[nodiscard]] std::vector<Agent> allAgents() const;
	// Cached agents of category ("" = all) matching query.
	[[nodiscard]] std::vector<Agent> offline(
		const QString &category,
		const QString &query) const;

	void putDetail(const Agent &agent);
	[[nodiscard]] std::optional<Agent> detail(const QString &id) const;

private:
	QJsonObject _json;

};

// 30 需求 6: the "used" list, most recently used first.
[[nodiscard]] std::vector<Agent> ParseUsed(const QString &stored);
[[nodiscard]] QString UsedToString(const std::vector<Agent> &used);
// Puts agent first (replacing an older snapshot), at most kMaxUsed.
[[nodiscard]] std::vector<Agent> MarkUsed(
	const std::vector<Agent> &used,
	const Agent &agent);
// Replaces the snapshot of an agent already in the list, keeping its place.
[[nodiscard]] std::vector<Agent> RefreshUsed(
	const std::vector<Agent> &used,
	const Agent &agent);
[[nodiscard]] std::vector<Agent> RemoveUsed(
	const std::vector<Agent> &used,
	const QString &id);

} // namespace Luckgram::AI::Market
