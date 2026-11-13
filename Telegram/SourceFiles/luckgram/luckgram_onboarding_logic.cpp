/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_onboarding_logic.h"

#include <algorithm>

namespace Luckgram::Onboarding {
namespace {

[[nodiscard]] QString Utf8(const char *text) {
	return QString::fromUtf8(text);
}

} // namespace

bool ShouldAskPrivacy(bool accepted) {
	return !accepted;
}

bool ShouldPromptUpdate(
		bool loggedIn,
		qint64 remoteVersion,
		qint64 acceptedVersion) {
	return loggedIn && (remoteVersion > 0) && (remoteVersion > acceptedVersion);
}

QString PrivacyUrl(const QString &configured, bool zh) {
	const auto trimmed = configured.trimmed();
	if (trimmed.startsWith(QStringLiteral("https://"))) {
		return trimmed;
	}
	return Utf8(zh ? kDefaultPrivacyUrlZh : kDefaultPrivacyUrlEn);
}

std::vector<Step> AllSteps() {
	return { Step::AI, Step::Agents, Step::Summary, Step::Discover, Step::Pro };
}

QString StepId(Step step) {
	switch (step) {
	case Step::AI: return QStringLiteral("ai");
	case Step::Agents: return QStringLiteral("agents");
	case Step::Summary: return QStringLiteral("summary");
	case Step::Discover: return QStringLiteral("discover");
	case Step::Pro: return QStringLiteral("pro");
	}
	return QString();
}

QStringList StepFeatures(Step step) {
	switch (step) {
	case Step::AI:
		return {
			QStringLiteral("translate_out"),
			QStringLiteral("quick_replies"),
			QStringLiteral("ai_reply"),
			QStringLiteral("ai_writing"),
		};
	case Step::Agents: return { QStringLiteral("ai_agents") };
	case Step::Summary: return { QStringLiteral("ai_summary") };
	case Step::Discover:
		return { QStringLiteral("nearby"), QStringLiteral("square") };
	case Step::Pro: return {};
	}
	return {};
}

std::vector<Step> StepsToShow(
		const std::function<bool(const QString&)> &featureAvailable,
		bool proAvailable,
		const QStringList &seen) {
	auto result = std::vector<Step>();
	for (const auto step : AllSteps()) {
		if (seen.contains(StepId(step))) {
			continue;
		}
		const auto features = StepFeatures(step);
		const auto available = (step == Step::Pro)
			? proAvailable
			: std::any_of(features.begin(), features.end(), [&](
					const QString &key) {
				return featureAvailable && featureAvailable(key);
			});
		if (available) {
			result.push_back(step);
		}
	}
	return result;
}

QStringList MarkSeen(const QStringList &seen, const std::vector<Step> &steps) {
	auto result = seen;
	for (const auto step : steps) {
		const auto id = StepId(step);
		if (!result.contains(id)) {
			result.push_back(id);
		}
	}
	return result;
}

QStringList Toggles(Step step) {
	switch (step) {
	case Step::AI: return StepFeatures(Step::AI);
	case Step::Agents: return { QStringLiteral("ai_agents") };
	default: return {};
	}
}

bool HasSecondary(Step step) {
	return (step == Step::AI) || (step == Step::Agents) || (step == Step::Pro);
}

bool CanSubscribe(Step step, bool billingEnabled) {
	return (step == Step::Pro) && billingEnabled;
}

QString Progress(int index, int total) {
	return QStringLiteral("%1/%2").arg(index + 1).arg(total);
}

double ProgressFraction(int index, int total) {
	return (total <= 0) ? 1. : std::min(1., (index + 1) / double(total));
}

QString VersionLine(
		const QString &version,
		int buildNumber,
		const QString &commit) {
	const auto trimmed = commit.trimmed();
	const auto shortCommit = trimmed.isEmpty()
		? QStringLiteral("unknown")
		: trimmed.left(7);
	return QStringLiteral("Luckgram %1 (%2) · %3")
		.arg(version)
		.arg(buildNumber)
		.arg(shortCommit);
}

QString Texts::t(const char *zh, const char *en) const {
	return Utf8(_zh ? zh : en);
}

QString Texts::welcomeTo() const {
	return t("欢迎使用", "Welcome to");
}

QString Texts::appName() const {
	return QStringLiteral("Luckgram");
}

QString Texts::welcomeTitle() const {
	return t("欢迎使用 Luckgram", "Welcome to Luckgram");
}

QString Texts::intro() const {
	return t(
		"基于 Telegram 的开源客户端：AI 帮你写、帮你回、帮你总结，还有隐藏聊天、附近的人和广场。",
		"An open-source Telegram client with AI that writes, replies and sums up for you, plus hidden chats, people nearby and the Square.");
}

QString Texts::getStarted() const {
	return t("开始使用", "Get Started");
}

QString Texts::privacyTitle() const {
	return t("Luckgram 隐私政策", "Luckgram Privacy Policy");
}

QString Texts::privacyText() const {
	return t(
		"Luckgram 是非官方的 Telegram 客户端，你在 Luckgram 里用到的很多服务实际由 Telegram 提供。\n\n"
		"使用前请先阅读我们的隐私政策。\n\n"
		"点击「继续」即表示你已阅读并同意 Luckgram 隐私政策。",
		"Luckgram is an unofficial Telegram client, and many of the services you use in Luckgram are actually provided by Telegram.\n\n"
		"Please read our Privacy Policy before using the app.\n\n"
		"By clicking Continue, you confirm that you have read and agree to the Luckgram Privacy Policy.");
}

QString Texts::privacyUpdatedTitle() const {
	return t("隐私政策已更新", "Privacy Policy Updated");
}

QString Texts::privacyUpdatedText() const {
	return t(
		"我们更新了 Luckgram 隐私政策，请花一点时间阅读新版本。\n\n"
		"点击「继续」即表示你已阅读并同意更新后的 Luckgram 隐私政策。",
		"We've updated the Luckgram Privacy Policy. Please take a moment to read the new version.\n\n"
		"By clicking Continue, you confirm that you have read and agree to the updated Luckgram Privacy Policy.");
}

QString Texts::privacyPolicyButton() const {
	return t("隐私政策", "PRIVACY POLICY");
}

QString Texts::continueButton() const {
	return t("继续", "CONTINUE");
}

QString Texts::privacyPolicyLink() const {
	return privacyTitle();
}

QString Texts::title(Step step) const {
	switch (step) {
	case Step::AI: return t("AI 让聊天更轻松", "Chat Smarter with AI");
	case Step::Agents: return t("AI 智能体", "AI Agents");
	case Step::Summary: return t("AI 聊天总结", "AI Chat Summary");
	case Step::Discover: return t("附近的人和广场", "Nearby & Square");
	case Step::Pro: return QStringLiteral("Luckgram Pro");
	}
	return QString();
}

QString Texts::body(Step step) const {
	switch (step) {
	case Step::AI:
		return t(
			"Luckgram 把 AI 放进了输入框：发送前一键翻译成对方的语言，常用回复存成模板一点就发，还能让 AI 帮你回复、润色和改写。",
			"Luckgram puts AI right in the message field: translate into the other person's language before sending, insert saved replies with a click, and let AI draft, polish or rewrite your messages.");
	case Step::Agents:
		return t(
			"写作、翻译、出行、学习……每个智能体专注一件事，在 AI 助手里随时找它们帮忙。",
			"Writing, translating, travel, studying: each agent is focused on one job and ready in the AI assistant whenever you need it.");
	case Step::Summary:
		return t(
			"群里消息太多看不完？让 AI 把最近的聊天整理成要点，还能接着追问细节。",
			"Too many messages to catch up on? AI turns the recent chat into key points, and you can ask follow-up questions.");
	case Step::Discover:
		return t(
			"找到附近同样开启的 Luckgram 用户，在广场上发图文短帖、点赞和评论。随时可以在「Luckgram 功能」里关闭。",
			"Find Luckgram users nearby who turned it on, and post, like and comment in the Square. You can turn both off anytime in Luckgram Features.");
	case Step::Pro:
		return t(
			"更多的 AI 额度，让每个 AI 功能都用得尽兴。",
			"More AI every day, for every AI feature.");
	}
	return QString();
}

QString Texts::question(Step step) const {
	switch (step) {
	case Step::AI: return t("要开启这些 AI 功能吗？", "Turn on these AI features?");
	case Step::Agents: return t("要开启 AI 智能体吗？", "Turn on AI Agents?");
	default: return QString();
	}
}

QString Texts::primary(Step step) const {
	switch (step) {
	case Step::AI: return t("开启 AI 功能", "Turn On AI Features");
	case Step::Agents: return t("开启 AI 智能体", "Turn On AI Agents");
	case Step::Pro: return t("订阅", "Subscribe");
	default: return t("继续", "Continue");
	}
}

QString Texts::secondary(Step step) const {
	switch (step) {
	case Step::AI: return t("暂不开启", "Continue without AI");
	case Step::Agents: return t("暂不开启", "Continue without AI Agents");
	case Step::Pro: return t("暂不订阅", "Continue without Pro");
	default: return QString();
	}
}

QString Texts::comingSoon() const {
	return t("即将推出", "Coming soon");
}

QString Texts::price(int stars, int days) const {
	if (stars <= 0) {
		return QString();
	}
	auto per = QString();
	if (days == 30 || days == 31) {
		per = t("月", "month");
	} else if (days == 365 || days == 366) {
		per = t("年", "year");
	} else if (days > 0) {
		per = _zh
			? QStringLiteral("%1 ").arg(days) + Utf8("天")
			: QStringLiteral("%1 days").arg(days);
	} else {
		return QString::number(stars) + Utf8(" ⭐");
	}
	return QString::number(stars) + Utf8(" ⭐ / ") + per;
}

QString Texts::subscribe(int stars, int days) const {
	if (stars <= 0) {
		return primary(Step::Pro);
	}
	return t("订阅 · ", "Subscribe · ") + price(stars, days);
}

std::vector<std::pair<QString, QString>> Texts::proFeatures() const {
	return {
		{
			t("更多 AI 额度", "More AI every day"),
			t(
				"AI 对话、写作、总结、智能体、生图和语音转文字，每天可用次数更多。",
				"Higher daily limits for AI chat, writing, summaries, agents, images and transcription."),
		},
		{
			t("所有设备共享", "Shared across devices"),
			t(
				"绑定同一个 Telegram 账号的设备共享 Pro。",
				"Every device linked to your Telegram account shares Pro."),
		},
		{
			t("用 Telegram Stars 付款", "Paid with Telegram Stars"),
			t(
				"不需要绑卡，已是 Pro 时再买会从到期时间往后延。",
				"No card needed. Buying again while Pro extends from the current end date."),
		},
	};
}

QString Texts::proFootnote() const {
	return t(
		"登录 Telegram 后完成付款。",
		"You'll complete the payment after logging in to Telegram.");
}

QString Texts::codeTitle() const {
	return t("验证码已发到你的 Telegram", "Your code was sent to Telegram");
}

QStringList Texts::codeSteps() const {
	return {
		t(
			"在另一台已登录的设备上打开 Telegram（或 Luckgram）。",
			"Open Telegram (or Luckgram) on another device where you're logged in."),
		t(
			"在聊天列表里找到官方账号「Telegram」发来的消息（带蓝色认证标）。",
			"In the chat list, find the message from the official \"Telegram\" account (with the blue check)."),
		t(
			"把消息里的 5–6 位验证码填到上面。",
			"Enter the 5–6 digit code from that message above."),
	};
}

QString Texts::codeOk() const {
	return t("知道了", "OK");
}

QString Texts::codeHelpButton() const {
	return t("收不到验证码？", "How can I receive the code?");
}

QString Texts::codeHelpTitle() const {
	return t("怎样收到验证码", "How to receive the code");
}

QStringList Texts::codeHelpTexts(const QString &noCodeLink) const {
	const auto link = noCodeLink.trimmed();
	return {
		t(
			"验证码会优先发到你已登录的 Telegram App 里，而不是短信。请在手机、电脑或平板上已登录的 Telegram（或 Luckgram）中查看官方「Telegram」账号的消息。",
			"The code goes to the Telegram app where you're already logged in, not by SMS. Check the message from the official \"Telegram\" account in Telegram (or Luckgram) on your phone, computer or tablet."),
		(_zh
			? Utf8("如果所有设备都已退出登录，点击验证码页下方的「%1」，可以改为通过短信接收（是否可用由 Telegram 决定）。")
			: Utf8("If you're logged out everywhere, click \"%1\" below the code field to get it by SMS instead (if Telegram allows it).")
		).arg(link),
		t(
			"如果一直连不上网络，先点击右上角的「设置」，在「连接类型」里配置代理，再重新获取验证码。",
			"If you can't connect, click Settings at the top right and set up a proxy in Connection type first, then request the code again."),
	};
}

QString Texts::copied() const {
	return t("已复制", "Copied");
}

} // namespace Luckgram::Onboarding
