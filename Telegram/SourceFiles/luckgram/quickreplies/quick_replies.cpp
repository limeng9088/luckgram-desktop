/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/quickreplies/quick_replies.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/event_filter.h"
#include "base/timer.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_peer.h"
#include "data/data_chat_participant_status.h"
#include "history/history.h"
#include "history/history_item_helpers.h"
#include "luckgram/luckgram_double_bottom.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_storage.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/quickreplies/quick_replies_logic.h"
#include "main/main_session.h"
#include "settings/sections/settings_luckgram_quick_replies.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_common.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QPointer>

namespace Luckgram::QuickReplies {
namespace {

constexpr auto kStorageName = "quick_replies";
constexpr auto kSaveDelay = crl::time(1000);
constexpr auto kMenuTextLength = 60;

// Loaded once the account storage is usable (local key, not hidden in
// the second space); before that the examples are shown from memory and
// nothing is written.
struct Store {
	explicit Store(not_null<Main::Session*> session)
	: session(session)
	, saveTimer([=] { save(); }) {
	}

	void ensureLoaded() {
		if (loaded || !Storage::AccountAvailable(session)) {
			return;
		}
		loaded = true;
		data = Seeded(
			Parse(Storage::ReadAccount(
				session,
				QString::fromLatin1(kStorageName))),
			IsChinese());
		changes.fire({});
		saveTimer.callOnce(kSaveDelay); // Remember the examples are given.
	}
	void changed() {
		changes.fire({});
		if (loaded) {
			saveTimer.callOnce(kSaveDelay);
		}
	}
	void save() {
		if (loaded) {
			Storage::WriteAccount(
				session,
				QString::fromLatin1(kStorageName),
				Serialize(data));
		}
	}

	const not_null<Main::Session*> session;
	bool loaded = false;
	Data data = Seeded({}, IsChinese()); // Until loaded: memory only.
	rpl::event_stream<> changes;
	base::Timer saveTimer;
};

using StoreMap = std::map<not_null<Main::Session*>, std::unique_ptr<Store>>;

[[nodiscard]] StoreMap &Stores() {
	static const auto result = new StoreMap();
	return *result;
}

void SubscribeToSecondSpace() {
	static auto lifetime = (rpl::lifetime*)nullptr;
	if (lifetime) {
		return;
	}
	lifetime = new rpl::lifetime();
	DoubleBottom::ActiveChanges(
	) | rpl::on_next([](bool) {
		for (const auto &entry : Stores()) {
			entry.second->ensureLoaded();
		}
	}, *lifetime);
}

[[nodiscard]] Store &GetStore(not_null<Main::Session*> session) {
	auto &stores = Stores();
	auto i = stores.find(session);
	if (i == end(stores)) {
		SubscribeToSecondSpace();
		i = stores.emplace(session, std::make_unique<Store>(session)).first;
		session->lifetime().add([=] {
			auto &stores = Stores();
			const auto j = stores.find(session);
			if (j != end(stores)) {
				if (j->second->saveTimer.isActive()) {
					j->second->saveTimer.cancel();
					j->second->save();
				}
				stores.erase(j);
			}
		});
	}
	i->second->ensureLoaded();
	return *i->second;
}

[[nodiscard]] QString MenuText(const QString &text) {
	auto line = text.section(QChar('\n'), 0, 0);
	if (line.size() > kMenuTextLength || line.size() < text.size()) {
		line = line.left(kMenuTextLength) + QString::fromUtf8("…");
	}
	return line;
}

void Insert(QPointer<Ui::InputField> field, const QString &text) {
	if (!field) {
		return;
	}
	auto cursor = field->textCursor();
	cursor.insertText(text); // 02.2: into the field, not sent.
	field->setTextCursor(cursor);
	field->setFocus();
}

// Right-click: sent like the field would send it (send as, reply to,
// topic from upstream's prepareSendAction), after upstream's checks (slow
// mode, restrictions). Paid messages go through the field instead, where
// the price is confirmed.
void SendNow(
		const Compose::SlotContext &context,
		QPointer<Ui::InputField> field,
		const QString &text) {
	auto action = context.sendAction
		? context.sendAction()
		: std::optional<::Api::SendAction>();
	if (!action) {
		return;
	}
	const auto peer = action->history->peer;
	if (peer->starsPerMessageChecked() > 0) {
		Insert(field, text);
		return;
	}
	const auto textWithTags = TextWithTags{ text };
	const auto error = GetErrorForSending(peer, {
		.topicRootId = action->replyTo.topicRootId,
		.text = &textWithTags,
	});
	if (error) {
		::Data::ShowSendErrorToast(context.show, peer, error);
		return;
	}
	action->clearDraft = false; // The user's draft stays.
	auto message = ::Api::MessageToSend(*action);
	message.textWithTags = textWithTags;
	peer->session().api().sendMessage(std::move(message));
}

// Click: insert, right-click: send (mobile: tap / long press).
void AddItem(
		not_null<Ui::PopupMenu*> popup,
		const QString &text,
		Fn<void()> insert,
		Fn<void()> send,
		Fn<void()> close) {
	const auto menu = popup->menu();
	auto item = base::make_unique_q<Ui::Menu::Action>(
		menu,
		menu->st(),
		Ui::Menu::CreateAction(menu, MenuText(text), std::move(insert)),
		nullptr,
		nullptr);
	base::install_event_filter(item.get(), [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseButtonPress
			|| type == QEvent::MouseButtonRelease) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			if (mouse->button() == Qt::RightButton) {
				if (type == QEvent::MouseButtonRelease) {
					send();
					close();
				}
				return base::EventFilterResult::Cancel;
			}
		} else if (type == QEvent::ContextMenu) {
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});
	popup->addAction(std::move(item));
}

void ShowList(
		const Compose::SlotContext &context,
		not_null<QWidget*> parent,
		std::shared_ptr<base::unique_qptr<Ui::PopupMenu>> holder) {
	const auto history = context.history ? context.history() : nullptr;
	if (!history) {
		return;
	}
	const auto session = &history->session();
	const auto field = QPointer<Ui::InputField>(context.field.get());
	const auto show = context.show;
	*holder = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto menu = holder->get();
	const auto weak = QPointer<Ui::PopupMenu>(menu);
	const auto close = [=] {
		if (weak) {
			weak->hideMenu();
		}
	};
	const auto items = Items(session);
	if (items.isEmpty()) {
		menu->addAction(Tr("还没有快捷回复", "No quick replies yet"), [] {
		})->setEnabled(false);
	} else {
		menu->addAction(Tr(
			"点击填入输入框，右键直接发送",
			"Click to insert, right-click to send"), [] {})->setEnabled(false);
		for (const auto &text : items) {
			AddItem(
				menu,
				text,
				[=] { Insert(field, text); },
				[=] { SendNow(context, field, text); },
				close);
		}
	}
	menu->addSeparator();
	menu->addAction(
		items.isEmpty()
			? Tr("添加快捷回复", "Add quick reply")
			: Tr("管理", "Manage"),
		[=] {
			if (const auto window = show->resolveWindow()) {
				window->showSettings(::Settings::LuckgramQuickReplies::Id());
			}
		},
		&st::menuIconSettings);
	menu->popup(QCursor::pos());
}

} // namespace

