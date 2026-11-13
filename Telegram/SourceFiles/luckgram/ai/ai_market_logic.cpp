/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_market_logic.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QUrl>

#include <algorithm>
#include <cmath>

namespace Luckgram::AI::Market {
namespace {

// Like org.json optString: strings as they are, numbers and booleans as
// text, anything else (null, missing, objects) the default.
[[nodiscard]] QString OptString(
		const QJsonValue &value,
		const QString &def = QString()) {
	if (value.isString()) {
		return value.toString();
	} else if (value.isDouble()) {
		const auto d = value.toDouble();
		return (d == std::floor(d) && std::abs(d) < 1e15)
			? QString::number(qint64(d))
			: QString::number(d);
	} else if (value.isBool()) {
		return value.toBool()
			? QStringLiteral("true")
			: QStringLiteral("false");
	}
	return def;
}

[[nodiscard]] QString Encode(const QString &value) {
	return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

// At most count code points of s.
[[nodiscard]] QString LeftCodePoints(const QString &s, int count) {
	auto i = 0;
	auto taken = 0;
	while (i < s.size() && taken < count) {
		i += (s[i].isHighSurrogate()
			&& i + 1 < s.size()
			&& s[i + 1].isLowSurrogate()) ? 2 : 1;
		++taken;
	}
	return s.left(i);
}

[[nodiscard]] int CodePointCount(const QString &s) {
	auto count = 0;
	for (auto i = 0; i < s.size(); ++i) {
		if (!(s[i].isLowSurrogate() && i > 0 && s[i - 1].isHighSurrogate())) {
			++count;
		}
	}
	return count;
}

[[nodiscard]] QString TrimNumber(double v) {
	auto s = QString::number(std::floor(v * 10) / 10, 'f', 1);
	if (s.endsWith(QLatin1String(".0"))) {
		s.chop(2);
	}
	return s;
}

[[nodiscard]] QJsonArray ToArray(const std::vector<Agent> &agents) {
	auto result = QJsonArray();
	for (const auto &agent : agents) {
		result.push_back(agent.toJson());
	}
	return result;
}

} // namespace

QString Text::pick(bool chinese) const {
	const auto &first = chinese ? zh : en;
	return first.isEmpty() ? (chinese ? en : zh) : first;
}

bool Text::contains(const QString &lowerQuery) const {
	return zh.toLower().contains(lowerQuery)
		|| en.toLower().contains(lowerQuery);
}

bool Text::empty() const {
	return zh.isEmpty() && en.isEmpty();
}

QJsonObject Text::toJson() const {
	auto result = QJsonObject();
	result.insert(QStringLiteral("zh"), zh);
	result.insert(QStringLiteral("en"), en);
	return result;
}

Text Text::Parse(const QJsonValue &value) {
	if (value.isObject()) {
		const auto object = value.toObject();
		return {
			OptString(object.value(QStringLiteral("zh"))),
			OptString(object.value(QStringLiteral("en"))),
		};
	} else if (value.isString()) {
		return { value.toString(), value.toString() };
	}
	return {};
}

bool Agent::isPremium() const {
	return modelTier == QLatin1String("premium");
}

QString Agent::avatarText(bool chinese) const {
	if (!emoji.isEmpty()) {
		return emoji;
	}
	const auto n = name.pick(chinese).trimmed();
	return n.isEmpty()
		? QStringLiteral("?")
		: LeftCodePoints(n, 1).toUpper();
}

bool Agent::matches(const QString &query) const {
	const auto q = query.trimmed().toLower();
	return q.isEmpty() || name.contains(q) || description.contains(q);
}

QJsonObject Agent::toJson() const {
	auto avatar = QJsonObject();
	avatar.insert(QStringLiteral("color"), ColorString(color));
	if (!emoji.isEmpty()) {
		avatar.insert(QStringLiteral("emoji"), emoji);
	}
	if (!imageUrl.isEmpty()) {
		avatar.insert(QStringLiteral("image_url"), imageUrl);
	}
	auto list = QJsonArray();
	for (const auto &example : examples) {
		list.push_back(example.toJson());
	}
	auto result = QJsonObject();
	result.insert(QStringLiteral("id"), id);
	result.insert(QStringLiteral("name"), name.toJson());
	result.insert(QStringLiteral("description"), description.toJson());
	result.insert(QStringLiteral("category"), category);
	result.insert(QStringLiteral("avatar"), avatar);
	result.insert(QStringLiteral("examples"), list);
	result.insert(QStringLiteral("author"), author);
	result.insert(QStringLiteral("pro_only"), proOnly);
	result.insert(QStringLiteral("model_tier"), modelTier);
	result.insert(QStringLiteral("users"), double(users));
	return result;
}

std::optional<Agent> ParseAgent(const QJsonObject &o) {
	const auto id = OptString(o.value(QStringLiteral("id")));
	if (id.isEmpty()) {
		return std::nullopt;
	}
	auto a = Agent();
	a.id = id;
	a.name = Text::Parse(o.value(QStringLiteral("name")));
	a.description = Text::Parse(o.value(QStringLiteral("description")));
	a.category = OptString(o.value(QStringLiteral("category")));
	const auto avatar = o.value(QStringLiteral("avatar"));
	if (avatar.isObject()) {
		const auto object = avatar.toObject();
		a.color = ParseColor(
			OptString(object.value(QStringLiteral("color"))),
			kDefaultColor);
		a.emoji = OptString(object.value(QStringLiteral("emoji")));
		const auto url = OptString(object.value(QStringLiteral("image_url")));
		a.imageUrl = url.startsWith(QLatin1String("https://"))
			? url
			: QString();
	}
	for (const auto &value : o.value(QStringLiteral("examples")).toArray()) {
		auto text = Text::Parse(value);
		if (!text.empty()) {
			a.examples.push_back(std::move(text));
		}
	}
	a.author = OptString(
		o.value(QStringLiteral("author")),
		QStringLiteral("luckgram"));
	a.proOnly = o.value(QStringLiteral("pro_only")).toBool(false);
	a.modelTier = OptString(
		o.value(QStringLiteral("model_tier")),
		QStringLiteral("standard"));
	a.users = std::max(
		qint64(0),
		qint64(o.value(QStringLiteral("users")).toDouble(0.)));
	// Never keep an instruction, even if a server ever sent one.
	return a;
}

Page ParsePage(const QJsonObject &object) {
	auto result = Page();
	for (const auto &value : object.value(QStringLiteral("items")).toArray()) {
		if (auto agent = ParseAgent(value.toObject())) {
			result.items.push_back(std::move(*agent));
		}
	}
	result.nextCursor = OptString(object.value(QStringLiteral("next_cursor")));
	return result;
}

std::vector<Category> ParseCategories(const QJsonObject &object) {
	auto result = std::vector<Category>();
	const auto list = object.value(QStringLiteral("categories")).toArray();
	for (const auto &value : list) {
		if (!value.isObject()) {
			continue;
		}
		const auto c = value.toObject();
		const auto id = OptString(c.value(QStringLiteral("id")));
		if (id.isEmpty()) {
			continue;
		}
		result.push_back({
			id,
			Text::Parse(c.value(QStringLiteral("name"))),
			c.value(QStringLiteral("count")).toInt(0),
		});
	}
	return result;
}

quint32 ParseColor(const QString &value, quint32 def) {
	static const auto kColor = QRegularExpression(
		QStringLiteral("^#[0-9a-fA-F]{6}$"));
	if (!kColor.match(value).hasMatch()) {
		return def;
	}
	return 0xFF000000U | value.mid(1).toUInt(nullptr, 16);
}

QString ColorString(quint32 color) {
	return QChar('#')
		+ QString::number(color & 0xFFFFFFU, 16).toUpper().rightJustified(
			6,
			QChar('0'));
}

QString ListPath(
		const QString &category,
		const QString &query,
		const QString &cursor,
		int limit) {
	auto result = QStringLiteral("/v1/agents?limit=") + QString::number(limit);
	if (!category.isEmpty()) {
		result += QStringLiteral("&category=") + Encode(category);
	}
	auto q = query.trimmed();
	if (!q.isEmpty()) {
		// The server accepts at most 50 characters.
		if (CodePointCount(q) > 50) {
			q = LeftCodePoints(q, 50);
		}
		result += QStringLiteral("&q=") + Encode(q);
	}
	if (!cursor.isEmpty()) {
		result += QStringLiteral("&cursor=") + Encode(cursor);
	}
	return result;
}

QString DetailPath(const QString &id) {
	return QStringLiteral("/v1/agents/") + Encode(id);
}

QJsonObject ChatParams(const QString &agentId) {
	auto result = QJsonObject();
	result.insert(QString::fromLatin1(kParamAgentId), agentId);
	return result;
}

QString ConversationKey(const QString &agentId) {
	return QString::fromLatin1(kConversationKeyPrefix) + agentId;
}

bool NeedsUpgrade(const Agent &agent, bool isPro) {
	return agent.proOnly && !isPro;
}

QString FormatUsers(qint64 users, bool chinese) {
	auto n = QString();
	if (chinese) {
		if (users >= 100'000'000LL) {
			n = TrimNumber(users / 100'000'000.) + QString::fromUtf8("亿");
		} else if (users >= 10'000LL) {
			n = TrimNumber(users / 10'000.) + QString::fromUtf8("万");
		} else {
			n = QString::number(users);
		}
		return n + QString::fromUtf8("人使用");
	}
	if (users >= 1'000'000LL) {
		n = TrimNumber(users / 1'000'000.) + QChar('M');
	} else if (users >= 1'000LL) {
		n = TrimNumber(users / 1'000.) + QChar('K');
	} else {
		n = QString::number(users);
	}
	return n + ((users == 1)
		? QStringLiteral(" user")
		: QStringLiteral(" users"));
}

Cache::Cache(const QString &stored) {
	if (!stored.isEmpty()) {
		const auto document = QJsonDocument::fromJson(stored.toUtf8());
		if (document.isObject()) {
			_json = document.object();
		}
	}
}

QString Cache::serialize() const {
	return QString::fromUtf8(
		QJsonDocument(_json).toJson(QJsonDocument::Compact));
}

void Cache::putCategories(const QJsonObject &response) {
	_json.insert(QStringLiteral("categories"), response);
}

std::vector<Category> Cache::categories() const {
	return ParseCategories(_json.value(QStringLiteral("categories")).toObject());
}

void Cache::putFirstPage(const QString &category, const QJsonObject &response) {
	auto lists = _json.value(QStringLiteral("lists")).toObject();
	lists.insert(category, response);
	_json.insert(QStringLiteral("lists"), lists);
}

std::optional<Page> Cache::firstPage(const QString &category) const {
	const auto lists = _json.value(QStringLiteral("lists")).toObject();
	const auto page = lists.value(category);
	if (!page.isObject()) {
		return std::nullopt;
	}
	return ParsePage(page.toObject());
}

std::vector<Agent> Cache::allAgents() const {
	auto result = std::vector<Agent>();
	auto seen = QSet<QString>();
	const auto add = [&](Agent &&agent) {
		if (!seen.contains(agent.id)) {
			seen.insert(agent.id);
			result.push_back(std::move(agent));
		}
	};
	const auto lists = _json.value(QStringLiteral("lists")).toObject();
	for (auto i = lists.begin(); i != lists.end(); ++i) {
		for (auto &agent : ParsePage(i.value().toObject()).items) {
			add(std::move(agent));
		}
	}
	const auto details = _json.value(QStringLiteral("details")).toArray();
	for (const auto &value : details) {
		if (auto agent = ParseAgent(value.toObject())) {
			add(std::move(*agent));
		}
	}
	return result;
}

std::vector<Agent> Cache::offline(
		const QString &category,
		const QString &query) const {
	if (query.trimmed().isEmpty()) {
		if (auto page = firstPage(category)) {
			return std::move(page->items);
		}
	}
	auto result = std::vector<Agent>();
	for (auto &agent : allAgents()) {
		if ((category.isEmpty() || category == agent.category)
			&& agent.matches(query)) {
			result.push_back(std::move(agent));
		}
	}
	return result;
}

void Cache::putDetail(const Agent &agent) {
	if (agent.id.isEmpty()) {
		return;
	}
	// An array, oldest first: QJsonObject sorts its keys, so it can't tell
	// which detail is the oldest.
	auto details = _json.value(QStringLiteral("details")).toArray();
	for (auto i = details.size(); i != 0;) {
		--i;
		if (OptString(details.at(i).toObject().value(QStringLiteral("id")))
				== agent.id) {
			details.removeAt(i);
		}
	}
	details.push_back(agent.toJson());
	while (details.size() > kMaxCachedDetails) {
		details.removeFirst();
	}
	_json.insert(QStringLiteral("details"), details);
}

std::optional<Agent> Cache::detail(const QString &id) const {
	const auto details = _json.value(QStringLiteral("details")).toArray();
	for (const auto &value : details) {
		auto agent = ParseAgent(value.toObject());
		if (agent && agent->id == id) {
			return agent;
		}
	}
	for (auto &agent : allAgents()) {
		if (agent.id == id) {
			return std::move(agent);
		}
	}
	return std::nullopt;
}

std::vector<Agent> ParseUsed(const QString &stored) {
	auto result = std::vector<Agent>();
	const auto document = QJsonDocument::fromJson(stored.toUtf8());
	for (const auto &value : document.array()) {
		if (auto agent = ParseAgent(value.toObject())) {
			result.push_back(std::move(*agent));
		}
	}
	return result;
}

QString UsedToString(const std::vector<Agent> &used) {
	return QString::fromUtf8(
		QJsonDocument(ToArray(used)).toJson(QJsonDocument::Compact));
}

std::vector<Agent> MarkUsed(
		const std::vector<Agent> &used,
		const Agent &agent) {
	auto result = std::vector<Agent>{ agent };
	for (const auto &a : used) {
		if (a.id != agent.id && int(result.size()) < kMaxUsed) {
			result.push_back(a);
		}
	}
	return result;
}

std::vector<Agent> RefreshUsed(
		const std::vector<Agent> &used,
		const Agent &agent) {
	auto result = used;
	for (auto &a : result) {
		if (a.id == agent.id) {
			a = agent;
		}
	}
	return result;
}

std::vector<Agent> RemoveUsed(
		const std::vector<Agent> &used,
		const QString &id) {
	auto result = std::vector<Agent>();
	for (const auto &a : used) {
		if (a.id != id) {
			result.push_back(a);
		}
	}
	return result;
}

} // namespace Luckgram::AI::Market
