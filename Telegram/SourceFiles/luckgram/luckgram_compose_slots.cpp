/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_compose_slots.h"

#include "base/unique_qptr.h"
#include "luckgram/transcribe/luckgram_dictation.h"
#include "luckgram/translate/translate_out.h"
#include "ui/rp_widget.h"
#include "luckgram/luckgram_settings.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

namespace Luckgram::Compose {
namespace {

using Factory = Slot(*)(const SlotContext &context);

// Nearest to the send button first: [D6 translate][D7 ✦][send].
[[nodiscard]] std::vector<Factory> Factories() {
	return {
		// D7: ✦ AI.
		Luckgram::Translate::CreateSendSlot, // D6: send translation.
		Luckgram::Transcribe::CreateDictationSlot, // D6: voice input.
	};
}

} // namespace

struct RightSlots::Entry {
	base::unique_qptr<Ui::RpWidget> widget;
	Fn<void(not_null<Ui::DropdownMenu*>)> plus;
	bool shown = false;
};

bool CompactEnabled() {
	return Settings::IsEnabled(QString::fromLatin1(kCompactFeature));
}

rpl::producer<bool> CompactValue() {
	return Settings::EnabledValue(QString::fromLatin1(kCompactFeature));
}

void SetupPlusIcon(not_null<Ui::IconButton*> attach) {
	if (attach->property("luckgramPlus").toBool()) {
		return; // Once per button.
	}
	attach->setProperty("luckgramPlus", true);
	CompactValue() | rpl::on_next([=](bool compact) {
		if (compact) {
			attach->setIconOverride(
				&st::historyLuckgramPlusIcon,
				&st::historyLuckgramPlusIconOver);
		} else {
			attach->setIconOverride(nullptr, nullptr);
		}
	}, attach->lifetime());
}

void RightSlots::fillPlusMenu(not_null<Ui::DropdownMenu*> menu) {
	if (!CompactEnabled()) {
		return;
	}
	auto separated = false;
	for (const auto &entry : _entries) {
		if (!entry->plus) {
			continue;
		} else if (!separated && !menu->actions().empty()) {
			menu->addSeparator();
			separated = true;
		}
		entry->plus(menu);
	}
}

struct SendMenuItems {
	uint64 id = 0;
	Fn<void(not_null<Ui::PopupMenu*>)> fill;
};

[[nodiscard]] std::map<QWidget*, std::vector<SendMenuItems>> &SendMenus() {
	static const auto result
		= new std::map<QWidget*, std::vector<SendMenuItems>>();
	return *result;
}

void AddSendMenuItems(
		not_null<QWidget*> send,
		Fn<void(not_null<Ui::PopupMenu*>)> fill,
		rpl::lifetime &lifetime) {
	static auto lastId = uint64();
	const auto id = ++lastId;
	const auto key = send.get();
	SendMenus()[key].push_back({ .id = id, .fill = std::move(fill) });
	lifetime.add([=] {
		auto &menus = SendMenus();
		const auto i = menus.find(key);
		if (i == end(menus)) {
			return;
		}
		auto &list = i->second;
		list.erase(
			ranges::remove(list, id, &SendMenuItems::id),
			end(list));
		if (list.empty()) {
			menus.erase(i);
		}
	});
}

void FillSendMenu(QWidget *send, not_null<Ui::PopupMenu*> menu) {
	const auto i = SendMenus().find(send);
	if (i == end(SendMenus())) {
		return;
	}
	const auto copy = i->second; // A fill may add or remove items.
	for (const auto &items : copy) {
		items.fill(menu);
	}
}

RightSlots::RightSlots(
		SlotContext context,
		not_null<Ui::RpWidget*> send,
		Fn<void()> relayout) {
	context.send = send;
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
		entry->plus = std::move(slot.plus);
		const auto raw = entry.get();
		_entries.push_back(std::move(entry));

		rpl::combine(
			std::move(slot.wanted),
			send->shownValue(),
			CompactValue()
		) | rpl::map([hasPlus = bool(raw->plus)](
				bool wanted,
				bool sendShown,
				bool compact) {
			// specs#94: in the 「+」 menu instead while the bar is compact.
			return wanted && sendShown && !(compact && hasPlus);
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
