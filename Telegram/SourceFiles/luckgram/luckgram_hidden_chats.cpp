/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_hidden_chats.h"

#include "boxes/peer_list_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "luckgram/luckgram_double_bottom.h"
#include "luckgram/luckgram_privacy_logic.h"
#include "luckgram/luckgram_privacy_ui.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_storage.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "storage/storage_domain.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace Luckgram::HiddenChats {
namespace {

// Account level, encrypted with the local key (docs/desktop.md storage
// rule); kLegacyIdsPrefix is the plain list in luckgram.json before it,
// moved over the first time an account is read.
constexpr auto kStorageName = "hidden_chats";
constexpr auto kLegacyIdsPrefix = "hidden_chats.ids.";
constexpr auto kLockKey = "hidden_chats.lock";
constexpr auto kNotifyKey = "hidden_chats.notify";
constexpr auto kHintShownKey = "hidden_chats.hint_shown";

[[nodiscard]] QString Name(const char *key) {
	return QString::fromLatin1(key);
}

[[nodiscard]] quint64 AccountKey(not_null<Main::Session*> session) {
	return quint64(session->userId().bare);
}

[[nodiscard]] QString LegacyIdsKey(quint64 account) {
	return Name(kLegacyIdsPrefix) + QString::number(account);
}

// History::shouldBeInChatList() asks on every list update, so the lists
// and the switch are cached. A list read while the account's storage is
// unavailable (an account the second space hides) lives in memory only
// and is read again when the space changes.
struct Cache {
	bool enabledKnown = false;
	bool enabled = false;
	base::flat_map<quint64, base::flat_set<quint64>> ids;
	rpl::event_stream<> changes;
	rpl::lifetime lifetime;
};

void RefreshAll();

[[nodiscard]] Cache &State() {
	static auto result = [] {
		auto cache = std::make_unique<Cache>();
		const auto raw = cache.get();
		DoubleBottom::ActiveChanges(
		) | rpl::on_next([=] {
			raw->ids.clear();
			RefreshAll();
			raw->changes.fire({});
		}, raw->lifetime);
		Settings::Changes(
		) | rpl::on_next([=](const QString &key) {
			const auto feature = key.isEmpty() || key == Name(kFeatureKey);
			if (feature) {
				const auto was = raw->enabled;
				raw->enabledKnown = false;
				const auto now = Settings::IsEnabled(Name(kFeatureKey));
				raw->enabled = now;
				raw->enabledKnown = true;
				if (was != now) {
					RefreshAll(); // Also covers the remote switch.
				}
			}
			raw->changes.fire({});
		}, raw->lifetime);
		return cache;
	}();
	return *result;
}

void Persist(not_null<Main::Session*> session, const QString &value) {
	const auto name = Name(kStorageName);
	if (value.isEmpty()) {
		Storage::RemoveAccount(session, name);
	} else {
		Storage::WriteAccount(session, name, value.toUtf8());
	}
}

[[nodiscard]] const base::flat_set<quint64> &Ids(
		not_null<Main::Session*> session) {
	auto &state = State();
	const auto account = AccountKey(session);
	const auto i = state.ids.find(account);
	if (i != end(state.ids)) {
		return i->second;
	}
	auto ids = std::vector<quint64>();
	if (Storage::AccountAvailable(session)) {
		ids = Privacy::ParseIds(QString::fromUtf8(
			Storage::ReadAccount(session, Name(kStorageName))));
		const auto legacyKey = LegacyIdsKey(account);
		if (Settings::Has(legacyKey)) {
			const auto legacy = Privacy::ParseIds(
				Settings::GetString(legacyKey));
			ids.insert(ids.end(), legacy.begin(), legacy.end());
			const auto value = Privacy::SerializeIds(ids);
			if (value.isEmpty()
				|| Storage::WriteAccount(
					session,
					Name(kStorageName),
					value.toUtf8())) {
				Settings::Remove(legacyKey);
			}
			ids = Privacy::ParseIds(value);
		}
	}
	auto set = base::flat_set<quint64>();
	for (const auto id : ids) {
		set.emplace(id);
	}
	return state.ids.emplace(account, std::move(set)).first->second;
}

void RefreshSession(not_null<Main::Session*> session) {
	const auto owner = &session->data();
	for (const auto id : Ids(session)) {
		if (const auto history = owner->historyLoaded(DeserializePeerId(id))) {
			history->updateChatListExistence();
		}
	}
	owner->notifyUnreadBadgeChanged();
}

void RefreshAll() {
	if (!Core::IsAppLaunched() || !Core::App().domain().started()) {
		return;
	}
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (const auto session = account->maybeSession()) {
			RefreshSession(session);
		}
	}
}

[[nodiscard]] bool TakeFirstHint() {
	if (Settings::GetBool(Name(kHintShownKey), false)) {
		return false;
	}
	Settings::SetBool(Name(kHintShownKey), true);
	return true;
}

