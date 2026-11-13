/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class Show;
} // namespace Ui

// D10 (mobile account/SilentLink, S6): the silent device <-> Telegram account
// link shared by Square and People Nearby. Nothing about "linking an
// account" is shown: POST /v1/link/start -> the app starts the bot from the
// current account with the nonce -> GET /v1/link?nonce= until the server has
// it -> the bot chat is deleted (again a little later, and whatever the bot
// sends there for a minute). Callers show their own short notice.
namespace Luckgram::SilentLink {

enum class State {
	Unknown,
	Linked,
	NotLinked,
};

// Last known state for this session's user.
[[nodiscard]] State Cached(not_null<Main::Session*> session);
// Forget it (unlink, logout, a not_linked answer).
void ClearCache();
// GET /v1/link; `done` runs either way.
void Refresh(not_null<Main::Session*> session, Fn<void()> done);
// Calls `done(true)` once linked, linking in the background first if
// needed; concurrent calls share one attempt.
void Ensure(
	not_null<Main::Session*> session,
	std::shared_ptr<Ui::Show> show,
	Fn<void(bool)> done);

} // namespace Luckgram::SilentLink
