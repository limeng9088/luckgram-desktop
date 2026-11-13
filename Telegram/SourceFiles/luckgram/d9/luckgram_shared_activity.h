/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

class UserData;

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// D9 / mobile 16: a user's recent messages in your groups in common.
// Telegram API only (messages.getCommonChats + messages.search with
// from_id), only groups you are in yourself. Rules live in
// luckgram_shared_activity_logic.h.
namespace Luckgram::SharedActivity {

inline constexpr auto kFeatureKey = "shared_activity";

[[nodiscard]] bool Enabled();
[[nodiscard]] rpl::producer<bool> EnabledValue();

// Whether the user's profile gets 「动态」: a regular user (not you, not a
// bot, not deleted); the switch is checked separately (EnabledValue).
[[nodiscard]] bool AppliesTo(not_null<UserData*> user);

[[nodiscard]] QString Title(); // 动态

// The 「动态」 list: 「最近消息 · N 个共同群组」, then group, time and
// preview of each message, newest first; click opens the message in its
// group. 「刷新」 reloads (results are reused for 10 minutes otherwise).
void ActivityBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user);

} // namespace Luckgram::SharedActivity
