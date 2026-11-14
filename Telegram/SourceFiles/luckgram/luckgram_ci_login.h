/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// CI-only login to Telegram's *test* servers, for screenshots of the logged
// in UI. The real implementation is compiled only with the CMake option
// LUCKGRAM_CI_BUILD=ON (CI screenshot build on the self-hosted runner,
// never the release build; CI checks the released exe for the marker
// string); otherwise these are stubs.
namespace Luckgram {

[[nodiscard]] bool CiTestLoginAvailable();

// Adds a test-environment account, signs in (or signs up) with a random
// public test number on the account's main test DC X (99966 X YYYY, the
// code is X repeated, as long as auth.sendCode says; 22222 on DC 2) and
// calls `done(true)` once
// the session exists. Production accounts and data are not touched.
//
// Telegram answers PHONE_CODE_INVALID to these codes now (test accounts
// are switched off); then, or after any other failure, the session is made
// on this computer only, without authorization: CiOfflineSession().
void CiTestLogin(Fn<void(bool)> done);

// True once CiTestLogin made its session without the server. Main::Account
// then does not log out on the 401 every request gets. Always false in a
// release build.
[[nodiscard]] bool CiOfflineSession();

} // namespace Luckgram
