/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"
#include "luckgram/ai/ai_market_logic.h"

#include <QtCore/QJsonDocument>

// Same cases as Android AgentMarketLogicTest.
using namespace Luckgram::AI::Market;

namespace {

[[nodiscard]] QJsonObject Json(const char *text) {
	return QJsonDocument::fromJson(QByteArray(text)).object();
}

const char kPage[] = "{\"items\":["
	"{\"id\":\"email-pro\",\"name\":{\"zh\":\"职场邮件\",\"en\":\"Work email\"},"
	"\"description\":{\"zh\":\"写邮件\",\"en\":\"Writes emails\"},"
	"\"category\":\"work\",\"avatar\":{\"color\":\"#475569\",\"emoji\":\"📧\"},"
	"\"examples\":[{\"zh\":\"请假邮件\",\"en\":\"Leave email\"}],"
	"\"author\":\"luckgram\",\"pro_only\":false,\"model_tier\":\"standard\","
	"\"users\":12345,\"instruction\":\"SECRET\"},"
	"{\"id\":\"translator-x\",\"name\":{\"zh\":\"翻译官\",\"en\":\"Translator\"},"
	"\"description\":{\"zh\":\"中英互译\",\"en\":\"Translate\"},"
	"\"category\":\"study\",\"avatar\":{\"color\":\"bad\"},\"pro_only\":true,"
	"\"model_tier\":\"premium\",\"users\":3},"
	"{\"name\":{\"zh\":\"没有 id\"}}],\"next_cursor\":\"bzIw\"}";

[[nodiscard]] std::vector<Agent> Items() {
	return ParsePage(Json(kPage)).items;
}

} // namespace

LUCKGRAM_TEST(AiMarketParsesPageWithoutInstruction) {
	const auto p = ParsePage(Json(kPage));
	LG_EQUAL(int(p.items.size()), 2);
	LG_EQUAL(p.nextCursor, QStringLiteral("bzIw"));
	const auto &a = p.items[0];
	LG_EQUAL(a.name.pick(true), QString::fromUtf8("职场邮件"));
	LG_EQUAL(a.name.pick(false), QStringLiteral("Work email"));
	LG_EQUAL(a.color, quint32(0xFF475569U));
	LG_EQUAL(a.avatarText(true), QString::fromUtf8("📧"));
	LG_EQUAL(int(a.examples.size()), 1);
	LG_EQUAL(a.users, qint64(12345));
	const auto stored = QJsonDocument(a.toJson()).toJson();
	LG_CHECK(!stored.contains("SECRET"));
	const auto &b = p.items[1];
	LG_EQUAL(b.color, kDefaultColor);
	LG_CHECK(b.proOnly);
	LG_CHECK(b.isPremium());
	LG_EQUAL(b.avatarText(true), QString::fromUtf8("翻"));
	LG_CHECK(ParsePage(Json("{\"items\":[],\"next_cursor\":null}"))
		.nextCursor.isEmpty());
}

LUCKGRAM_TEST(AiMarketCategories) {
	const auto c = ParseCategories(Json(
		"{\"categories\":[{\"id\":\"writing\",\"name\":{\"zh\":\"写作\","
		"\"en\":\"Writing\"},\"count\":4},{\"name\":{}}]}"));
	LG_EQUAL(int(c.size()), 1);
	LG_EQUAL(c[0].name.pick(true), QString::fromUtf8("写作"));
	LG_EQUAL(c[0].count, 4);
}

LUCKGRAM_TEST(AiMarketPaths) {
	LG_EQUAL(
		ListPath(QString(), QStringLiteral(" "), QString(), 20),
		QStringLiteral("/v1/agents?limit=20"));
	LG_EQUAL(
		ListPath(
			QStringLiteral("work"),
			QString::fromUtf8("翻译"),
			QStringLiteral("bzIw"),
			20),
		QStringLiteral("/v1/agents?limit=20&category=work"
			"&q=%E7%BF%BB%E8%AF%91&cursor=bzIw"));
	LG_EQUAL(
		ListPath(QString(), QStringLiteral("a b"), QString(), 5),
		QStringLiteral("/v1/agents?limit=5&q=a%20b"));
	const auto longQuery = QString(60, QChar('x'));
	LG_CHECK(ListPath(QString(), longQuery, QString(), 5).endsWith(
		QStringLiteral("&q=") + longQuery.left(50)));
	LG_EQUAL(
		DetailPath(QStringLiteral("email-pro")),
		QStringLiteral("/v1/agents/email-pro"));
	LG_EQUAL(
		QString::fromUtf8(QJsonDocument(ChatParams(
			QStringLiteral("email-pro"))).toJson(QJsonDocument::Compact)),
		QStringLiteral("{\"agent_id\":\"email-pro\"}"));
	LG_EQUAL(
		ConversationKey(QStringLiteral("email-pro")),
		QStringLiteral("market:email-pro"));
}

