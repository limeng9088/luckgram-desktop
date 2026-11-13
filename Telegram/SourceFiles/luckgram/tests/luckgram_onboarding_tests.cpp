/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/luckgram_onboarding_logic.h"

// Cases ported from mobile WelcomeLogicTest, plus the desktop D5 rules
// (steps after login, pages of missing features skipped, version line).

namespace {

using namespace Luckgram::Onboarding;

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] std::function<bool(const QString&)> Has(QStringList keys) {
	return [=](const QString &key) { return keys.contains(key); };
}

} // namespace

LUCKGRAM_TEST(OnboardingPrivacyAskedUntilAccepted) {
	LG_CHECK(ShouldAskPrivacy(false));
	LG_CHECK(!ShouldAskPrivacy(true));
}

LUCKGRAM_TEST(OnboardingPolicyUpdateAsksLoggedInUsersOnce) {
	LG_CHECK(!ShouldPromptUpdate(true, 0, 0));
	LG_CHECK(ShouldPromptUpdate(true, 2, 1));
	LG_CHECK(ShouldPromptUpdate(true, 1, 0));
	LG_CHECK(!ShouldPromptUpdate(true, 2, 2));
	LG_CHECK(!ShouldPromptUpdate(true, 1, 2));
	LG_CHECK(!ShouldPromptUpdate(false, 2, 1));
	LG_CHECK(Texts(true).privacyUpdatedTitle().contains(U("已更新")));
}

LUCKGRAM_TEST(OnboardingPrivacyUrlByLanguageUnlessConfigured) {
	LG_EQUAL(PrivacyUrl(QString(), true), U("https://luckgram.cc/privacy-policy/"));
	LG_EQUAL(PrivacyUrl(U(""), false), U("https://luckgram.cc/en/privacy-policy/"));
	LG_EQUAL(PrivacyUrl(U("http://luckgram.cc/x"), false), U("https://luckgram.cc/en/privacy-policy/"));
	LG_EQUAL(PrivacyUrl(U(" https://luckgram.cc/legal/privacy "), true), U("https://luckgram.cc/legal/privacy"));
}

LUCKGRAM_TEST(OnboardingProStepAlwaysLastAndSubscribeOnlyWithBilling) {
	const auto all = AllSteps();
	LG_EQUAL(int(all.size()), 5);
	LG_CHECK(all.front() == Step::AI);
	LG_CHECK(all[1] == Step::Agents);
	LG_CHECK(all[2] == Step::Summary);
	LG_CHECK(all[3] == Step::Discover);
	LG_CHECK(all.back() == Step::Pro);
	LG_CHECK(CanSubscribe(Step::Pro, true));
	LG_CHECK(!CanSubscribe(Step::Pro, false));
	LG_CHECK(!CanSubscribe(Step::AI, true));
	LG_EQUAL(Texts(true).comingSoon(), U("即将推出"));
	LG_EQUAL(Texts(false).comingSoon(), U("Coming soon"));
}

LUCKGRAM_TEST(OnboardingProgressCountsShownSteps) {
	LG_EQUAL(Progress(0, 4), U("1/4"));
	LG_EQUAL(Progress(4, 5), U("5/5"));
	LG_CHECK(ProgressFraction(0, 4) > 0.2499 && ProgressFraction(0, 4) < 0.2501);
	LG_CHECK(ProgressFraction(3, 4) > 0.9999);
	LG_CHECK(ProgressFraction(0, 0) > 0.9999);
}

LUCKGRAM_TEST(OnboardingStepsWriteTheirFeatureSwitches) {
	LG_EQUAL(Toggles(Step::AI), (QStringList{ U("translate_out"), U("quick_replies"), U("ai_reply"), U("ai_writing") }));
	LG_EQUAL(Toggles(Step::Agents), QStringList{ U("ai_agents") });
	LG_CHECK(Toggles(Step::Summary).isEmpty());
	LG_CHECK(Toggles(Step::Discover).isEmpty());
	LG_CHECK(Toggles(Step::Pro).isEmpty());
	LG_CHECK(HasSecondary(Step::AI));
	LG_CHECK(HasSecondary(Step::Agents));
	LG_CHECK(HasSecondary(Step::Pro));
	LG_CHECK(!HasSecondary(Step::Summary));
	LG_CHECK(!HasSecondary(Step::Discover));
}

LUCKGRAM_TEST(OnboardingDesktopSkipsStepsOfMissingFeatures) {
	LG_CHECK(StepsToShow(Has({}), false, {}).empty());
	LG_CHECK(StepsToShow(nullptr, false, {}).empty());

	const auto discover = StepsToShow(Has({ U("square") }), false, {});
	LG_EQUAL(int(discover.size()), 1);
	LG_CHECK(discover.front() == Step::Discover);

	const auto ai = StepsToShow(Has({ U("ai_writing"), U("nearby") }), true, {});
	LG_EQUAL(int(ai.size()), 3);
	LG_CHECK(ai[0] == Step::AI);
	LG_CHECK(ai[1] == Step::Discover);
	LG_CHECK(ai[2] == Step::Pro);

	const auto pro = StepsToShow(Has({}), true, {});
	LG_EQUAL(int(pro.size()), 1);
	LG_CHECK(pro.front() == Step::Pro);
}

