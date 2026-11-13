/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Ui {
class RpWidget;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// D7 (mobile 18 需求 1, switch ai_assistant_row): the 「Luckgram AI」 row at
// the top of the chat list, like Android's pinned row in DialogsActivity
// (purple icon, title). Desktop difference: a fixed button row above the
// list, not a chat of the list model, so it can't be dragged or unpinned;
// the switch hides it. Clicking opens the most recent conversation.
namespace Luckgram::AI {

// Adds the row into the chat list's layout (Dialogs::Widget, one line).
// shown: the main list without search, folder or forum. Returns the row
// (its height counts in the list's minimum height).
not_null<Ui::RpWidget*> AddChatListRow(
	not_null<Ui::VerticalLayout*> list,
	not_null<Window::SessionController*> controller,
	rpl::producer<bool> shown);

} // namespace Luckgram::AI
