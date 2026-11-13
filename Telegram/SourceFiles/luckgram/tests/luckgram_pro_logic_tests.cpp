/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/pro/luckgram_pro_logic.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

// D12: ported from Android ProStateTest, ProPerksTest and AccountLogicTest.
namespace {

using namespace Luckgram::Pro;

QString S(const char *utf8) {
	return QString::fromUtf8(utf8);
}

QJsonObject J(const char *json) {
	return QJsonDocument::fromJson(QByteArray(json)).object();
}

const auto kNow = ParseTime(S("2026-11-03T00:00:00Z"));

} // namespace

LUCKGRAM_TEST(ProS12Fields) {
	const auto s = ParseMe(J(R"({"pro":{"active":true,"expires_at":"2026-12-03T00:00:00Z"},
		"credits":{"remaining":37,"total":50,"resets_at":"2026-12-01T00:00:00Z"},
		"perks":["ai","no_ads","summary","ai",""],
		"limits":{"accounts_max":50,"summary_max_messages":1000}})"), kNow);
	LG_CHECK(s.isPro(kNow));
	LG_EQUAL(s.credits(kNow), 37);
	LG_EQUAL(s.creditsTotal, 50);
	LG_EQUAL(s.creditsResetsAtMs, ParseTime(S("2026-12-01T00:00:00Z")));
	LG_EQUAL(s.perks, (QStringList{ S("ai"), S("no_ads"), S("summary") }));
	LG_CHECK(s.hasPerk(S(kPerkNoAds)));
	LG_CHECK(!s.hasPerk(S(kPerkAgentMarket)));
	LG_EQUAL(s.accountsMaxNow(kNow), 50);
	LG_EQUAL(s.summaryMaxNow(kNow), 1000);
}

LUCKGRAM_TEST(ProOlderTierFields) {
	const auto s = ParseMe(
		J(R"({"tier":"pro","pro_expires_at":"2026-11-10T00:00:00Z"})"),
		kNow);
	LG_CHECK(s.isPro(kNow));
	LG_CHECK(!s.isPro(ParseTime(S("2026-11-10T00:00:01Z"))));
	const auto free = ParseMe(
		J(R"({"tier":"free","pro_expires_at":null})"),
		kNow);
	LG_CHECK(!free.isPro(kNow));
}

LUCKGRAM_TEST(ProMissingFieldsAreFree) {
	const auto s = ParseMe(J(R"({"quota":{}})"), kNow);
	LG_CHECK(!s.isPro(kNow));
	LG_EQUAL(s.credits(kNow), 0);
	LG_CHECK(s.perks.isEmpty());
	LG_EQUAL(s.accountsMaxNow(kNow), kAccountsMaxFree);
	LG_EQUAL(s.summaryMaxNow(kNow), kSummaryMaxFree);
	LG_CHECK(!ParseMe(QJsonObject(), kNow).isPro(kNow));
	LG_CHECK(!State().isPro(kNow));
}

LUCKGRAM_TEST(ProExpiredOrInactiveIsFree) {
	const auto expired = ParseMe(J(R"({"pro":{"active":true,"expires_at":"2026-11-01T00:00:00Z"},"credits":{"remaining":5}})"), kNow);
	LG_CHECK(!expired.isPro(kNow));
	LG_EQUAL(expired.credits(kNow), 0);
	LG_EQUAL(expired.summaryMaxNow(kNow), kSummaryMaxFree);
	const auto off = ParseMe(J(R"({"pro":{"active":false,"expires_at":"2027-01-01T00:00:00Z"}})"), kNow);
	LG_CHECK(!off.isPro(kNow));
	// Active without a known expiry: the server's word decides.
	const auto open = ParseMe(J(R"({"pro":{"active":true}})"), kNow);
	LG_CHECK(open.isPro(kNow));
	// Pro without limits: no account limit (specs#135), Pro summary default.
	LG_EQUAL(open.accountsMaxNow(kNow), 0);
	LG_EQUAL(open.summaryMaxNow(kNow), kSummaryMaxPro);
}