LUCKGRAM_TEST(OnboardingSeenStepsAreNotShownAgainButNewOnesAre) {
	const auto first = StepsToShow(Has({ U("nearby") }), false, {});
	const auto seen = MarkSeen({}, first);
	LG_EQUAL(seen, QStringList{ U("discover") });
	LG_CHECK(StepsToShow(Has({ U("nearby") }), false, seen).empty());

	const auto later = StepsToShow(Has({ U("nearby"), U("ai_agents") }), false, seen);
	LG_EQUAL(int(later.size()), 1);
	LG_CHECK(later.front() == Step::Agents);

	LG_EQUAL(MarkSeen(seen, later), (QStringList{ U("discover"), U("agents") }));
	LG_EQUAL(MarkSeen(seen, first), seen);
}

LUCKGRAM_TEST(OnboardingTextsInBothLanguages) {
	const auto zh = Texts(true);
	const auto en = Texts(false);
	LG_EQUAL(zh.getStarted(), U("开始使用"));
	LG_EQUAL(en.getStarted(), U("Get Started"));
	LG_EQUAL(zh.welcomeTitle(), U("欢迎使用 Luckgram"));
	LG_EQUAL(en.welcomeTitle(), U("Welcome to Luckgram"));
	LG_EQUAL(en.welcomeTo(), U("Welcome to"));
	LG_CHECK(zh.intro().startsWith(U("基于 Telegram 的开源客户端：")));
	LG_EQUAL(en.privacyTitle(), U("Luckgram Privacy Policy"));
	LG_EQUAL(en.privacyText().split(U("\n\n")).size(), 3);
	LG_EQUAL(zh.privacyText().split(U("\n\n")).size(), 3);
	LG_CHECK(zh.privacyText().contains(U("非官方")));
	LG_CHECK(en.privacyText().contains(U("By clicking Continue")));
	LG_CHECK(!en.privacyText().contains(U("tapping")));
	LG_EQUAL(zh.secondary(Step::AI), U("暂不开启"));
	LG_CHECK(en.secondary(Step::Summary).isEmpty());
	LG_CHECK(en.question(Step::Discover).isEmpty());
	LG_EQUAL(en.subscribe(500, 30), U("Subscribe · 500 ⭐ / month"));
	LG_EQUAL(zh.subscribe(500, 30), U("订阅 · 500 ⭐ / 月"));
	LG_EQUAL(zh.price(500, 30), U("500 ⭐ / 月"));
	LG_EQUAL(en.price(3000, 365), U("3000 ⭐ / year"));
	LG_EQUAL(zh.price(100, 7), U("100 ⭐ / 7 天"));
	LG_EQUAL(en.price(100, 7), U("100 ⭐ / 7 days"));
	LG_EQUAL(en.price(100, 0), U("100 ⭐"));
	LG_EQUAL(zh.price(0, 30), U(""));
	LG_EQUAL(zh.subscribe(0, 30), U("订阅"));
	for (const auto step : AllSteps()) {
		LG_CHECK(!zh.title(step).isEmpty());
		LG_CHECK(!en.body(step).isEmpty());
		LG_CHECK(!zh.primary(step).isEmpty());
	}
	LG_EQUAL(int(en.proFeatures().size()), 3);
}

LUCKGRAM_TEST(OnboardingCodeHelpUsesDesktopWords) {
	const auto zh = Texts(true);
	const auto en = Texts(false);
	LG_EQUAL(zh.codeTitle(), U("验证码已发到你的 Telegram"));
	LG_EQUAL(zh.codeSteps().size(), 3);
	LG_EQUAL(en.codeSteps().size(), 3);
	LG_EQUAL(zh.codeOk(), U("知道了"));
	LG_EQUAL(en.codeHelpButton(), U("How can I receive the code?"));
	const auto help = zh.codeHelpTexts(U(" 通过短信发送验证码 "));
	LG_EQUAL(help.size(), 3);
	LG_CHECK(help[1].contains(U("「通过短信发送验证码」")));
	LG_CHECK(help[2].startsWith(U("如果一直连不上网络，先点击")));
	const auto helpEn = en.codeHelpTexts(U("Send code via SMS"));
	LG_CHECK(helpEn[1].contains(U("click \"Send code via SMS\"")));
	for (const auto &text : helpEn) {
		LG_CHECK(!text.contains(U("tap")));
	}
}

LUCKGRAM_TEST(OnboardingVersionLineFormat) {
	LG_EQUAL(VersionLine(U("7.4.2"), 17, U("19e5f52d0c")), U("Luckgram 7.4.2 (17) · 19e5f52"));
	LG_EQUAL(VersionLine(U("7.4.2"), 0, QString()), U("Luckgram 7.4.2 (0) · unknown"));
	LG_EQUAL(VersionLine(U("7.4.2"), 3, U("  abc  ")), U("Luckgram 7.4.2 (3) · abc"));
	LG_EQUAL(Texts(true).copied(), U("已复制"));
	LG_EQUAL(Texts(false).copied(), U("Copied"));
}
