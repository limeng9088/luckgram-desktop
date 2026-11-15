/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QString>

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

// Why Main::Account::loggedOut() ran, for one log line (xs-master
// 2026-11-15: an account was gone after a restart and the log of that
// run was overwritten, so nobody could tell a logout in the UI from the
// server ending the session). The line has the account index, the user id
// and the reason only: no phone, name, auth key or server description.
enum class Reason {
	Unknown,
	User, // Log out in the UI (Settings, the window menu).
	ForgottenPasscode, // "Forgot passcode": every account logs out.
	AlreadyLoggedIn, // Intro: the account just authorized was already here.
	WrongSelf, // The server sent another user as self.
	ServerError, // 401 on the main DC (AUTH_KEY_UNREGISTERED, ...).
	ServerNotice, // updateServiceNotification with the force-logout flag.
};

struct Cause {
	Reason reason = Reason::Unknown;
	int code = 0; // ServerError only.
	QString type; // ServerError only, see ServerError().
};

// The error type is kept only as A-Z, 0-9 and _ (what Telegram sends),
// at most 64 characters.
[[nodiscard]] Cause ServerError(int code, const QString &type);
[[nodiscard]] QString Describe(const Cause &cause);
[[nodiscard]] QString LogLine(int index, quint64 userId, const Cause &cause);

} // namespace Luckgram::Logout
