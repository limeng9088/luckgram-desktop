/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTimeZone>

#include <cstdint>
#include <vector>

// D12 (mobile 29 / S6 / S7 / S12): the pure logic of Luckgram Pro and the
// Telegram account link, without any UI or app types (QtCore only), so it
// runs in luckgram_tests. Same rules and texts as Android ProState,
// ProPerks and AccountLogic / iOS LuckgramProState, LuckgramProPerks and
// LuckgramAccountLogic; the test cases are ported from their unit tests.
namespace Luckgram::Pro {

// pro.perks values (S12); a perk missing from the list hides its row.
inline constexpr auto kPerkAgentMarket = "agent_market";
inline constexpr auto kPerkAccounts = "accounts";
inline constexpr auto kPerkAi = "ai";
inline constexpr auto kPerkSummary = "summary";
inline constexpr auto kPerkTranslate = "translate";
inline constexpr auto kPerkCommunity = "community";
inline constexpr auto kPerkNoAds = "no_ads";

// Defaults when the server sends no limits (S12 §5, specs#135).
inline constexpr auto kAccountsMaxFree = 10;
inline constexpr auto kSummaryMaxFree = 100;
inline constexpr auto kSummaryMaxPro = 1000;
inline constexpr auto kDefaultMonthlyCredits = 50;

// Remote config keys (S4 / S12).
inline constexpr auto kConfigBillingEnabled = "billing.enabled";
inline constexpr auto kConfigPerks = "pro.perks";
inline constexpr auto kConfigMonthlyCredits = "pro.monthly_credits";
inline constexpr auto kConfigSummaryMax = "pro.summary_max_messages";
inline constexpr auto kConfigAccountsMaxFree = "accounts.max_free";
inline constexpr auto kConfigChatPro = "quota.pro.chat_per_day";
inline constexpr auto kConfigChatFree = "quota.free.chat_per_day";
inline constexpr auto kConfigTranslatePro = "quota.pro.translate_per_day";
inline constexpr auto kConfigTranslateFree = "quota.free.translate_per_day";

// Server error codes (S12 §8).
inline constexpr auto kErrorNotPro = "not_pro";
inline constexpr auto kErrorCreditsExhausted = "credits_exhausted";

// The Pro state from GET /v1/me, as one value. Anything missing counts as
// free; cached so Pro still holds offline until expires_at.
struct State {
	bool active = false; // pro.active as sent; use isPro().
	int64_t expiresAtMs = 0; // 0 = unknown (then active alone decides).
	int creditsRemaining = 0;
	int creditsTotal = 0;
	int64_t creditsResetsAtMs = 0; // 0 = unknown.
	QStringList perks;
	int accountsMax = 0; // 0 = not sent.
	int summaryMaxMessages = 0; // 0 = not sent.
	int64_t fetchedAtMs = 0; // 0 = the free default.