[[nodiscard]] bool LocalPasscodeOn() {
	return Core::App().domain().local().hasLocalPasscode();
}

void ShowUndoToast(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<History*> history,
		bool hidden) {
	auto text = TextWithEntities{ hidden
		? Tr("已隐藏", "Hidden")
		: Tr("已取消隐藏", "Unhidden") };
	text.append(u" · "_q).append(tr::link(Tr("撤销", "Undo")));
	if (hidden && TakeFirstHint()) {
		text.append(u"\n"_q).append(Tr(
			"在左上角菜单 ☰ 里可以打开隐藏的聊天",
			"Open hidden chats from the ☰ menu in the top left corner"));
	}
	const auto weak = base::make_weak(history);
	show->showToast(Ui::Toast::Config{
		.text = std::move(text),
		.filter = [=](const ClickHandlerPtr &, Qt::MouseButton) {
			if (const auto strong = weak.get()) {
				SetHidden(strong, !hidden);
			}
			return false;
		},
		.duration = crl::time(5000),
	});
}

void ShowHiddenBox(
	not_null<Window::SessionController*> controller,
	const QString &query);

class HiddenListController final : public PeerListController {
public:
	HiddenListController(
		not_null<Window::SessionController*> window,
		QString query);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

private:
	void refreshRows();

	const not_null<Window::SessionController*> _window;
	const QString _query;
	rpl::lifetime _lifetime;

};

HiddenListController::HiddenListController(
	not_null<Window::SessionController*> window,
	QString query)
: _window(window)
, _query(std::move(query)) {
}

Main::Session &HiddenListController::session() const {
	return _window->session();
}

void HiddenListController::prepare() {
	delegate()->peerListSetTitle(TrValue("隐藏的聊天", "Hidden chats"));
	if (!_query.isEmpty()) {
		delegate()->peerListSetAdditionalTitle(rpl::single(_query));
	}
	refreshRows();

	State().changes.events() | rpl::on_next([=] {
		refreshRows();
	}, _lifetime);
}

void HiddenListController::refreshRows() {
	const auto list = _query.isEmpty()
		? List(&session())
		: Search(&session(), _query);
	auto keep = base::flat_set<PeerListRowId>();
	for (const auto &history : list) {
		const auto id = history->peer->id.value;
		keep.emplace(id);
		if (!delegate()->peerListFindRow(id)) {
			delegate()->peerListAppendRow(
				std::make_unique<PeerListRow>(history->peer));
		}
	}
	for (auto i = delegate()->peerListFullRowsCount(); i > 0; --i) {
		const auto row = delegate()->peerListRowAt(i - 1);
		if (!keep.contains(row->id())) {
			delegate()->peerListRemoveRow(row);
		}
	}
	setDescriptionText(list.empty()
		? Tr(
			"在聊天列表右键聊天，选择「隐藏」，它就会出现在这里。",
			"Right-click a chat in the chat list and choose Hide; it will show up here.")
		: Tr(
			"右键聊天可以取消隐藏。",
			"Right-click a chat to unhide it."));
	delegate()->peerListRefreshRows();
}

void HiddenListController::rowClicked(not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	const auto window = _window;
	window->hideLayer();
	window->showPeerHistory(peer);
}

base::unique_qptr<Ui::PopupMenu> HiddenListController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	const auto history = row->peer()->owner().history(row->peer());
	const auto show = _window->uiShow();
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	result->addAction(Tr("取消隐藏", "Unhide"), [=] {
		SetHidden(history, false);
		ShowUndoToast(show, history, false);
	}, &st::menuIconShowAll);
	return result;
}