bool Enabled() {
	return Settings::IsEnabled(QString::fromLatin1(kFeature));
}

QStringList Items(not_null<Main::Session*> session) {
	return GetStore(session).data.items;
}

rpl::producer<> Changes(not_null<Main::Session*> session) {
	return GetStore(session).changes.events();
}

bool Add(not_null<Main::Session*> session, const QString &text) {
	auto &store = GetStore(session);
	const auto normalized = Normalize(text);
	if (!normalized || !CanAdd(store.data.items.size())) {
		return false;
	}
	store.data.items.push_back(*normalized);
	store.changed();
	return true;
}

bool Edit(
		not_null<Main::Session*> session,
		int index,
		const QString &text) {
	auto &store = GetStore(session);
	const auto normalized = Normalize(text);
	if (!normalized || index < 0 || index >= store.data.items.size()) {
		return false;
	}
	store.data.items[index] = *normalized;
	store.changed();
	return true;
}

void Remove(not_null<Main::Session*> session, int index) {
	auto &store = GetStore(session);
	if (index >= 0 && index < store.data.items.size()) {
		store.data.items.removeAt(index);
		store.changed();
	}
}

void Move(not_null<Main::Session*> session, int from, int to) {
	auto &store = GetStore(session);
	const auto moved = QuickReplies::Move(store.data.items, from, to);
	if (moved != store.data.items) {
		store.data.items = moved;
		store.changed();
	}
}

Compose::Slot CreateSlot(const Compose::SlotContext &context) {
	auto button = object_ptr<Ui::IconButton>(
		context.parent,
		st::historyLuckgramQuickReplies);
	const auto raw = button.data();
	const auto holder = std::make_shared<
		base::unique_qptr<Ui::PopupMenu>>();
	raw->setClickedCallback([=] {
		if (Enabled()) {
			ShowList(context, raw, holder);
		}
	});
	return {
		.widget = std::move(button),
		.wanted = Settings::EnabledValue(QString::fromLatin1(kFeature)),
		// specs#94: first of the Luckgram items in the 「+」 menu.
		.plus = [=](not_null<Ui::DropdownMenu*> menu) {
			if (Enabled()) {
				menu->addAction(Tr("快捷回复", "Quick replies"), [=] {
					ShowList(context, raw->window(), holder);
				}, &st::menuIconShortcut);
			}
		},
	};
}

} // namespace Luckgram::QuickReplies
