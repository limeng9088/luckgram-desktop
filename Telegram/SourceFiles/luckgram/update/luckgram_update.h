/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Ui {
class Checkbox;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

// D4 / M2 (mobile S5 + specs#124): in-app update. GET /v1/version through
// the D2 client at start and every 30 minutes; "有新版本 7.4.2（23） · 更新" in
// the main menu and on top of Settings (no popup); download with progress,
// sha256 + size check, optional automatic download on unmetered networks,
// "重启并更新" installs and starts the new version; below `min` only update
// or quit.
namespace Luckgram::Update {

// Once, after the main window is shown (luckgram_onboarding OnMainShown).
void Start(not_null<Window::SessionController*> controller);

// Main menu row (shown while there is an update).
void AddMainMenuEntry(
	not_null<Ui::VerticalLayout*> menu,
	not_null<Window::SessionController*> controller);
// Settings main page, first row (shown while there is an update).
void AddSettingsButton(::Settings::Builder::SectionBuilder &builder);

// Settings → Advanced: "Wi-Fi 下自动下载更新", default on.
[[nodiscard]] rpl::producer<QString> AutoDownloadTitle();
[[nodiscard]] bool AutoDownload();
void BindAutoDownloadCheckbox(not_null<Ui::Checkbox*> checkbox);

// Settings → Advanced: "空闲时自动重启并更新", default off.
[[nodiscard]] rpl::producer<QString> AutoInstallTitle();
[[nodiscard]] bool AutoInstall();
void BindAutoInstallCheckbox(not_null<Ui::Checkbox*> checkbox);

} // namespace Luckgram::Update
