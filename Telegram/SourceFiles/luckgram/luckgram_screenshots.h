/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Luckgram {

// CI screenshots (D1-branding). Only when the LUCKGRAM_SCREENSHOTS
// environment variable names a folder: renders the welcome page, tray menu,
// about box and settings of a fresh (logged out) client in English, switches
// to Simplified Chinese, renders them again into that folder and quits.
// Rendering uses QWidget::grab(), so it works without an interactive desktop
// (the self-hosted runner service).
void RunScreenshotsIfRequested();

} // namespace Luckgram
