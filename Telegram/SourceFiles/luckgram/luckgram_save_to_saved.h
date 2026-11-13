/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

class ClickHandler;
class HistoryItem;
using ClickHandlerPtr = std::shared_ptr<ClickHandler>;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {
struct ContextMenuRequest;
class ListWidget;
} // namespace HistoryView

namespace Luckgram::SaveToSaved {

// Spec 15: "收藏" forwards messages to Saved Messages like a normal
// forward (the original sender is kept). Same rules and wording as mobile.
inline constexpr auto kFeature = "save_to_saved";

// Switch on, the messages are real, forwardable (no protected content)
// and not already in Saved Messages.
[[nodiscard]] bool CanSave(
	not_null<Window::SessionController*> controller,
	const MessageIdsList &ids);

// Forwards `ids` to Saved Messages oldest first and shows
// "已保存到收藏夹 · 查看".
void Save(
	not_null<Window::SessionController*> controller,
	MessageIdsList ids,
	Fn<void()> done = nullptr);

// Adds "收藏" when CanSave(), `done` runs after sending (clear selection).
void AddMenuAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	MessageIdsList ids,
	Fn<void()> done = nullptr);

// HistoryView::FillContextMenu (replies, topics, scheduled, ...).
void AddListMenuAction(
	not_null<Ui::PopupMenu*> menu,
	const HistoryView::ContextMenuRequest &request,
	not_null<HistoryView::ListWidget*> list);

// 15.3: 「收藏」 below the fast share button of a channel post
// (HistoryView::Message right action), same rules as the menu item.
[[nodiscard]] bool ShowFastSave(not_null<HistoryItem*> item);
[[nodiscard]] ClickHandlerPtr FastSaveLink(not_null<HistoryItem*> item);

} // namespace Luckgram::SaveToSaved
