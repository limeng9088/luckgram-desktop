/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// D4 / M2: what the in-app update does differently per platform
// (luckgram/platform/{win,mac,other}/luckgram_update_*.cpp).
namespace Luckgram::Update::Platform {

// "windows" / "macos": GET /v1/version?platform=...
[[nodiscard]] QString Name();
// ".exe" (installer) / ".zip" (Luckgram.app).
[[nodiscard]] QString PackageSuffix();
// False: only tell about the new version and open the download page
// (Windows portable, a Mac app in a read-only place, other systems).
[[nodiscard]] bool CanInstall();
// Checks a downloaded package before it is offered for installing (macOS:
// unpacks it and checks the bundle id). Called off the main thread.
[[nodiscard]] bool Prepare(const QString &package);
// Starts the installation of the checked package; the app quits right
// after true. The installer / helper starts Luckgram again.
[[nodiscard]] bool Install(const QString &package);

} // namespace Luckgram::Update::Platform
