/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "settings/settings_common_session.h"

// D8: Luckgram 功能 → 双重空间 / 隐藏聊天 (same rows as the mobile pages
// LuckgramDoubleBottomActivity / LuckgramHiddenChatsActivity).
namespace Settings {

[[nodiscard]] Type LuckgramDoubleBottomId();
[[nodiscard]] Type LuckgramHiddenChatsId();

// Registry hooks of double_bottom (mobile confirmEnable / confirmDisable):
// on needs the local passcode and a second passcode (else the setup page
// opens and the switch stays off), off needs the main passcode.
void ConfirmDoubleBottomEnable(
	not_null<Window::SessionController*> controller,
	Fn<void(bool)> done);
void ConfirmDoubleBottomDisable(
	not_null<Window::SessionController*> controller,
	Fn<void(bool)> done);

} // namespace Settings
