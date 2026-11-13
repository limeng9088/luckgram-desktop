/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// D2: when Luckgram::OnLoggedOutCompletely() runs (what the last account
// leaves in memory or on the server: translation caches, the nearby card).
// Pure logic, covered by luckgram_logout_tests.cpp.
//
// Only when an account that was logged in logs out (Account::loggedOut(),
// user or server) and no other account keeps a session. Never at startup
// or while nobody ever logged in: settings made before the first login
// (a proxy needed to reach Telegram at all) must survive restarts.
//
// specs#129, as on mobile: the device keeps the Luckgram switches, the
// remote config, the device token and the settings level storage
// (proxies, subscriptions, Xray nodes: Luckgram::Storage::*Settings) even
// after the last logout. Account data (notes, translations, quick
// replies...) goes with its account in Storage::Account::reset().
namespace Luckgram::Logout {

enum class Event {
	Startup, // Domain::activateAfterStarting(), sessions restored or not.
	AccountLoggedOut, // Account::loggedOut(): a session really ended.
};

[[nodiscard]] bool ClearsDeviceData(Event event, int sessionsLeft);

} // namespace Luckgram::Logout
