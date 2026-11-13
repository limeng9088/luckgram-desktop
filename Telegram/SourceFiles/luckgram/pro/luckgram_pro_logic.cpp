/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/pro/luckgram_pro_logic.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QLocale>
#include <QtCore/QRegularExpression>

#include <algorithm>

namespace Luckgram::Pro {
namespace {

[[nodiscard]] QString L(const char *latin1) {
	return QString::fromLatin1(latin1);
}

[[nodiscard]] QString U(const char *utf8) {
	return QString::fromUtf8(utf8);
}

// A string member, empty when missing or null.
[[nodiscard]] QString StringOf(const QJsonObject &json, const char *key) {
	const auto value = json.value(L(key));
	return value.isString() ? value.toString() : QString();
}

[[nodiscard]] int IntOf(const QJsonObject &json, const char *key) {
	const auto value = json.value(L(key));
	return value.isDouble() ? int(value.toDouble()) : 0;
}

[[nodiscard]] int64_t Int64Of(const QJsonObject &json, const char *key) {
	const auto value = json.value(L(key));
	return value.isDouble() ? int64_t(value.toDouble()) : 0;
}

[[nodiscard]] QStringList PerksOf(const QJsonArray &array) {
	auto result = QStringList();
	for (const auto &value : array) {
		const auto perk = value.toString().trimmed();
		if (!perk.isEmpty() && !result.contains(perk)) {
			result.push_back(perk);
		}
	}
	return result;
}

[[nodiscard]] QString N(int value) {
	return QString::number(value);
}

} // namespace

bool State::isPro(int64_t nowMs) const {
	return active && (expiresAtMs == 0 || nowMs < expiresAtMs);
}

bool State::hasPerk(const QString &perk) const {
	return !perk.isEmpty() && perks.contains(perk);
}

int State::credits(int64_t nowMs) const {
	return isPro(nowMs) ? creditsRemaining : 0;
}

int State::accountsMaxNow(int64_t nowMs) const {
	if (accountsMax > 0) {
		return accountsMax;
	}
	return isPro(nowMs) ? 0 : kAccountsMaxFree;
}

int State::summaryMaxNow(int64_t nowMs) const {
	if (summaryMaxMessages > 0) {
		return summaryMaxMessages;
	}
	return isPro(nowMs) ? kSummaryMaxPro : kSummaryMaxFree;
}

bool State::sameAs(const State &other) const {
	return (active == other.active)
		&& (expiresAtMs == other.expiresAtMs)
		&& (creditsRemaining == other.creditsRemaining)
		&& (creditsTotal == other.creditsTotal)
		&& (creditsResetsAtMs == other.creditsResetsAtMs)
		&& (perks == other.perks)
		&& (accountsMax == other.accountsMax)
		&& (summaryMaxMessages == other.summaryMaxMessages);
}

State ParseMe(const QJsonObject &me, int64_t nowMs) {
	auto result = State();
	const auto pro = me.value(L("pro"));
	if (pro.isObject()) {
		const auto object = pro.toObject();
		result.active = object.value(L("active")).toBool(false);
		result.expiresAtMs = ParseTime(StringOf(object, "expires_at"));
	} else {
		result.active = (StringOf(me, "tier") == L("pro"));
		result.expiresAtMs = ParseTime(StringOf(me, "pro_expires_at"));
	}
	const auto credits = me.value(L("credits")).toObject();
	result.creditsRemaining = std::max(0, IntOf(credits, "remaining"));
	result.creditsTotal = std::max(0, IntOf(credits, "total"));
	result.creditsResetsAtMs = ParseTime(StringOf(credits, "resets_at"));
	result.perks = PerksOf(me.value(L("perks")).toArray());
	const auto limits = me.value(L("limits")).toObject();
	result.accountsMax = std::max(0, IntOf(limits, "accounts_max"));
	result.summaryMaxMessages = std::max(
		0,
		IntOf(limits, "summary_max_messages"));
	result.fetchedAtMs = nowMs;
	return result;
}

QByteArray ToCache(const State &state) {
	auto json = QJsonObject();
	json.insert(L("active"), state.active);
	json.insert(L("expires_at_ms"), double(state.expiresAtMs));
	json.insert(L("credits_remaining"), state.creditsRemaining);
	json.insert(L("credits_total"), state.creditsTotal);
	json.insert(L("credits_resets_at_ms"), double(state.creditsResetsAtMs));
	json.insert(L("perks"), QJsonArray::fromStringList(state.perks));
	json.insert(L("accounts_max"), state.accountsMax);
	json.insert(L("summary_max_messages"), state.summaryMaxMessages);
	json.insert(L("fetched_at_ms"), double(state.fetchedAtMs));
	return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

State FromCache(const QByteArray &cache) {
	if (cache.isEmpty()) {
		return State();
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(cache, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return State();
	}
	const auto json = document.object();
	auto result = State();
	result.active = json.value(L("active")).toBool(false);
	result.expiresAtMs = Int64Of(json, "expires_at_ms");
	result.creditsRemaining = std::max(0, IntOf(json, "credits_remaining"));
	result.creditsTotal = std::max(0, IntOf(json, "credits_total"));
	result.creditsResetsAtMs = Int64Of(json, "credits_resets_at_ms");
	result.perks = PerksOf(json.value(L("perks")).toArray());
	result.accountsMax = std::max(0, IntOf(json, "accounts_max"));
	result.summaryMaxMessages = std::max(
		0,
		IntOf(json, "summary_max_messages"));
	result.fetchedAtMs = Int64Of(json, "fetched_at_ms");
	return result;
}

int64_t ParseTime(const QString &rfc3339) {
	auto text = rfc3339.trimmed();
	if (text.isEmpty()) {
		return 0;
	}
	// Fractional seconds of any length (the server may send nanoseconds)
	// are dropped; Qt::ISODate then handles "Z" and "+08:00".
	static const auto fraction = QRegularExpression(
		QString::fromLatin1("\\.\\d+"));
	text.remove(fraction);
	// "2026-11-13T09:30:00" without a zone is not RFC 3339: refuse it
	// rather than read it in the local time zone.
	static const auto zone = QRegularExpression(
		QString::fromLatin1("(Z|z|[+-]\\d\\d:?\\d\\d)$"));
	if (!zone.match(text).hasMatch()) {
		return 0;
	}
	const auto parsed = QDateTime::fromString(text, Qt::ISODate);
	return parsed.isValid() ? parsed.toMSecsSinceEpoch() : 0;
}

QString Date(int64_t ms, const QTimeZone &tz) {
	if (ms <= 0) {
		return QString();
	}
	return QDateTime::fromMSecsSinceEpoch(ms, tz).toString(L("yyyy-MM-dd"));
}

LinkStatus ParseLink(const QJsonObject &json) {
	auto result = LinkStatus();
	result.linked = json.value(L("linked")).toBool(false);
	result.devices = IntOf(json, "devices");
	result.maxDevices = IntOf(json, "max_devices");
	result.request = StringOf(json, "request");
	return result;
}

LinkStart ParseStart(const QJsonObject &json) {
	auto result = LinkStart();
	result.nonce = StringOf(json, "nonce");
	result.url = StringOf(json, "url");
	result.expiresAtMs = ParseTime(StringOf(json, "expires_at"));
	return result;
}

bool ShouldPoll(const QString &request, int64_t nowMs, int64_t expiresAtMs) {
	return (request.isEmpty() || request == L("pending"))
		&& (nowMs < expiresAtMs);
}

QString BotUsername(const QString &link) {
	const auto text = link.trimmed();
	const auto index = text.indexOf(L("t.me/"));
	if (index < 0) {
		return QString();
	}
	auto rest = text.mid(index + 5);
	for (auto i = 0; i != rest.size(); ++i) {
		const auto c = rest[i];
		if (c == QChar('?') || c == QChar('/') || c == QChar('#')) {
			rest = rest.left(i);
			break;
		}
	}
	static const auto valid = QRegularExpression(
		QString::fromLatin1("^[A-Za-z0-9_]{4,32}$"));
	return valid.match(rest).hasMatch() ? rest : QString();
}

bool IsTelegramLink(const QString &url) {
	const auto prefix = L("https://t.me/");
	return url.startsWith(prefix) && (url.size() > prefix.size());
}

Billing ParseBilling(const QJsonObject &json) {
	auto result = Billing();
	result.enabled = json.value(L("enabled")).toBool(false);
	result.linked = json.value(L("linked")).toBool(false);
	result.pro = (StringOf(json, "tier") == L("pro"));
	result.proExpiresAt = StringOf(json, "pro_expires_at");
	for (const auto &value : json.value(L("plans")).toArray()) {
		const auto plan = value.toObject();
		const auto stars = IntOf(plan, "stars");
		if (stars <= 0) {
			continue;
		}
		result.plans.push_back({
			.id = StringOf(plan, "id"),
			.days = IntOf(plan, "days"),
			.stars = stars,
		});
	}
	return result;
}

QStringList PerkOrder() {
	return {
		L(kPerkAgentMarket),
		L(kPerkAccounts),
		L(kPerkAi),
		L(kPerkSummary),
		L(kPerkTranslate),
		L(kPerkCommunity),
		L(kPerkNoAds),
	};
}

QStringList LivePerks(
		const QStringList &fromMe,
		const QJsonValue &fromConfig) {
	auto result = QStringList();
	const auto add = [&](QString perk) {
		perk = perk.trimmed();
		perk.remove(QChar('"'));
		if (!perk.isEmpty() && !result.contains(perk)) {
			result.push_back(perk);
		}
	};
	if (!fromMe.isEmpty()) {
		for (const auto &perk : fromMe) {
			add(perk);
		}
		return result;
	}
	if (fromConfig.isArray()) {
		for (const auto &value : fromConfig.toArray()) {
			add(value.toString());
		}
	} else if (fromConfig.isString()) {
		auto text = fromConfig.toString();
		text.remove(QChar('[')).remove(QChar(']'));
		for (const auto &perk : text.split(QChar(','))) {
			add(perk);
		}
	}
	return result;
}

std::vector<Row> Rows(
		const QStringList &perks,
		const Numbers &numbers,
		bool zh) {
	auto result = std::vector<Row>();
	const auto texts = Texts(zh);
	for (const auto &perk : PerkOrder()) {
		if (perks.contains(perk)) {
			result.push_back({
				.perk = perk,
				.proOnly = IsProOnlyRow(perk),
				.title = texts.perkTitle(perk),
				.subtitle = texts.perkSubtitle(perk, numbers),
			});
		}
	}
	return result;
}

bool IsProOnlyRow(const QString &perk) {
	return (perk == L(kPerkAgentMarket)) || (perk == L(kPerkAccounts));
}

bool SummaryNeedsPro(int count, bool pro) {
	return !pro && (count > kSummaryMaxFree);
}

int SummaryUnreadCap(int unread, bool pro, int proMax) {
	const auto cap = pro ? std::max(kSummaryMaxFree, proMax) : kSummaryMaxFree;
	return std::max(0, std::min(unread, cap));
}

// --- Texts (mobile AccountLogic.Texts / ProPerks.Texts).

QString Texts::accountTitle() const {
	return t(U("账号"), L("Account"));
}

QString Texts::telegramAccount() const {
	return t(U("Telegram 账号"), L("Telegram account"));
}

QString Texts::linked() const {
	return t(U("已绑定"), L("Linked"));
}

QString Texts::notLinked() const {
	return t(U("未绑定"), L("Not linked"));
}

QString Texts::devices(int count, int max) const {
	return t(
		U("已绑定设备：") + N(count) + L(" / ") + N(max),
		L("Linked devices: ") + N(count) + L(" / ") + N(max));
}

QString Texts::link() const {
	return t(U("绑定 Telegram 账号"), L("Link Telegram account"));
}

QString Texts::unlink() const {
	return t(U("解绑"), L("Unlink"));
}

QString Texts::waiting() const {
	return t(
		U("等待 Bot 确认…（在 Bot 里点「开始」）"),
		U("Waiting for the bot… (click Start in the bot)"));
}

QString Texts::linkInfo() const {
	return t(
		U("绑定后，同一个 Telegram 账号下的设备共享 AI 额度和 Luckgram Pro。服务器只保存 Telegram 用户 ID，不保存手机号、用户名或聊天内容。"),
		U("Devices linked to the same Telegram account share AI limits and Luckgram Pro. The server keeps only your Telegram user ID — no phone number, username or chats."));
}

QString Texts::unlinkConfirmTitle() const {
	return t(U("解绑这台设备？"), L("Unlink this device?"));
}

QString Texts::unlinkConfirmText() const {
	return t(
		U("解绑后这台设备不再共享 Luckgram Pro。今天已用的 AI 额度不会因此恢复。"),
		L("This device will no longer share Luckgram Pro. AI usage already counted today is not reset."));
}

QString Texts::linkedNow() const {
	return t(U("已绑定 Telegram 账号"), L("Telegram account linked"));
}

QString Texts::unlinkedNow() const {
	return t(U("已解绑"), L("Unlinked"));
}

QString Texts::linkResult(const QString &request, int max) const {
	if (request == L("expired")) {
		return t(
			U("绑定链接已过期，请重新点「绑定 Telegram 账号」。"),
			U("The link expired. Click “Link Telegram account” again."));
	} else if (request == L("used")) {
		return t(
			U("这个绑定链接已被使用，请重新点「绑定 Telegram 账号」。"),
			U("This link was already used. Click “Link Telegram account” again."));
	} else if (request == L("limit_reached")) {
		return t(
			U("这个 Telegram 账号已绑定 ") + N(max) + U(" 台设备，请先在 Bot 里用 /devices 解绑一台。"),
			L("This Telegram account already has ") + N(max) + L(" linked devices. Unlink one in the bot with /devices first."));
	}
	return t(
		U("没有完成绑定，请重试。"),
		L("Linking was not completed. Please try again."));
}

QString Texts::proTitle() const {
	return L("Luckgram Pro");
}

QString Texts::freeTier() const {
	return t(U("免费版"), L("Free"));
}

QString Texts::proUntil(const QString &date) const {
	return t(U("Pro，到期 ") + date, L("Pro until ") + date);
}

QString Texts::plan(const Plan &plan) const {
	return t(N(plan.days) + U(" 天"), N(plan.days) + L(" days"));
}

QString Texts::stars(int count) const {
	return N(count) + U(" ⭐");
}

QString Texts::proInfo() const {
	return t(
		U("用 Telegram Stars 付款，所有绑定这个 Telegram 账号的设备共享。已是 Pro 时再买会从当前到期时间往后延。"),
		L("Paid with Telegram Stars and shared by every device linked to this Telegram account. Buying again while Pro extends from the current end date."));
}

QString Texts::linkFirst() const {
	return t(
		U("先绑定 Telegram 账号才能购买"),
		L("Link your Telegram account first"));
}

QString Texts::goLink() const {
	return t(U("去绑定"), L("Link"));
}

QString Texts::notOnSale() const {
	return t(
		U("Luckgram Pro 暂不开放购买"),
		L("Luckgram Pro is not on sale right now"));
}

QString Texts::checking() const {
	return t(U("正在确认付款…"), U("Checking the payment…"));
}

QString Texts::proActive() const {
	return t(U("Luckgram Pro 已生效"), L("Luckgram Pro is active"));
}

QString Texts::error(const QString &code) const {
	if (code == L("link_unavailable") || code == L("billing_unavailable")) {
		return t(
			U("服务暂不可用，请稍后再试"),
			L("Not available right now. Please try again later"));
	} else if (code == L("already_linked")) {
		return t(
			U("这台设备已经绑定了"),
			L("This device is already linked"));
	} else if (code == L("not_linked")) {
		return linkFirst();
	} else if (code == L("billing_disabled")) {
		return notOnSale();
	} else if (code == L("rate_limited")) {
		return t(
			U("操作太频繁，请稍后再试"),
			L("Too many attempts. Please try again later"));
	} else if (code == L("network_error")) {
		return t(
			U("网络不可用，请检查网络后重试"),
			L("No connection. Check your network and try again"));
	}
	return t(
		U("出错了，请稍后再试"),
		L("Something went wrong. Please try again later"));
}

QString Texts::proOnlyBadge() const {
	return t(U("Pro 专享"), L("Pro only"));
}

QString Texts::perkTitle(const QString &perk) const {
	if (perk == L(kPerkAgentMarket)) {
		return t(U("AI 智能体市场"), L("AI Agent Market"));
	} else if (perk == L(kPerkAccounts)) {
		return t(U("更多账号与云备份"), L("More Accounts & Cloud Backup"));
	} else if (perk == L(kPerkAi)) {
		return t(U("AI 畅用"), L("More AI"));
	} else if (perk == L(kPerkSummary)) {
		return t(U("AI 聊天分析"), L("AI Chat Analysis"));
	} else if (perk == L(kPerkTranslate)) {
		return t(U("内置翻译"), L("Built-in Translation"));
	} else if (perk == L(kPerkCommunity)) {
		return t(U("Pro 社群"), L("Pro Community"));
	} else if (perk == L(kPerkNoAds)) {
		return t(U("去除赞助消息"), L("No Sponsored Messages"));
	}
	return perk;
}

QString Texts::perkSubtitle(
		const QString &perk,
		const Numbers &n) const {
	if (perk == L(kPerkAgentMarket)) {
		return t(
			U("发现并使用更多 AI 智能体。"),
			L("Discover and use more AI agents."));
	} else if (perk == L(kPerkAccounts)) {
		return (n.accountsMaxFree > 0)
			? t(
				U("同时登录的账号数量不限（免费 ") + N(n.accountsMaxFree) + U(" 个）；账号备份加密存到云端。"),
				L("Log in to unlimited accounts at once (free: ") + N(n.accountsMaxFree) + L("); account backups are encrypted in the cloud."))
			: t(
				U("同时登录的账号数量不限；账号备份加密存到云端。"),
				L("Log in to unlimited accounts at once; account backups are encrypted in the cloud."));
	} else if (perk == L(kPerkAi)) {
		const auto daily = (n.chatPro > 0)
			? t(
				U("AI 对话每天 ") + N(n.chatPro) + U(" 次") + ((n.chatFree > 0) ? (U("（免费 ") + N(n.chatFree) + U(" 次）")) : QString()),
				L("AI chat ") + N(n.chatPro) + L(" times a day") + ((n.chatFree > 0) ? (L(" (free: ") + N(n.chatFree) + L(")")) : QString()))
			: t(U("AI 对话每天可用次数更多"), L("More AI chats every day"));
		return daily + t(
			U("，每月另送 ") + N(n.monthlyCredits) + U(" 个 AI 点数，用于高级模型。"),
			L(", plus ") + N(n.monthlyCredits) + L(" AI credits a month for the premium model."));
	} else if (perk == L(kPerkSummary)) {
		return t(
			U("总结最多 ") + N(n.summaryMaxPro) + U(" 条消息并可追问（免费 ") + N(kSummaryMaxFree) + U(" 条）。"),
			L("Summarize up to ") + N(n.summaryMaxPro) + L(" messages and ask follow-up questions (free: ") + N(kSummaryMaxFree) + L(")."));
	} else if (perk == L(kPerkTranslate)) {
		return (n.translatePro > 0)
			? t(
				U("每天翻译 ") + N(n.translatePro) + U(" 次") + ((n.translateFree > 0) ? (U("（免费 ") + N(n.translateFree) + U(" 次）")) : QString()) + U("。"),
				L("Translate ") + N(n.translatePro) + L(" times a day") + ((n.translateFree > 0) ? (L(" (free: ") + N(n.translateFree) + L(")")) : QString()) + L("."))
			: t(U("每天可翻译的次数更多。"), L("More translations every day."));
	} else if (perk == L(kPerkCommunity)) {
		return t(
			U("加入 Luckgram Pro 用户私密群。"),
			L("Join the private group for Luckgram Pro users."));
	} else if (perk == L(kPerkNoAds)) {
		return t(
			U("频道里不显示 Telegram 赞助消息。"),
			L("Telegram sponsored messages are hidden in channels."));
	}
	return QString();
}

QString Texts::creditsTitle() const {
	return t(U("本月 AI 点数"), L("AI credits this month"));
}

QString Texts::creditsValue(
		int remaining,
		int total,
		int64_t resetsAtMs,
		const QTimeZone &tz) const {
	const auto left = std::max(0, remaining);
	const auto text = t(
		U("剩余 ") + N(left) + L(" / ") + N(total),
		N(left) + L(" / ") + N(total) + L(" left"));
	if (resetsAtMs <= 0) {
		return text;
	}
	const auto at = QDateTime::fromMSecsSinceEpoch(resetsAtMs, tz);
	const auto day = at.date().day();
	const auto en = QLocale(QLocale::English, QLocale::UnitedStates).toString(
		at.date(),
		L("MMM d"));
	return text + t(
		U("，") + N(day) + U(" 日重置"),
		L(", resets ") + en);
}

QString Texts::creditsLine(
		int remaining,
		int total,
		int64_t resetsAtMs,
		const QTimeZone &tz) const {
	return creditsTitle()
		+ t(U(" "), L(": "))
		+ creditsValue(remaining, total, resetsAtMs, tz);
}

QString Texts::upgradeTitle() const {
	return t(U("升级 Luckgram Pro"), L("Upgrade to Luckgram Pro"));
}

QString Texts::upgradeReason(Reason reason) const {
	switch (reason) {
	case Reason::Quota:
		return t(
			U("今天的 AI 额度已用完。开通 Pro 每天可以用更多次。"),
			U("You've used up today's AI quota. Pro gives you more every day."));
	case Reason::SummaryRange:
		return t(
			U("总结 ") + N(kSummaryMaxFree) + U(" 条以上的消息是 Pro 专享。"),
			L("Summarizing more than ") + N(kSummaryMaxFree) + L(" messages is for Pro members."));
	case Reason::FollowUp:
		return t(
			U("对总结结果追问是 Pro 专享。"),
			L("Asking follow-up questions about a summary is for Pro members."));
	case Reason::PremiumModel:
		return t(
			U("高级模型是 Pro 专享，每月送 AI 点数。"),
			L("The premium model is for Pro members, with AI credits every month."));
	case Reason::ProOnly:
		break;
	}
	return t(
		U("这是 Pro 专享功能。"),
		L("This feature is for Pro members."));
}

QString Texts::upgradeButton() const {
	return t(U("查看 Pro"), L("See Pro"));
}

} // namespace Luckgram::Pro