LUCKGRAM_TEST(ProCacheKeepsProOfflineUntilExpiry) {
	const auto s = ParseMe(J(R"({"pro":{"active":true,"expires_at":"2026-11-20T00:00:00Z"},
		"credits":{"remaining":3,"total":50},"perks":["ai"],"limits":{"summary_max_messages":500}})"), kNow);
	const auto c = FromCache(ToCache(s));
	LG_CHECK(c.sameAs(s));
	LG_CHECK(c.isPro(kNow));
	LG_EQUAL(c.summaryMaxNow(kNow), 500);
	LG_EQUAL(c.fetchedAtMs, kNow);
	LG_CHECK(!c.isPro(ParseTime(S("2026-11-20T00:00:00Z"))));
	LG_CHECK(!FromCache(QByteArray()).isPro(kNow));
	LG_CHECK(!FromCache(QByteArray("not json")).isPro(kNow));
}

LUCKGRAM_TEST(ProChangeDetection) {
	const auto a = ParseMe(J(R"({"pro":{"active":true},"credits":{"remaining":3}})"), kNow);
	const auto b = ParseMe(J(R"({"pro":{"active":true},"credits":{"remaining":2}})"), kNow + 1000);
	LG_CHECK(!a.sameAs(b));
	LG_CHECK(a.sameAs(ParseMe(J(R"({"pro":{"active":true},"credits":{"remaining":3}})"), kNow + 5000)));
}

LUCKGRAM_TEST(PerkRowsFollowFixedOrderAndPerks) {
	const auto rows = Rows(
		{ S("no_ads"), S("ai"), S("agent_market"), S("unknown"), S("accounts") },
		Numbers(),
		true);
	LG_EQUAL(int(rows.size()), 4);
	if (rows.size() == 4) {
		LG_EQUAL(rows[0].perk, S("agent_market"));
		LG_EQUAL(rows[1].perk, S("accounts"));
		LG_EQUAL(rows[2].perk, S("ai"));
		LG_EQUAL(rows[3].perk, S("no_ads"));
		LG_CHECK(rows[0].proOnly);
		LG_CHECK(rows[1].proOnly);
		LG_CHECK(!rows[2].proOnly);
	}
	LG_EQUAL(int(Rows(PerkOrder(), Numbers(), false).size()), 7);
	LG_CHECK(Rows(QStringList(), Numbers(), true).empty());
}

LUCKGRAM_TEST(LivePerksPreferMeThenConfig) {
	LG_EQUAL(
		LivePerks({ S("ai"), S("ai") }, QJsonValue(S("summary"))),
		QStringList{ S("ai") });
	LG_EQUAL(
		LivePerks({}, QJsonValue(S("summary, no_ads,"))),
		(QStringList{ S("summary"), S("no_ads") }));
	LG_EQUAL(
		LivePerks({}, QJsonValue(S(R"(["ai","translate"])"))),
		(QStringList{ S("ai"), S("translate") }));
	LG_EQUAL(
		LivePerks({}, QJsonValue(QJsonArray{ S("ai"), S("translate") })),
		(QStringList{ S("ai"), S("translate") }));
	LG_CHECK(LivePerks({}, QJsonValue()).isEmpty());
}

LUCKGRAM_TEST(PerkSubtitlesUseRemoteNumbers) {
	auto n = Numbers();
	n.accountsMaxFree = 5;
	n.chatPro = 500;
	n.chatFree = 50;
	n.monthlyCredits = 50;
	n.summaryMaxPro = 1000;
	n.translatePro = 2000;
	n.translateFree = 200;
	const auto zh = Texts(true);
	LG_EQUAL(zh.perkSubtitle(S("accounts"), n), S("同时登录的账号数量不限（免费 5 个）；账号备份加密存到云端。"));
	LG_EQUAL(zh.perkSubtitle(S("accounts"), Numbers()), S("同时登录的账号数量不限；账号备份加密存到云端。"));
	LG_EQUAL(zh.perkSubtitle(S("ai"), n), S("AI 对话每天 500 次（免费 50 次），每月另送 50 个 AI 点数，用于高级模型。"));
	LG_EQUAL(zh.perkSubtitle(S("summary"), n), S("总结最多 1000 条消息并可追问（免费 100 条）。"));
	LG_EQUAL(zh.perkSubtitle(S("translate"), n), S("每天翻译 2000 次（免费 200 次）。"));
	// Numbers not configured: no made-up numbers.
	LG_EQUAL(zh.perkSubtitle(S("translate"), Numbers()), S("每天可翻译的次数更多。"));
	const auto en = Texts(false);
	for (const auto &perk : PerkOrder()) {
		const auto all = zh.perkTitle(perk)
			+ zh.perkSubtitle(perk, n)
			+ en.perkSubtitle(perk, n);
		LG_CHECK(!all.contains(S("自动续费")));
		LG_CHECK(!all.toLower().contains(S("auto-renew")));
	}
}

