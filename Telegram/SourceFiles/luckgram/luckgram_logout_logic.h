/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// D2: when Luckgram::OnLoggedOutCompletely() runs (Luckgram switches,
// remote config and the device token go). Pure logic, covered by
// luckgram_logout_tests.cpp.
//
// Only when an account that was logged in logs out (Account::loggedOut(),
// user or server) and no other account keeps a session. Never at startup
// or while nobody ever logged in: settings made before the first login
// (a proxy needed to reach Telegram at all) must survive restarts.
//
// Proxies, subscriptions and Xray nodes are settings level storage
// (Luckgram::Storage::*Settings, tdata/luckgram_*) and stay in any case:
// they belong to the device, as on mobile. Account data (notes,
// translations, quick replies...) goes with its account in
// Storage::Account::reset().
namespace Luckgram::Logout {

enum class Event {
	Startup, // Domain::activateAfterStarting(), sessions restored or not.
	AccountLoggedOut, // Account::loggedOut(): a session really ended.
};

[[nodiscard]] bool ClearsDeviceData(Event event, int sessionsLeft);

} // namespace Luckgram::Logout
