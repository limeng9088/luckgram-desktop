/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <functional>
#include <utility>
#include <vector>

// D5: welcome page, privacy policy, onboarding steps, version line and the
// login code help. Pure logic and texts (QtCore only, unit tested in
// luckgram_tests); same rules and wording as mobile WelcomeLogic /
// CodeHelpCard / 28 version info, with desktop words ("点击", "click").
namespace Luckgram::Onboarding {

// Luckgram settings keys (same as mobile LuckgramSettings).
inline constexpr auto kPrivacyAcceptedKey = "privacy_policy_accepted";
inline constexpr auto kPrivacyAcceptedAtKey = "privacy_policy_accepted_at";
inline constexpr auto kPrivacyVersionKey = "privacy_policy_version";
// Desktop: the onboarding pages already gone through ("ai,agents,...").
// A page of a feature that ships later is shown once it is available.
inline constexpr auto kSeenStepsKey = "onboarding_seen_steps";

// Remote config values (GET /v1/config "values").
inline constexpr auto kRemotePrivacyUrl = "app.privacy_url";
inline constexpr auto kRemotePrivacyVersion = "app.privacy_version";
inline constexpr auto kRemoteBillingEnabled = "billing.enabled";

inline constexpr auto kDefaultPrivacyUrlZh
	= "https://luckgram.cc/privacy-policy/";
inline constexpr auto kDefaultPrivacyUrlEn
	= "https://luckgram.cc/en/privacy-policy/";

enum class Step {
	AI, // 01 translate before sending, 02 quick replies, 20/21 AI reply / writing.
	Agents, // 22 AI agents.
	Summary, // 19 chat summary.
	Discover, // 25 people nearby + 26 square.
	Pro, // S7 Luckgram Pro, always the last one.
};

// The privacy dialog opens by itself until the user clicked Continue once.
[[nodiscard]] bool ShouldAskPrivacy(bool accepted);

// A logged in user is asked again ("privacy policy updated") when the remote
// policy version is newer than the accepted one. Version 0 never asks.
[[nodiscard]] bool ShouldPromptUpdate(
	bool loggedIn,
	qint64 remoteVersion,
	qint64 acceptedVersion);

// The configured page if it is https, else the default in the language.
[[nodiscard]] QString PrivacyUrl(const QString &configured, bool zh);

// All steps in the mobile order; Pro is the last one.
[[nodiscard]] std::vector<Step> AllSteps();

// Stable id stored in kSeenStepsKey.
[[nodiscard]] QString StepId(Step step);

// Feature keys of the step: the page is shown when at least one of them is
// on the desktop (registered in luckgram_features). Pro has none.
[[nodiscard]] QStringList StepFeatures(Step step);

// Desktop D5: the steps to show after login, in order: available on the
// desktop (a feature key registered, Pro by proAvailable) and not seen yet.
[[nodiscard]] std::vector<Step> StepsToShow(
	const std::function<bool(const QString&)> &featureAvailable,
	bool proAvailable,
	const QStringList &seen);

// Seen list after going through `steps`, without duplicates.
[[nodiscard]] QStringList MarkSeen(
	const QStringList &seen,
	const std::vector<Step> &steps);

// Feature switches the step's two buttons write ("turn on" = true,
// "not now" = false).
[[nodiscard]] QStringList Toggles(Step step);

// Steps with a "not now" choice (steps with switches, and Pro).
[[nodiscard]] bool HasSecondary(Step step);

// The Pro step's Subscribe works only while billing is enabled; otherwise
// it shows "Coming soon", disabled.
[[nodiscard]] bool CanSubscribe(Step step, bool billingEnabled);

// "n/N" over the steps shown, index 0-based.
[[nodiscard]] QString Progress(int index, int total);
[[nodiscard]] double ProgressFraction(int index, int total);

// 28: "Luckgram 7.4.2 (17) · 19e5f52". Commit: 7 characters, "unknown" when
// not known. English and digits only, never translated.
[[nodiscard]] QString VersionLine(
	const QString &version,
	int buildNumber,
	const QString &commit);

class Texts final {
public:
	explicit Texts(bool zh) : _zh(zh) {
	}

	// Welcome page.
	[[nodiscard]] QString welcomeTo() const;
	[[nodiscard]] QString appName() const;
	[[nodiscard]] QString welcomeTitle() const; // "欢迎使用 Luckgram"
	[[nodiscard]] QString intro() const;
	[[nodiscard]] QString getStarted() const;

	// Privacy dialog.
	[[nodiscard]] QString privacyTitle() const;
	[[nodiscard]] QString privacyText() const;
	[[nodiscard]] QString privacyUpdatedTitle() const;
	[[nodiscard]] QString privacyUpdatedText() const;
	[[nodiscard]] QString privacyPolicyButton() const;
	[[nodiscard]] QString continueButton() const;
	[[nodiscard]] QString privacyPolicyLink() const; // Under Get Started.

	// Steps. question() and secondary() are empty when the step has none.
	[[nodiscard]] QString title(Step step) const;
	[[nodiscard]] QString body(Step step) const;
	[[nodiscard]] QString question(Step step) const;
	[[nodiscard]] QString primary(Step step) const;
	[[nodiscard]] QString secondary(Step step) const;
	[[nodiscard]] QString comingSoon() const;
	[[nodiscard]] QString price(int stars, int days) const;
	[[nodiscard]] QString subscribe(int stars, int days) const;
	[[nodiscard]] std::vector<std::pair<QString, QString>> proFeatures() const;
	[[nodiscard]] QString proFootnote() const;

	// Login code help (mobile CodeHelpCard). `noCodeLink` is the text of the
	// desktop link under the code field (lng_code_no_telegram).
	[[nodiscard]] QString codeTitle() const;
	[[nodiscard]] QStringList codeSteps() const;
	[[nodiscard]] QString codeOk() const;
	[[nodiscard]] QString codeHelpButton() const;
	[[nodiscard]] QString codeHelpTitle() const;
	[[nodiscard]] QStringList codeHelpTexts(const QString &noCodeLink) const;

	// Version line.
	[[nodiscard]] QString copied() const;

private:
	[[nodiscard]] QString t(const char *zh, const char *en) const;

	bool _zh = false;

};

} // namespace Luckgram::Onboarding