	// Server said active and, when an expiry is known, it hasn't passed.
	[[nodiscard]] bool isPro(int64_t nowMs) const;
	[[nodiscard]] bool hasPerk(const QString &perk) const;
	// AI credits left this period; 0 when not Pro.
	[[nodiscard]] int credits(int64_t nowMs) const;
	// limits.accounts_max, else the free default for free users and 0
	// ("no limit") for Pro users (specs#135).
	[[nodiscard]] int accountsMaxNow(int64_t nowMs) const;
	[[nodiscard]] int summaryMaxNow(int64_t nowMs) const;
	// Whether something a user would see differs (fetchedAtMs ignored).
	[[nodiscard]] bool sameAs(const State &other) const;
};

// Parses a /v1/me body (S12 fields, falling back to the older tier /
// pro_expires_at). Never fails.
[[nodiscard]] State ParseMe(const QJsonObject &me, int64_t nowMs);
[[nodiscard]] QByteArray ToCache(const State &state);
// Reads ToCache(); the free default for anything unreadable.
[[nodiscard]] State FromCache(const QByteArray &cache);

// RFC 3339 ("2026-11-13T09:30:00Z", "+08:00", fractional seconds) to Unix
// milliseconds; 0 for empty or invalid.
[[nodiscard]] int64_t ParseTime(const QString &rfc3339);
// yyyy-MM-dd in tz; empty for 0.
[[nodiscard]] QString Date(int64_t ms, const QTimeZone &tz);

// --- Account link (S6).

inline constexpr auto kLinkPollIntervalMs = 2000;
// After opening an invoice: re-read the membership this often, this many times.
inline constexpr auto kProRefreshIntervalMs = 3000;
inline constexpr auto kProRefreshTimes = 20;
// GET /v1/me again on activation only after this long.
inline constexpr auto kMinRefreshIntervalMs = 60 * 1000;

struct LinkStatus {
	bool linked = false;
	int devices = 0;
	int maxDevices = 0;
	QString request; // pending / linked / expired / used / limit_reached, or empty.
};
[[nodiscard]] LinkStatus ParseLink(const QJsonObject &json);

struct LinkStart {
	QString nonce;
	QString url;
	int64_t expiresAtMs = 0;
};
[[nodiscard]] LinkStart ParseStart(const QJsonObject &json);

// Keep polling while the request is pending (or not known yet) and the
// nonce has not expired.
[[nodiscard]] bool ShouldPoll(
	const QString &request,
	int64_t nowMs,
	int64_t expiresAtMs);
// Bot username from a t.me/<bot>?start=<nonce> link, or empty.
[[nodiscard]] QString BotUsername(const QString &link);
// Only links the server hands out: https://t.me/...
[[nodiscard]] bool IsTelegramLink(const QString &url);

// --- Billing (S7).

struct Plan {
	QString id;
	int days = 0;
	int stars = 0;
};

struct Billing {
	bool enabled = false;
	bool linked = false;
	bool pro = false;
	QString proExpiresAt; // RFC 3339 or empty.
	std::vector<Plan> plans; // Only plans with a price.
};
[[nodiscard]] Billing ParseBilling(const QJsonObject &json);

// --- Subscription page perks (29 需求 2).

// Numbers for the perk texts; 0 = unknown (left out of the text).
struct Numbers {
	int accountsMaxFree = 0;
	int chatPro = 0;
	int chatFree = 0;
	int monthlyCredits = kDefaultMonthlyCredits;
	int summaryMaxPro = kSummaryMaxPro;
	int translatePro = 0;
	int translateFree = 0;
};

struct Row {
	QString perk;
	bool proOnly = false;
	QString title;
	QString subtitle;
};

// Display order of the subscription page.
[[nodiscard]] QStringList PerkOrder();
// From /v1/me when it sent any, else pro.perks of the remote config (a JSON
// array or a comma separated string), else none.
[[nodiscard]] QStringList LivePerks(
	const QStringList &fromMe,
	const QJsonValue &fromConfig);
// PerkOrder() filtered by perks; unknown names are ignored.
[[nodiscard]] std::vector<Row> Rows(
	const QStringList &perks,
	const Numbers &numbers,
	bool zh);
[[nodiscard]] bool IsProOnlyRow(const QString &perk);

// Summary ranges (29 需求 5).
[[nodiscard]] bool SummaryNeedsPro(int count, bool pro);
[[nodiscard]] int SummaryUnreadCap(int unread, bool pro, int proMax);

// Why the upgrade box opens (29 需求 6).
enum class Reason {
	Quota,
	ProOnly,
	SummaryRange,
	FollowUp,
	PremiumModel,
};

// Texts in both languages; mobile wording (AccountLogic.Texts and
// ProPerks.Texts), only "点按" became "点击".
class Texts {
public:
	explicit Texts(bool zh) : _zh(zh) {
	}

	// Account (S6).
	[[nodiscard]] QString accountTitle() const;
	[[nodiscard]] QString telegramAccount() const;
	[[nodiscard]] QString linked() const;
	[[nodiscard]] QString notLinked() const;
	[[nodiscard]] QString devices(int count, int max) const;
	[[nodiscard]] QString link() const;
	[[nodiscard]] QString unlink() const;
	[[nodiscard]] QString waiting() const;
	[[nodiscard]] QString linkInfo() const;
	[[nodiscard]] QString unlinkConfirmTitle() const;
	[[nodiscard]] QString unlinkConfirmText() const;
	[[nodiscard]] QString linkedNow() const;
	[[nodiscard]] QString unlinkedNow() const;
	[[nodiscard]] QString linkResult(const QString &request, int max) const;

	// Pro (S7).
	[[nodiscard]] QString proTitle() const;
	[[nodiscard]] QString freeTier() const;
	[[nodiscard]] QString proUntil(const QString &date) const;
	[[nodiscard]] QString plan(const Plan &plan) const;
	[[nodiscard]] QString stars(int count) const;
	[[nodiscard]] QString proInfo() const;
	[[nodiscard]] QString linkFirst() const;
	[[nodiscard]] QString goLink() const;
	[[nodiscard]] QString notOnSale() const;
	[[nodiscard]] QString checking() const;
	[[nodiscard]] QString proActive() const;
	// Server error codes of /v1/link* and /v1/billing/*.
	[[nodiscard]] QString error(const QString &code) const;

	// Perks (29).
	[[nodiscard]] QString proOnlyBadge() const;
	[[nodiscard]] QString perkTitle(const QString &perk) const;
	[[nodiscard]] QString perkSubtitle(
		const QString &perk,
		const Numbers &numbers) const;
	[[nodiscard]] QString creditsTitle() const;
	// "剩余 N / 50，X 日重置"; without a reset time "剩余 N / 50".
	[[nodiscard]] QString creditsValue(
		int remaining,
		int total,
		int64_t resetsAtMs,
		const QTimeZone &tz) const;
	// "本月 AI 点数 剩余 N / 50，X 日重置" in one line.
	[[nodiscard]] QString creditsLine(
		int remaining,
		int total,
		int64_t resetsAtMs,
		const QTimeZone &tz) const;
	[[nodiscard]] QString upgradeTitle() const;
	[[nodiscard]] QString upgradeReason(Reason reason) const;
	[[nodiscard]] QString upgradeButton() const;

private:
	[[nodiscard]] QString t(const QString &zh, const QString &en) const {
		return _zh ? zh : en;
	}

	bool _zh = false;

};

} // namespace Luckgram::Pro
