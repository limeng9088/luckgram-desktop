/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/luckgram_logout_logic.h"

// D2: Luckgram device data is cleared only on a real last logout
// (luckgram-desktop#89, P1).

using namespace Luckgram::Logout;

LUCKGRAM_TEST(LogoutColdStartNotLoggedIn) {
	// Never logged in, or the app restarted before login (proxy set up
	// first): nothing is cleared, however often it starts.
	LG_CHECK(!ClearsDeviceData(Event::Startup, 0));
	LG_CHECK(!ClearsDeviceData(Event::Startup, 0));
}

LUCKGRAM_TEST(LogoutRestartLoggedIn) {
	LG_CHECK(!ClearsDeviceData(Event::Startup, 1));
	LG_CHECK(!ClearsDeviceData(Event::Startup, 3));
}

LUCKGRAM_TEST(LogoutLastAccount) {
	// Logged in, then the last account logs out: cleared once.
	LG_CHECK(ClearsDeviceData(Event::AccountLoggedOut, 0));
}

LUCKGRAM_TEST(LogoutOtherAccountsRemain) {
	LG_CHECK(!ClearsDeviceData(Event::AccountLoggedOut, 1));
	LG_CHECK(!ClearsDeviceData(Event::AccountLoggedOut, 2));
}

LUCKGRAM_TEST(LogoutRestartWithProxyAfterLastLogout) {
	// After the last logout the user sets a proxy again and restarts
	// before logging in: the next start doesn't clear it.
	LG_CHECK(ClearsDeviceData(Event::AccountLoggedOut, 0));
	LG_CHECK(!ClearsDeviceData(Event::Startup, 0));
}

// One log line per logout (xs-master 2026-11-15): index, user id and the
// reason, nothing else.
LUCKGRAM_TEST(LogoutReasonLine) {
	LG_EQUAL(
		LogLine(3, 6000000001ULL, { .reason = Reason::User }),
		QStringLiteral("Luckgram Account: logged out, account index 3, user 6000000001, reason: user"));
	LG_EQUAL(
		LogLine(-1, 0, {}),
		QStringLiteral("Luckgram Account: logged out, account index -1, user 0, reason: unknown"));
	LG_EQUAL(
		Describe(ServerError(401, QStringLiteral("AUTH_KEY_UNREGISTERED"))),
		QStringLiteral("server error 401 AUTH_KEY_UNREGISTERED"));
	LG_EQUAL(
		Describe(ServerError(401, QString())),
		QStringLiteral("server error 401 (no type)"));
	LG_EQUAL(Describe({ .reason = Reason::ServerNotice }), QStringLiteral("server notice"));
	LG_EQUAL(Describe({ .reason = Reason::WrongSelf }), QStringLiteral("wrong self user"));
	LG_EQUAL(
		Describe({ .reason = Reason::ForgottenPasscode }),
		QStringLiteral("forgotten passcode"));
	LG_EQUAL(
		Describe({ .reason = Reason::AlreadyLoggedIn }),
		QStringLiteral("already logged in"));
}

LUCKGRAM_TEST(LogoutReasonServerTypeCleaned) {
	// Only what Telegram error types are made of; anything else (text,
	// digits of a phone after a +, spaces) is dropped, and it is capped.
	LG_EQUAL(
		ServerError(401, QStringLiteral("SESSION_REVOKED: +123 abc")).type,
		QStringLiteral("SESSION_REVOKED123"));
	LG_EQUAL(
		ServerError(401, QString(100, QChar('A'))).type.size(),
		64);
	LG_EQUAL(ServerError(401, QStringLiteral("X")).code, 401);
	LG_CHECK(ServerError(401, QStringLiteral("X")).reason == Reason::ServerError);
}