LUCKGRAM_TEST(AiMarketSearch) {
	const auto a = Items()[1];
	LG_CHECK(a.matches(QString::fromUtf8("翻译")));
	LG_CHECK(a.matches(QStringLiteral("TRANSL")));
	LG_CHECK(a.matches(QString()));
	LG_CHECK(!a.matches(QString::fromUtf8("邮件")));
}

LUCKGRAM_TEST(AiMarketCacheAndOffline) {
	auto c = Cache();
	c.putFirstPage(QString(), Json(kPage));
	c.putCategories(Json("{\"categories\":[{\"id\":\"work\",\"name\":"
		"{\"zh\":\"工作\",\"en\":\"Work\"},\"count\":1}]}"));
	const auto back = Cache(c.serialize());
	LG_EQUAL(int(back.firstPage(QString())->items.size()), 2);
	LG_EQUAL(int(back.categories().size()), 1);
	LG_EQUAL(int(back.offline(QString(), QString()).size()), 2);
	LG_EQUAL(int(back.offline(QStringLiteral("work"), QString()).size()), 1);
	// Copies: LG_EQUAL binds a reference, a temporary's member would dangle.
	LG_EQUAL(
		QString(back.offline(QString(), QString::fromUtf8("翻译"))[0].id),
		QStringLiteral("translator-x"));
	LG_EQUAL(
		int(back.offline(QStringLiteral("work"), QString::fromUtf8("翻译")).size()),
		0);
	LG_EQUAL(
		QString(back.detail(QStringLiteral("email-pro"))->id),
		QStringLiteral("email-pro"));
	LG_CHECK(!back.detail(QStringLiteral("nope")));
	LG_EQUAL(int(Cache(QStringLiteral("not json")).allAgents().size()), 0);
}

LUCKGRAM_TEST(AiMarketDetailCacheKeepsTheNewest) {
	auto c = Cache();
	auto agent = Items()[0];
	for (auto i = 0; i < kMaxCachedDetails + 5; ++i) {
		agent.id = QStringLiteral("a") + QString::number(i);
		c.putDetail(agent);
	}
	// Putting an existing one again moves it to the end, not a duplicate.
	agent.id = QStringLiteral("a10");
	c.putDetail(agent);
	const auto back = Cache(c.serialize());
	LG_EQUAL(int(back.allAgents().size()), kMaxCachedDetails);
	LG_CHECK(!back.detail(QStringLiteral("a4")));
	LG_CHECK(back.detail(QStringLiteral("a5")).has_value());
	LG_CHECK(back.detail(QStringLiteral("a10")).has_value());
	LG_CHECK(back.detail(QStringLiteral("a104")).has_value());
}

LUCKGRAM_TEST(AiMarketUsedList) {
	const auto items = Items();
	auto used = std::vector<Agent>();
	used = MarkUsed(used, items[0]);
	used = MarkUsed(used, items[1]);
	used = MarkUsed(used, items[0]);
	LG_EQUAL(int(used.size()), 2);
	LG_EQUAL(used[0].id, QStringLiteral("email-pro"));
	const auto back = ParseUsed(UsedToString(used));
	LG_EQUAL(back[1].id, QStringLiteral("translator-x"));
	LG_EQUAL(back[0].emoji, QString::fromUtf8("📧"));
	LG_EQUAL(int(RemoveUsed(back, QStringLiteral("email-pro")).size()), 1);
	LG_EQUAL(int(ParseUsed(QStringLiteral("garbage")).size()), 0);
	auto renamed = items[1];
	renamed.name.en = QStringLiteral("Translator 2");
	const auto refreshed = RefreshUsed(back, renamed);
	LG_EQUAL(refreshed[1].name.en, QStringLiteral("Translator 2"));
	LG_EQUAL(refreshed[0].id, QStringLiteral("email-pro"));
}

LUCKGRAM_TEST(AiMarketProGate) {
	const auto items = Items();
	LG_CHECK(!NeedsUpgrade(items[0], false));
	LG_CHECK(NeedsUpgrade(items[1], false));
	LG_CHECK(!NeedsUpgrade(items[1], true));
}

LUCKGRAM_TEST(AiMarketUsers) {
	LG_EQUAL(FormatUsers(12, true), QString::fromUtf8("12人使用"));
	LG_EQUAL(FormatUsers(12345, true), QString::fromUtf8("1.2万人使用"));
	LG_EQUAL(FormatUsers(1, false), QStringLiteral("1 user"));
	LG_EQUAL(FormatUsers(12345, false), QStringLiteral("12.3K users"));
	LG_EQUAL(FormatUsers(2000000, false), QStringLiteral("2M users"));
}

LUCKGRAM_TEST(AiMarketColors) {
	LG_EQUAL(ParseColor(QStringLiteral("#00acc1"), 0), quint32(0xFF00ACC1U));
	LG_EQUAL(ParseColor(QStringLiteral("00ACC1"), 7), quint32(7));
	LG_EQUAL(ColorString(0xFF00ACC1U), QStringLiteral("#00ACC1"));
	LG_EQUAL(ColorString(0xFF000001U), QStringLiteral("#000001"));
}