LUCKGRAM_TEST(CreditsLine) {
	const auto utc = QTimeZone::utc();
	const auto resets = int64_t(1764460800000LL); // 2025-11-30T00:00Z
	LG_EQUAL(Texts(true).creditsLine(12, 50, resets, utc), S("本月 AI 点数 剩余 12 / 50，30 日重置"));
	LG_EQUAL(Texts(false).creditsLine(12, 50, resets, utc), S("AI credits this month: 12 / 50 left, resets Nov 30"));
	LG_EQUAL(Texts(true).creditsValue(-3, 50, 0, utc), S("剩余 0 / 50"));
}

LUCKGRAM_TEST(SummaryRangesNeedPro) {
	LG_CHECK(!SummaryNeedsPro(100, false));
	LG_CHECK(SummaryNeedsPro(300, false));
	LG_CHECK(SummaryNeedsPro(500, false));
	LG_CHECK(!SummaryNeedsPro(500, true));
	LG_EQUAL(SummaryUnreadCap(800, false, 1000), 100);
	LG_EQUAL(SummaryUnreadCap(800, true, 1000), 800);
	LG_EQUAL(SummaryUnreadCap(5000, true, 1000), 1000);
	LG_EQUAL(SummaryUnreadCap(40, false, 1000), 40);
}

LUCKGRAM_TEST(LinkStatusParsing) {
	const auto s = ParseLink(J(R"({"linked":true,"devices":2,"max_devices":5,"request":"linked"})"));
	LG_CHECK(s.linked);
	LG_EQUAL(s.devices, 2);
	LG_EQUAL(s.maxDevices, 5);
	LG_EQUAL(s.request, S("linked"));
	const auto none = ParseLink(J(R"({"linked":false,"devices":0,"max_devices":5})"));
	LG_CHECK(!none.linked);
	LG_CHECK(none.request.isEmpty());
}

LUCKGRAM_TEST(LinkPollsOnlyWhilePendingAndUnexpired) {
	const auto expires = int64_t(1000000);
	LG_CHECK(ShouldPoll(S("pending"), expires - 1, expires));
	LG_CHECK(ShouldPoll(QString(), expires - 1, expires));
	LG_CHECK(!ShouldPoll(S("pending"), expires, expires));
	for (const auto done : { "linked", "expired", "used", "limit_reached" }) {
		LG_CHECK(!ShouldPoll(S(done), 0, expires));
	}
}

