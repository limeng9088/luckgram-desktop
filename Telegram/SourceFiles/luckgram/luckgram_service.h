/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// D2: app-level Luckgram services. Upstream files call into here with one
// line each (core/application.cpp, main/main_domain.cpp).
namespace Luckgram {

// After the domain started: device registration and GET /v1/config now,
// then the config again every 6 hours (D2 §6-7).
void Start();

// Logged out of every account: Luckgram settings and the device token are
// removed (D2 acceptance: "退出全部账号时一并清除").
void OnLoggedOutCompletely();

// GET /v1/config now (debug page); done(true) when something changed.
void RefreshRemoteConfig(Fn<void(bool)> done = nullptr);

} // namespace Luckgram
