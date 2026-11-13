/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "base/object_ptr.h"

class History;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class RpWidget;
class InputField;
} // namespace Ui

namespace Luckgram::Compose {

// Luckgram buttons between the input field and the send button, shared by
// HistoryWidget and ComposeControls (D6 translate, D7 ✦ AI, ...):
//   [field] ... [slot N] ... [slot 1] [slot 0] [send]
// Each feature adds one factory line in luckgram_compose_slots.cpp.

struct SlotContext {
	not_null<Ui::RpWidget*> parent;
	not_null<Ui::InputField*> field;
	std::shared_ptr<ChatHelpers::Show> show;
	Fn<History*()> history; // The current chat, may be nullptr.
};

struct Slot {
	object_ptr<Ui::RpWidget> widget = { nullptr };
	// When the slot wants to be shown. The button is shown only while the
	// send button is shown too (not while recording, not when restricted).
	rpl::producer<bool> wanted = rpl::single(true);
};

class RightSlots final {
public:
	// `relayout` is called when a slot appears or disappears.
	RightSlots(
		SlotContext context,
		not_null<Ui::RpWidget*> send,
		Fn<void()> relayout);
	~RightSlots();

	// Total width of the shown slots.
	[[nodiscard]] int width() const;

	// Places the shown slots right-to-left starting at `right` (distance
	// from the parent's right edge), returns the new `right`.
	int moveToRight(int right, int top);

	void raise();

private:
	struct Entry;

	std::vector<std::unique_ptr<Entry>> _entries;
	rpl::lifetime _lifetime;

};

} // namespace Luckgram::Compose