LUCKGRAM_TEST(LinkStartAndTimes) {
	const auto st = ParseStart(J(R"({"nonce":"abc","url":"https://t.me/LuckgramBot?start=abc","expires_at":"2026-11-01T00:10:00Z"})"));
	LG_EQUAL(st.expiresAtMs, int64_t(1793491800000LL));
	LG_CHECK(IsTelegramLink(st.url));
	LG_CHECK(!IsTelegramLink(S("https://evil.example.com/t.me/x")));
	LG_CHECK(!IsTelegramLink(S("https://t.me/")));
	LG_EQUAL(ParseTime(S("2026-11-01T08:10:00+08:00")), st.expiresAtMs);
	LG_EQUAL(ParseTime(S("2026-11-01T00:10:00.123Z")), st.expiresAtMs);
	LG_EQUAL(ParseTime(S("2026-11-01T00:10:00.123456789Z")), st.expiresAtMs);
	LG_EQUAL(ParseTime(S("2026-11-01T00:10:00")), int64_t(0)); // No zone.
	LG_EQUAL(ParseTime(S("garbage")), int64_t(0));
	LG_EQUAL(ParseTime(QString()), int64_t(0));
	const auto t = ParseTime(S("2026-10-31T16:30:00Z"));
	LG_EQUAL(Date(t, QTimeZone(QByteArray("Asia/Shanghai"))), S("2026-11-01"));
	LG_EQUAL(Date(t, QTimeZone::utc()), S("2026-10-31"));
	LG_CHECK(Date(0, QTimeZone::utc()).isEmpty());
}

LUCKGRAM_TEST(BillingHidesFreePlans) {
	const auto b = ParseBilling(J(R"({"enabled":true,"linked":true,"tier":"pro",
		"pro_expires_at":"2026-12-01T00:00:00Z","plans":[{"id":"pro_month","days":30,"stars":100},{"id":"x","days":1,"stars":0}]})"));
	LG_CHECK(b.enabled);
	LG_CHECK(b.pro);
	LG_EQUAL(b.proExpiresAt, S("2026-12-01T00:00:00Z"));
	LG_EQUAL(int(b.plans.size()), 1);
	if (!b.plans.empty()) {
		LG_EQUAL(b.plans[0].id, S("pro_month"));
		LG_EQUAL(b.plans[0].days, 30);
		LG_EQUAL(b.plans[0].stars, 100);
	}
	const auto free = ParseBilling(J(R"({"enabled":false,"linked":false,"plans":[],"tier":"free","pro_expires_at":null})"));
	LG_CHECK(!free.pro);
	LG_CHECK(free.proExpiresAt.isEmpty());
}

LUCKGRAM_TEST(AccountTextsInBothLanguages) {
	const auto zh = Texts(true);
	const auto en = Texts(false);
	LG_CHECK(zh.linkResult(S("limit_reached"), 5).contains(S("5 台")));
	LG_CHECK(en.linkResult(S("limit_reached"), 5).contains(S("5 linked")));
	for (const auto code : { "link_unavailable", "already_linked", "not_linked", "billing_disabled", "rate_limited", "network_error", "x", "" }) {
		LG_CHECK(!zh.error(S(code)).isEmpty());
		LG_CHECK(!en.error(S(code)).isEmpty());
	}
	LG_EQUAL(zh.proUntil(S("2026-12-01")), S("Pro，到期 2026-12-01"));
	LG_EQUAL(zh.proUntil(QString()), S("Pro")); // pro.free_for_all.
	LG_EQUAL(zh.stars(100), S("100 ⭐"));
	LG_EQUAL(en.plan({ .id = S("m"), .days = 30, .stars = 100 }), S("30 days"));
}

LUCKGRAM_TEST(BotUsernameFromLink) {
	LG_EQUAL(BotUsername(S("https://t.me/LuckgramBot?start=abc")), S("LuckgramBot"));
	LG_EQUAL(BotUsername(S("t.me/luck_bot")), S("luck_bot"));
	LG_CHECK(BotUsername(S("https://example.com/x")).isEmpty());
	LG_CHECK(BotUsername(S("https://t.me/ab?start=1")).isEmpty());
	LG_CHECK(BotUsername(QString()).isEmpty());
}

// D3 #6: ported from Android AccountLimitTest.
LUCKGRAM_TEST(AccountsLimitFreeFromConfigProSlots) {
	LG_EQUAL(AccountsLimit(false, 0), kAccountsMaxFree);
	LG_EQUAL(AccountsLimit(false, 12), 12);
	LG_EQUAL(AccountsLimit(false, 3), 3); // No lower bound but 1.
	LG_EQUAL(AccountsLimit(false, -5), kAccountsMaxFree);
	LG_EQUAL(AccountsLimit(false, 1000), int(kAccountSlots));
	LG_EQUAL(AccountsLimit(true, 0), int(kAccountSlots));
	LG_EQUAL(AccountsLimit(true, 3), int(kAccountSlots));
}

LUCKGRAM_TEST(AccountsOnlyAddingIsBlocked) {
	LG_CHECK(CanAddAccount(9, 10));
	LG_CHECK(!CanAddAccount(10, 10));
	LG_CHECK(!CanAddAccount(12, 10)); // Above the limit: nobody logged out.
	LG_CHECK(OfferAccountsUpgrade(false, 10));
	LG_CHECK(!OfferAccountsUpgrade(true, 10));
	LG_CHECK(!OfferAccountsUpgrade(false, int(kAccountSlots)));
}

LUCKGRAM_TEST(AccountsLimitTexts) {
	LG_EQUAL(Texts(true).accountsLimitReached(10, true), S("免费版最多可登录 10 个账号。升级 Pro 可登录的账号数量不限"));
	LG_EQUAL(Texts(false).accountsLimitReached(10, true), S("Free accounts can log in to at most 10 accounts. Upgrade to Pro for unlimited accounts"));
	LG_EQUAL(Texts(true).accountsLimitReached(100, false), S("这台设备最多可登录 100 个账号"));
}

// D12 Pro community: ported from Android ProCommunityLogicTest.
LUCKGRAM_TEST(CommunityInviteLinkIsJoin) {
	const auto i = ParseCommunityInvite(J(R"({"invite_link":"https://t.me/+AbC123","expires_at":"2026-11-04T00:00:00Z"})"));
	LG_CHECK(i.kind == CommunityInvite::Kind::Join);
	LG_EQUAL(i.link, S("https://t.me/+AbC123"));
}

LUCKGRAM_TEST(CommunityAlreadyMemberInBody) {
	const auto i = ParseCommunityInvite(J(R"({"already_member":true,"chat_id":-1001234567890})"));
	LG_CHECK(i.kind == CommunityInvite::Kind::AlreadyMember);
	LG_EQUAL(i.channelId, int64_t(1234567890LL));
	LG_CHECK(i.link.isEmpty());
}

LUCKGRAM_TEST(CommunityAlreadyMemberAsErrorKeepsLink) {
	const auto i = CommunityAlreadyMember(J(R"({"code":"already_member","chat_link":"https://t.me/c/1234567890"})"));
	LG_CHECK(i.kind == CommunityInvite::Kind::AlreadyMember);
	LG_EQUAL(i.link, S("https://t.me/c/1234567890"));
	LG_EQUAL(i.channelId, int64_t(0));
	LG_CHECK(CommunityAlreadyMember(QJsonObject()).kind
		== CommunityInvite::Kind::AlreadyMember);
}

LUCKGRAM_TEST(CommunityNonTelegramOrMissingLinkIsBad) {
	LG_CHECK(ParseCommunityInvite(J(R"({"invite_link":"https://luckgram.cc/+x"})")).kind
		== CommunityInvite::Kind::BadAnswer);
	LG_CHECK(ParseCommunityInvite(J("{}")).kind
		== CommunityInvite::Kind::BadAnswer);
}

LUCKGRAM_TEST(CommunityLinksAndChannelIds) {
	LG_CHECK(IsOpenableTelegramLink(S("https://t.me/+x")));
	LG_CHECK(IsOpenableTelegramLink(S("tg://join?invite=x")));
	LG_CHECK(!IsOpenableTelegramLink(S("https://t.me/")));
	LG_CHECK(!IsOpenableTelegramLink(S("http://t.me/+x")));
	LG_CHECK(!IsOpenableTelegramLink(QString()));
	LG_EQUAL(CommunityChannelId(-1001234567890LL), int64_t(1234567890LL));
	LG_EQUAL(CommunityChannelId(42), int64_t(42));
	LG_EQUAL(CommunityChannelId(-42), int64_t(0));
	LG_EQUAL(CommunityChannelId(0), int64_t(0));
	LG_CHECK(ShowCommunityRow(true, true));
	LG_CHECK(!ShowCommunityRow(false, true));
	LG_CHECK(!ShowCommunityRow(true, false));
}
