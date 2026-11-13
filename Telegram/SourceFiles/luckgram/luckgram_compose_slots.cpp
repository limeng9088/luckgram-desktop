/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_compose_slots.h"

#include "base/unique_qptr.h"
#include "ui/rp_widget.h"

namespace Luckgram::Compose {
namespace {

using Factory = Slot(*)(const SlotContext &context);

// Nearest to the send button first: [D6 translate][D7 ✦][send].
[[nodiscard]] std::vector<Factory> Factories() {
	return {
		// D7: ✦ AI.
		// D6: send translation.
	};
}

} // namespace

struct RightSlots::Entry {
	base::unique_qptr<Ui::RpWidget> widget;
	bool shown = false;
};

RightSlots::RightSlots(
		SlotContext context,
		not_null<Ui::RpWidget*> send,
		Fn<void()> relayout) {
	// The owner lays out right after construction, no callback needed yet.
	const auto constructing = std::make_shared<bool>(true);
	for (const auto factory : Factories()) {
		auto slot = factory(context);
		if (!slot.widget) {
			continue;
		}
		const auto widget = slot.widget.release();
		widget->setParent(context.parent);
		widget->hide();
		auto entry = std::make_unique<Entry>();
		entry->widget.reset(widget);
		const auto raw = entry.get();
		_entries.push_back(std::move(entry));

		rpl::combine(
			std::move(slot.wanted),
			send->shownValue()
		) | rpl::map([](bool wanted, bool sendShown) {
			return wanted && sendShown;
		}) | rpl::distinct_until_changed(
		) | rpl::on_next([=](bool shown) {
			raw->shown = shown;
			raw->widget->setVisible(shown);
			if (relayout && !*constructing) {
				relayout();
			}
		}, _lifetime);
	}
	*constructing = false;
}

RightSlots::~RightSlots() = default;

int RightSlots::width() const {
	auto result = 0;
	for (const auto &entry : _entries) {
		if (entry->shown) {
			result += entry->widget->width();
		}
	}
	return result;
}

int RightSlots::moveToRight(int right, int top) {
	for (const auto &entry : _entries) {
		if (entry->shown) {
			entry->widget->moveToRight(right, top);
			right += entry->widget->width();
		}
	}
	return right;
}

void RightSlots::raise() {
	for (const auto &entry : _entries) {
		entry->widget->raise();
	}
}

} // namespace Luckgram::Compose