void ShowHiddenBox(
		not_null<Window::SessionController*> controller,
		const QString &query) {
	auto list = std::make_unique<HiddenListController>(controller, query);
	controller->show(Box<PeerListBox>(std::move(list), [=](
			not_null<PeerListBox*> box) {
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

} // namespace

bool Enabled() {
	auto &state = State();
	if (!state.enabledKnown) {
		state.enabled = Settings::IsEnabled(Name(kFeatureKey));
		state.enabledKnown = true;
	}
	return state.enabled;
}

bool IsHidden(not_null<const History*> history) {
	if (!Enabled()) {
		return false;
	}
	const auto session = &history->session();
	return Ids(session).contains(
		SerializePeerId(history->peer->id));
}

bool HasHidden(not_null<Main::Session*> session) {
	return Enabled() && !Ids(session).empty();
}

rpl::producer<bool> HasHiddenValue(not_null<Main::Session*> session) {
	return rpl::single(rpl::empty) | rpl::then(
		State().changes.events()
	) | rpl::map([=] {
		return HasHidden(session);
	}) | rpl::distinct_until_changed();
}

std::vector<not_null<History*>> List(not_null<Main::Session*> session) {
	auto result = std::vector<not_null<History*>>();
	if (!Enabled()) {
		return result;
	}
	const auto owner = &session->data();
	for (const auto id : Ids(session)) {
		const auto peerId = DeserializePeerId(id);
		if (owner->peerLoaded(peerId)) {
			result.push_back(owner->history(peerId));
		}
	}
	ranges::stable_sort(result, ranges::greater(), [](not_null<History*> h) {
		return h->chatListTimeId();
	});
	return result;
}

std::vector<not_null<History*>> Search(
		not_null<Main::Session*> session,
		const QString &query) {
	auto words = std::vector<QString>();
	for (const auto &word : TextUtilities::PrepareSearchWords(query)) {
		words.push_back(word);
	}
	auto result = std::vector<not_null<History*>>();
	if (words.empty()) {
		return result;
	}
	for (const auto &history : List(session)) {
		auto name = std::vector<QString>();
		for (const auto &word : history->peer->nameWords()) {
			name.push_back(word);
		}
		if (Privacy::NameMatches(words, name)) {
			result.push_back(history);
		}
	}
	return result;
}

void SetHidden(not_null<History*> history, bool hidden) {
	const auto session = &history->session();
	const auto account = AccountKey(session);
	auto current = std::vector<quint64>();
	for (const auto id : Ids(session)) {
		current.push_back(id);
	}
	const auto updated = Privacy::WithHidden(
		std::move(current),
		SerializePeerId(history->peer->id),
		hidden);
	if (!updated) {
		return;
	}
	auto &state = State();
	auto &set = state.ids[account];
	set.clear();
	for (const auto id : *updated) {
		set.emplace(id);
	}
	Persist(session, Privacy::SerializeIds(*updated));
	history->updateChatListExistence();
	session->data().notifyUnreadBadgeChanged();
	state.changes.fire({});
}

bool LockEnabled() {
	return Settings::GetBool(Name(kLockKey), false);
}

void SetLockEnabled(bool enabled) {
	Settings::SetBool(Name(kLockKey), enabled);
}

bool NotifyEnabled() {
	return Settings::GetBool(Name(kNotifyKey), false);
}

void SetNotifyEnabled(bool enabled) {
	Settings::SetBool(Name(kNotifyKey), enabled);
}

bool SuppressNotification(not_null<const History*> history) {
	return Privacy::SuppressHiddenNotification(
		Enabled(),
		IsHidden(history),
		NotifyEnabled());
}

void OnToggled(bool enabled) {
	auto &state = State();
	state.enabled = Settings::IsEnabled(Name(kFeatureKey));
	state.enabledKnown = true;
	RefreshAll();
	state.changes.fire({});
}

void AddContextMenuAction(
		not_null<Window::SessionController*> controller,
		not_null<History*> history,
		const Ui::Menu::MenuCallback &addAction) {
	if (!Enabled() || history->peer->isSelf()) {
		return;
	}
	const auto show = controller->uiShow();
	if (IsHidden(history)) {
		addAction(Tr("取消隐藏", "Unhide"), [=] {
			SetHidden(history, false);
			ShowUndoToast(show, history, false);
		}, &st::menuIconShowAll);
	} else {
		addAction(Tr("隐藏", "Hide"), [=] {
			SetHidden(history, true);
			ShowUndoToast(show, history, true);
		}, &st::menuIconStealth);
	}
}

void AddMainMenuEntry(
		not_null<Ui::VerticalLayout*> menu,
		not_null<Window::SessionController*> controller) {
	const auto wrap = menu->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			menu,
			::Settings::CreateButtonWithIcon(
				menu,
				TrValue("隐藏的聊天", "Hidden chats"),
				st::mainMenuButton,
				{ &st::menuIconStealth })));
	wrap->toggleOn(HasHiddenValue(&controller->session()));
	wrap->finishAnimating();
	wrap->entity()->setClickedCallback([=] {
		Open(controller);
	});
}

void Open(
		not_null<Window::SessionController*> controller,
		const QString &query) {
	if (LockEnabled() && LocalPasscodeOn()) {
		PrivacyUi::AskMainPasscode(
			controller->uiShow(),
			Tr("隐藏的聊天", "Hidden chats"),
			crl::guard(controller, [=](QString) {
				ShowHiddenBox(controller, query);
			}));
	} else {
		ShowHiddenBox(controller, query);
	}
}

object_ptr<Ui::RpWidget> CreateSearchFooter(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		const QString &query) {
	if (query.isEmpty()) {
		return { nullptr };
	}
	const auto count = int(Search(&controller->session(), query).size());
	if (!count) {
		return { nullptr };
	}
	auto result = object_ptr<Ui::SettingsButton>(
		parent,
		rpl::single(SearchHiddenText(count)),
		st::settingsButtonNoIcon);
	result->setClickedCallback([=] {
		Open(controller, query);
	});
	return result;
}

QString SearchHiddenText(int count) {
	return Tr("在隐藏的聊天中搜索", "Search in hidden chats")
		+ u" ("_q
		+ QString::number(count)
		+ u")"_q;
}

} // namespace Luckgram::HiddenChats
