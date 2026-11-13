/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// D5 UI: welcome page extras, the Luckgram privacy policy dialog, the
// onboarding pages after login, the version line and the login code help.
// Rules and texts are in luckgram_onboarding_logic.h. Upstream files call
// into here with one line each.

namespace Ui {
class RpWidget;
class LinkButton;
class VerticalLayout;
class Show;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Luckgram {

// Welcome page (intro start step): the privacy policy link at the bottom,
// and the privacy dialog opens by itself until the user accepted it.
void SetupWelcomePage(not_null<Ui::RpWidget*> step);

// Get Started: true when the policy is accepted; otherwise shows the
// privacy dialog and calls `accepted` after Continue.
[[nodiscard]] bool ConfirmPrivacyBeforeStart(Fn<void()> accepted);

// After login, once per run when the main window is shown: "privacy policy
// updated" if the remote policy version went up, then the onboarding pages
// of the features the desktop already has.
void OnMainShown(not_null<Window::SessionController*> controller);

// 28: "Luckgram 7.4.2 (17) · 19e5f52".
[[nodiscard]] QString CurrentVersionLine();
// Copies the version line and shows "已复制".
void CopyVersionLine(std::shared_ptr<Ui::Show> show);

// Version line at the bottom of a settings page: click copies, right click
// opens the Luckgram API debug box in dev builds.
void AddVersionRow(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller);

// Login code page: "收不到验证码？" next to (or under) the "send code via
// SMS" link while the code was sent to the Telegram app.
void SetupCodeHelp(
	not_null<Ui::RpWidget*> step,
	not_null<Ui::LinkButton*> noTelegramCode);

} // namespace Luckgram
