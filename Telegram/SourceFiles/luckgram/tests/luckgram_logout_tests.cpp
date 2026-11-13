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
