/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Luckgram {

// CI screenshots. Compiled only into the CI build (CMake LUCKGRAM_CI_BUILD,
// see luckgram_ci_login.h); a no-op in release builds. Only when the
// LUCKGRAM_SCREENSHOTS environment variable names a folder and the client has
// no logged in account and no passcode (a fresh CI working folder): renders the welcome page, tray menu,
// about box and settings of a fresh (logged out) client in English, switches
// to Simplified Chinese, renders them again into that folder, then (only in
// the CI build, see luckgram_ci_login.h) signs in on Telegram's
// test servers, turns every Luckgram switch on (not double_bottom) and
// renders the main window and one screen per feature, named by desktop
// task (zh-D7-ai-assistant.png, ...; settings pages also as -full.png with
// the whole page), and quits.
// Rendering uses QWidget::grab(), so it works without an interactive desktop
// (the self-hosted runner service).
void RunScreenshotsIfRequested();

} // namespace Luckgram
