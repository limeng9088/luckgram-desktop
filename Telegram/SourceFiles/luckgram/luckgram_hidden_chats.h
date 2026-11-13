/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "ui/widgets/menu/menu_add_action_callback.h"

class History;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// D8 (mobile 12 隐藏聊天): chats hidden from the chat list - not deleted, not
// left. Kept per account (by user id) on this device only, never synced.
// A hidden chat is in no list or folder (History::shouldBeInChatList()),
// so it also leaves the unread counters; search skips it unless asked
// (「在隐藏的聊天中搜索」); its messages don't notify unless the user chose so.
//
// Desktop differences from mobile (texts changed accordingly):
// - 左滑 / 长按「隐藏」 → right-click a chat → 「隐藏」;
// - 下拉列表打开隐藏的聊天 → 「隐藏的聊天」 in the ☰ main menu (shown while
//   the account has hidden chats) and Luckgram 功能 → 隐藏聊天 → 打开;
// - 左滑「取消隐藏」 → right-click a chat in the hidden list → 「取消隐藏」.
namespace Luckgram::HiddenChats {

inline constexpr auto kFeatureKey = "hidden_chats";

[[nodiscard]] bool Enabled();
[[nodiscard]] bool IsHidden(not_null<const History*> history);
[[nodiscard]] bool HasHidden(not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<bool> HasHiddenValue(
	not_null<Main::Session*> session);
// Loaded hidden chats of the account, newest first.
[[nodiscard]] std::vector<not_null<History*>> List(
	not_null<Main::Session*> session);
// Hidden chats whose name matches the dialogs search query.
[[nodiscard]] std::vector<not_null<History*>> Search(
	not_null<Main::Session*> session,
	const QString &query);

void SetHidden(not_null<History*> history, bool hidden);

[[nodiscard]] bool LockEnabled();
void SetLockEnabled(bool enabled);
[[nodiscard]] bool NotifyEnabled();
void SetNotifyEnabled(bool enabled);

// Window::Notifications: drop the notification of a hidden chat.
[[nodiscard]] bool SuppressNotification(not_null<const History*> history);

// The switch changed (registry onToggled): every hidden chat goes back to
// / leaves the lists.
void OnToggled(bool enabled);

// ---- UI

// 「隐藏」 in the chat list context menu (Window::Filler).
void AddContextMenuAction(
	not_null<Window::SessionController*> controller,
	not_null<History*> history,
	const Ui::Menu::MenuCallback &addAction);
// 「隐藏的聊天」 in the ☰ main menu, while there are hidden chats.
void AddMainMenuEntry(
	not_null<Ui::VerticalLayout*> menu,
	not_null<Window::SessionController*> controller);
// Opens the hidden chats (after the main passcode when 「进入时需要验证」 is
// on). A non-empty query shows only the chats matching it.
void Open(
	not_null<Window::SessionController*> controller,
	const QString &query = QString());

[[nodiscard]] QString SearchHiddenText(int count);

// Dialogs search: the 「在隐藏的聊天中搜索」 row under the results, or
// nullptr when no hidden chat of the account matches the query.
[[nodiscard]] object_ptr<Ui::RpWidget> CreateSearchFooter(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	const QString &query);

} // namespace Luckgram::HiddenChats
