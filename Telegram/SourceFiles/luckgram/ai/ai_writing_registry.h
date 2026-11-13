/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "base/object_ptr.h"

class History;
struct TextWithTags;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
} // namespace Ui

// D7 (mobile 21 需求 6): extension points of the ✦ menu next to the message
// field, for 20 (AI reply) and 24 (AI post); same shape as Android
// AiWritingRegistry. Features add one line in RegisterWritingFeatures() and
// never change the input field code. Main thread only.
//
//   RegisterWritingItem({
//       .id = u"reply"_q,
//       .titleZh = "帮我回复",
//       .titleEn = "Help me reply",
//       .icon = &st::menuIconReply,
//       .order = 0, // smaller first; built-in effects use 100..140
//       .allowEmptyText = true, // also offered when the field is empty
//       .visible = [](not_null<WritingHost*> host) { ... },
//       .action = [](not_null<WritingHost*> host) { ... },
//   });
//
// Panel above the input field and writing to the field with undo:
//   host->showPanel(std::move(panel)); // replaces any other panel
//   host->setText(text, true);         // true: "撤销" restores the old text
//   host->send(text, false);           // like the Send button
//   host->hidePanel(raw);
namespace Luckgram::AI {

// What a menu item or panel can do with the message field of the open chat
// (history, topic or replies). Lives as long as that field.
class WritingHost {
public:
	virtual ~WritingHost() = default;

	[[nodiscard]] virtual not_null<Main::Session*> session() = 0;
	[[nodiscard]] virtual std::shared_ptr<ChatHelpers::Show> show() = 0;
	// The open chat (may be nullptr between chats).
	[[nodiscard]] virtual History *history() = 0;

	// The field's text with formatting.
	[[nodiscard]] virtual TextWithTags text() = 0;
	// Replaces the field's text; with undoable the previous text can be
	// restored with undo().
	virtual void setText(const TextWithTags &text, bool undoable) = 0;
	[[nodiscard]] virtual bool canUndo() = 0;
	virtual void undo() = 0;

	// The panel container above the input field: one panel at a time, the
	// container takes ownership and sizes it to its width; the panel sets
	// its own height. showPanel returns the shown panel.
	virtual Ui::RpWidget *showPanel(object_ptr<Ui::RpWidget> panel) = 0;
	// Hides (and destroys) panel if it is the panel shown.
	virtual void hidePanel(not_null<Ui::RpWidget*> panel) = 0;

	// True in a channel where the user can post.
	[[nodiscard]] virtual bool canPostInChannel() = 0;
	// Sends text to the chat the way the user would from the field (as the
	// channel when posting in one) and clears the field; with schedule,
	// Telegram's schedule picker opens first.
	virtual void send(const TextWithTags &text, bool schedule) = 0;
};

struct WritingItem {
	QString id;
	const char *titleZh = "";
	const char *titleEn = "";
	const style::icon *icon = nullptr;
	int order = 0;
	// Offer the item (and the ✦ button) even when the field is empty.
	bool allowEmptyText = false;
	Fn<bool(not_null<WritingHost*>)> visible; // nullptr = always.
	Fn<void(not_null<WritingHost*>)> action;
};

// Replaces an item with the same id.
void RegisterWritingItem(WritingItem item);
void UnregisterWritingItem(const QString &id);

// Items to show for this field, sorted by order.
[[nodiscard]] std::vector<WritingItem> VisibleWritingItems(
	not_null<WritingHost*> host,
	bool hasText);
// Whether ✦ shows for an empty field (some item allows it).
[[nodiscard]] bool HasWritingItemsForEmptyText(not_null<WritingHost*> host);

// Built-in effects (21) and the features' lines (20, 24); called once
// before the first use.
void RegisterWritingFeatures();

} // namespace Luckgram::AI
