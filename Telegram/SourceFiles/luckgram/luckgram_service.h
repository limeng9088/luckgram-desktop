/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// D2: app-level Luckgram services. Upstream files call into here with one
// line each (core/application.cpp, main/main_account.cpp).
namespace Luckgram {

// After the domain started: device registration and GET /v1/config now,
// then the config again every 6 hours (D2 §6-7).
void Start();

// The last logged in account logged out (Main::Account::loggedOut(), see
// luckgram_logout_logic.h). The Luckgram switches and the device token
// stay (specs#129, as on mobile).
void OnLoggedOutCompletely();

// GET /v1/config now (debug page); done(true) when something changed.
void RefreshRemoteConfig(Fn<void(bool)> done = nullptr);

} // namespace Luckgram
