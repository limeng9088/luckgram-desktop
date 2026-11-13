/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/d9/luckgram_keyword_folders.h"

#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat_filters.h"
#include "data/data_folder.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "luckgram/d9/luckgram_account_store.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Luckgram::Keywords {
namespace {

const auto kFileName = u"keyword_folders"_q;

struct State {
	std::vector<Folder> folders;
	// Folder id -> chats whose recent messages matched.
	base::flat_map<int, base::flat_set<not_null<History*>>> matches;
	base::flat_set<not_null<History*>> pendingRecheck;
	bool recheckScheduled = false;
	bool lastEnabled = false;
	rpl::event_stream<> changes;
	rpl::lifetime lifetime;
};

[[nodiscard]] base::flat_map<
	not_null<Main::Session*>,
	std::unique_ptr<State>> &States() {
	static auto result = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<State>>();
	return result;
}

[[nodiscard]] FilterId FilterIdOf(const Folder &folder) {
	return kFilterIdBase + folder.id;
}

[[nodiscard]] ChatKind KindOf(not_null<History*> history) {
	const auto peer = history->peer;
	if (peer->isUser()) {
		return ChatKind::Private;
	} else if (const auto channel = peer->asChannel()) {
		return channel->isBroadcast() ? ChatKind::Channel : ChatKind::Group;
	}
	return ChatKind::Group;
}

[[nodiscard]] QString TextOf(not_null<const HistoryItem*> item) {
	return item->originalText().text;
}

// The last message and the latest kRecentMessagesLimit messages loaded on
// this device (Telegram Desktop keeps no message database on disk, so
// "cached" means loaded in this session).
template <typename Callback>
void EnumerateRecent(not_null<History*> history, int limit, Callback &&callback) {
	auto seen = 0;
	if (const auto last = history->lastMessage()) {
		if (callback(not_null<const HistoryItem*>(last))) {
			return;
		}
	}
	for (auto b = history->blocks.rbegin(); b != history->blocks.rend(); ++b) {
		const auto &messages = (*b)->messages;
		for (auto m = messages.rbegin(); m != messages.rend(); ++m) {
			if (seen++ >= limit) {
				return;
			}
			const auto item = (*m)->data();
			if (callback(not_null<const HistoryItem*>(item.get()))) {
				return;
			}
		}
	}
}

[[nodiscard]] bool ScanMatches(
		not_null<History*> history,
		const QStringList &keywords) {
	auto found = false;
	EnumerateRecent(history, kRecentMessagesLimit, [&](
			not_null<const HistoryItem*> item) {
		found = MatchesAny(TextOf(item), keywords);
		return found;
	});
	return found;
}

void Save(not_null<Main::Session*> session, const State &state) {
	AccountStore::Write(session, kFileName, Serialize(state.folders));
}

State &Lookup(not_null<Main::Session*> session);

void RefreshFilters(not_null<Main::Session*> session) {
	session->data().chatsFilters().luckgramRefreshLocal();
}

void ForEachHistory(
		not_null<Main::Session*> session,
		Fn<void(not_null<History*>)> callback) {
	const auto feed = [&](not_null<const Dialogs::MainList*> list) {
		for (const auto &row : *list->indexed()) {
			if (const auto history = row->history()) {
				callback(history);
			}
		}
	};
	feed(session->data().chatsList());
	if (const auto folder = session->data().folderLoaded(Data::Folder::kId)) {
		feed(folder->chatsList());
	}
}

void Rescan(not_null<Main::Session*> session) {
	auto &state = Lookup(session);
	state.matches.clear();
	if (!Enabled() || state.folders.empty()) {
		return;
	}
	ForEachHistory(session, [&](not_null<History*> history) {
		for (const auto &folder : state.folders) {
			if (ScanMatches(history, folder.keywords)) {
				state.matches[folder.id].emplace(history);
			}
		}
	});
}

void Recheck(
		not_null<Main::Session*> session,
		not_null<History*> history) {
	auto &state = Lookup(session);
	auto changed = false;
	for (const auto &folder : state.folders) {
		auto &set = state.matches[folder.id];
		const auto now = ScanMatches(history, folder.keywords);
		const auto was = set.contains(history);
		if (now != was) {
			changed = true;
			if (now) {
				set.emplace(history);
			} else {
				set.remove(history);
			}
		}
	}
	if (changed) {
		session->data().chatsFilters().refreshHistory(history);
	}
}

void ScheduleRecheck(
		not_null<Main::Session*> session,
		not_null<History*> history) {
	auto &state = Lookup(session);
	if (!Enabled() || state.folders.empty()) {
		return;
	}
	state.pendingRecheck.emplace(history);
	if (state.recheckScheduled) {
		return;
	}
	state.recheckScheduled = true;
	crl::on_main(session, [=] {
		auto &state = Lookup(session);
		state.recheckScheduled = false;
		const auto histories = base::take(state.pendingRecheck);
		for (const auto history : histories) {
			Recheck(session, history);
		}
	});
}

void Subscribe(not_null<Main::Session*> session, State &state) {
	session->data().newItemAdded(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		// New messages show up in their folders right away (06 §3).
		ScheduleRecheck(session, item->history());
	}, state.lifetime);

	session->changes().messageUpdates(
		Data::MessageUpdate::Flag::Edited
	) | rpl::on_next([=](const Data::MessageUpdate &update) {
		ScheduleRecheck(session, update.item->history());
	}, state.lifetime);

	session->data().itemRemoved(
	) | rpl::on_next([=](not_null<const HistoryItem*> item) {
		ScheduleRecheck(session, item->history());
	}, state.lifetime);

	Settings::EnabledValue(
		QString::fromLatin1(kFeatureKey)
	) | rpl::filter([=](bool enabled) {
		return enabled != Lookup(session).lastEnabled;
	}) | rpl::on_next([=](bool enabled) {
		Lookup(session).lastEnabled = enabled;
		Rescan(session);
		RefreshFilters(session);
	}, state.lifetime);
}

State &Lookup(not_null<Main::Session*> session) {
	auto &states = States();
	if (const auto i = states.find(session); i != end(states)) {
		return *i->second;
	}
	auto state = std::make_unique<State>();
	state->folders = Deserialize(AccountStore::Read(session, kFileName));
	state->lastEnabled = Enabled();
	const auto raw = state.get();
	states.emplace(session, std::move(state));
	session->lifetime().add([=] {
		States().remove(session);
	});
	Subscribe(session, *raw);
	return *raw;
}

[[nodiscard]] const Folder *FindFolder(const State &state, FilterId id) {
	if (!IsLocalFilterId(id)) {
		return nullptr;
	}
	const auto folderId = int(id - kFilterIdBase);
	const auto i = ranges::find(state.folders, folderId, &Folder::id);
	return (i != end(state.folders)) ? &*i : nullptr;
}

void SaveFolder(not_null<Main::Session*> session, Folder folder) {
	auto &state = Lookup(session);
	folder.name = folder.name.trimmed();
	folder.keywords = CleanKeywords(folder.keywords);
	const auto i = ranges::find(state.folders, folder.id, &Folder::id);
	if (!folder.id || i == end(state.folders)) {
		if (int(state.folders.size()) >= kMaxFolders) {
			return;
		}
		folder.id = NextId(state.folders);
		// New folders go to the end of the tab bar.
		folder.order = int(session->data().chatsFilters().list().size());
		state.folders.push_back(folder);
	} else {
		folder.order = i->order;
		*i = folder;
	}
	Save(session, state);
	auto &set = state.matches[folder.id];
	set.clear();
	if (Enabled()) {
		ForEachHistory(session, [&](not_null<History*> history) {
			if (ScanMatches(history, folder.keywords)) {
				set.emplace(history);
			}
		});
	}
	RefreshFilters(session);
	state.changes.fire({});
}

void RemoveFolder(not_null<Main::Session*> session, FilterId id) {
	auto &state = Lookup(session);
	const auto folder = FindFolder(state, id);
	if (!folder) {
		return;
	}
	const auto folderId = folder->id;
	state.folders.erase(
		ranges::remove(state.folders, folderId, &Folder::id),
		end(state.folders));
	state.matches.remove(folderId);
	Save(session, state);
	// The chats themselves are not touched (06 acceptance).
	RefreshFilters(session);
	state.changes.fire({});
}

void EditBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		std::optional<Folder> existing) {
	const auto session = &controller->session();
	const auto editing = existing.has_value();
	const auto initial = existing.value_or(Folder());

	box->setTitle(rpl::single(editing
		? Tr("编辑关键词文件夹", "Edit Keyword Folder")
		: Tr("新建关键词文件夹", "New Keyword Folder")));
	box->setWidth(st::boxWideWidth);
	box->setCloseByOutsideClick(false);

	const auto container = box->verticalLayout();
	Ui::AddSubsectionTitle(
		container,
		rpl::single(Tr("文件夹名称", "Folder name")));
	const auto name = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		rpl::single(Tr("例如：发票", "e.g. Invoices")),
		initial.name));
	name->setMaxLength(64);

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		rpl::single(Tr("关键词（1–10 个）", "Keywords (1–10)")));
	const auto fieldsWrap = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins());
	const auto fields = box->lifetime().make_state<
		std::vector<not_null<Ui::InputField*>>>();
	const auto addField = [=](const QString &value) {
		const auto field = fieldsWrap->add(
			object_ptr<Ui::InputField>(
				fieldsWrap,
				st::defaultInputField,
				rpl::single(Tr("关键词", "Keyword")),
				value),
			st::boxRowPadding);
		field->setMaxLength(64);
		fields->push_back(field);
		fieldsWrap->resizeToWidth(fieldsWrap->width());
		return field;
	};
	for (const auto &keyword : initial.keywords) {
		addField(keyword);
	}
	if (fields->empty()) {
		addField(QString());
	}
	const auto add = box->addRow(
		object_ptr<Ui::LinkButton>(box, Tr("添加关键词", "Add keyword")),
		st::boxRowPadding);
	add->setClickedCallback([=] {
		if (int(fields->size()) >= kMaxKeywords) {
			controller->showToast(Tr("最多 10 个关键词", "Up to 10 keywords"));
			return;
		}
		addField(QString())->setFocusFast();
	});
	Ui::AddDividerText(
		container,
		rpl::single(Tr(
			"不区分大小写；中文按字词片段匹配，英文按整词匹配。只统计最近的消息：每个聊天的最后一条消息和最近 50 条已缓存在本机的消息。",
			"Case-insensitive. Chinese keywords match any part of the text, English keywords match whole words. Only recent messages are checked: the last message and the latest 50 messages cached on this device.")));

	Ui::AddSubsectionTitle(container, rpl::single(Tr("范围", "Include")));
	const auto checkbox = [&](const QString &text, bool checked) {
		return box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				text,
				checked,
				st::defaultBoxCheckbox),
			st::boxRowPadding + QMargins(0, 0, 0, st::boxLittleSkip));
	};
	const auto scope = initial.scope;
	const auto all = checkbox(
		Tr("全部", "All"),
		(scope & kScopeAll) == kScopeAll);
	const auto privates = checkbox(
		Tr("私聊", "Private chats"),
		(scope & kScopePrivate) != 0);
	const auto groups = checkbox(
		Tr("群组", "Groups"),
		(scope & kScopeGroups) != 0);
	const auto channels = checkbox(
		Tr("频道", "Channels"),
		(scope & kScopeChannels) != 0);
	const auto syncing = box->lifetime().make_state<bool>(false);
	all->checkedChanges() | rpl::on_next([=](bool checked) {
		if (*syncing) {
			return;
		}
		*syncing = true;
		privates->setChecked(checked);
		groups->setChecked(checked);
		channels->setChecked(checked);
		*syncing = false;
	}, all->lifetime());
	for (const auto part : { privates, groups, channels }) {
		part->checkedChanges() | rpl::on_next([=] {
			if (*syncing) {
				return;
			}
			*syncing = true;
			all->setChecked(privates->checked()
				&& groups->checked()
				&& channels->checked());
			*syncing = false;
		}, part->lifetime());
	}
	Ui::AddDividerText(
		container,
		rpl::single(Tr(
			"关键词文件夹只保存在本机、按账号保存，不会同步到其他设备。最近消息里提到关键词的聊天会自动归入。",
			"Keyword folders are stored on this device for this account only and are not synced. Chats whose recent messages mention a keyword are added automatically.")));

	box->setFocusCallback([=] {
		name->setFocusFast();
	});

	const auto collect = [=] {
		auto result = initial;
		result.name = name->getLastText().trimmed();
		auto keywords = QStringList();
		for (const auto field : *fields) {
			keywords.push_back(field->getLastText());
		}
		result.keywords = CleanKeywords(keywords);
		result.scope = (privates->checked() ? kScopePrivate : 0)
			| (groups->checked() ? kScopeGroups : 0)
			| (channels->checked() ? kScopeChannels : 0);
		return result;
	};
	box->addButton(tr::lng_settings_save(), [=] {
		const auto folder = collect();
		switch (Validate(folder)) {
		case Invalid::Name:
			name->showError();
			controller->showToast(Tr("请输入文件夹名称", "Enter a folder name"));
			return;
		case Invalid::Keywords:
			fields->front()->showError();
			controller->showToast(Tr(
				"请至少填写一个关键词",
				"Enter at least one keyword"));
			return;
		case Invalid::Scope:
			controller->showToast(Tr(
				"请至少选择一个范围",
				"Choose at least one chat type"));
			return;
		case Invalid::None:
			break;
		}
		if (!editing && !CanCreate(session)) {
			controller->showToast(Tr(
				"最多可以创建 10 个关键词文件夹",
				"You can create up to 10 keyword folders"));
			return;
		}
		SaveFolder(session, folder);
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
	if (editing) {
		const auto id = FilterIdOf(initial);
		box->addLeftButton(
			rpl::single(Tr("删除关键词文件夹", "Delete Keyword Folder")),
			[=] {
				box->closeBox();
				ConfirmDelete(controller, id);
			},
			st::attentionBoxButton);
	}
}

} // namespace

bool Enabled() {
	return Settings::IsEnabled(QString::fromLatin1(kFeatureKey));
}

bool IsLocalFilterId(FilterId id) {
	return id >= kFilterIdBase;
}

bool Contains(not_null<History*> history, FilterId id) {
	if (!Enabled()) {
		return false;
	}
	const auto session = &history->session();
	auto &state = Lookup(session);
	const auto folder = FindFolder(state, id);
	if (!folder || !InScope(folder->scope, KindOf(history))) {
		return false;
	}
	const auto i = state.matches.find(folder->id);
	if (i != end(state.matches) && i->second.contains(history)) {
		return true;
	}
	// The last message is checked live, so new chats show up immediately.
	if (const auto last = history->lastMessage()) {
		if (MatchesAny(TextOf(last), folder->keywords)) {
			state.matches[folder->id].emplace(history);
			return true;
		}
	}
	return false;
}

void ArrangeLocal(
		not_null<Main::Session*> session,
		std::vector<Data::ChatFilter> &list) {
	list.erase(ranges::remove_if(list, [](const Data::ChatFilter &filter) {
		return IsLocalFilterId(filter.id());
	}), end(list));
	if (!Enabled()) {
		return;
	}
	auto folders = Lookup(session).folders;
	ranges::stable_sort(folders, ranges::less(), &Folder::order);
	using Flag = Data::ChatFilter::Flag;
	for (const auto &folder : folders) {
		const auto position = std::clamp(folder.order, 0, int(list.size()));
		list.insert(begin(list) + position, Data::ChatFilter(
			FilterIdOf(folder),
			Data::ChatFilterTitle{
				.text = TextWithEntities{ folder.name },
				.isStatic = true,
			},
			QString(), // iconEmoji
			std::nullopt, // colorIndex
			Flag::NoArchived | Flag::StaticTitle,
			{}, // always
			{}, // pinned
			{})); // never
	}
}

void RememberOrder(
		not_null<Main::Session*> session,
		const std::vector<FilterId> &order) {
	auto &state = Lookup(session);
	auto changed = false;
	for (auto i = 0; i != int(order.size()); ++i) {
		if (!IsLocalFilterId(order[i])) {
			continue;
		}
		const auto folderId = int(order[i] - kFilterIdBase);
		const auto j = ranges::find(state.folders, folderId, &Folder::id);
		if (j != end(state.folders) && j->order != i) {
			j->order = i;
			changed = true;
		}
	}
	if (changed) {
		Save(session, state);
	}
}

const QStringList &HighlightKeywords(
		not_null<Main::Session*> session,
		FilterId id) {
	static const auto kEmpty = QStringList();
	if (!IsLocalFilterId(id) || !Enabled()) {
		return kEmpty;
	}
	const auto folder = FindFolder(Lookup(session), id);
	return folder ? folder->keywords : kEmpty;
}

std::vector<Folder> Folders(not_null<Main::Session*> session) {
	auto result = Lookup(session).folders;
	ranges::stable_sort(result, ranges::less(), &Folder::order);
	return result;
}

std::optional<Folder> FindByFilterId(
		not_null<Main::Session*> session,
		FilterId id) {
	const auto folder = FindFolder(Lookup(session), id);
	return folder ? std::make_optional(*folder) : std::nullopt;
}

bool CanCreate(not_null<Main::Session*> session) {
	return int(Lookup(session).folders.size()) < kMaxFolders;
}

rpl::producer<> FoldersChanges(not_null<Main::Session*> session) {
	return Lookup(session).changes.events();
}

void ShowCreate(not_null<Window::SessionController*> controller) {
	if (!CanCreate(&controller->session())) {
		controller->showToast(Tr(
			"最多可以创建 10 个关键词文件夹",
			"You can create up to 10 keyword folders"));
		return;
	}
	controller->show(Box(EditBox, controller, std::optional<Folder>()));
}

void ShowEdit(
		not_null<Window::SessionController*> controller,
		FilterId id) {
	if (const auto folder = FindByFilterId(&controller->session(), id)) {
		controller->show(Box(EditBox, controller, folder));
	}
}

void ConfirmDelete(
		not_null<Window::SessionController*> controller,
		FilterId id) {
	const auto session = &controller->session();
	controller->show(Ui::MakeConfirmBox({
		.text = Tr(
			"确定删除这个关键词文件夹吗？其中的聊天不会受影响。",
			"Delete this keyword folder? The chats in it will not be affected."),
		.confirmed = [=](Fn<void()> close) {
			RemoveFolder(session, id);
			close();
		},
		.confirmText = Tr("删除", "Delete"),
		.cancelText = Tr("取消", "Cancel"),
		.confirmStyle = &st::attentionBoxButton,
		.title = Tr("删除文件夹", "Delete Folder"),
	}));
}

QString Title() {
	return Tr("关键词文件夹", "Keyword Folders");
}

QString MenuNewFolder() {
	return Tr("新建关键词文件夹", "New Keyword Folder");
}

JumpBar::JumpBar(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<History*> history)
: _controller(controller)
, _history(history)
, _wrap(parent, object_ptr<Ui::AbstractButton>(parent))
, _shadow(std::make_unique<Ui::PlainShadow>(parent)) {
	_wrap.hide(anim::type::instant);
	_shadow->hide();
	_shadow->showOn(rpl::combine(
		_wrap.shownValue(),
		_wrap.heightValue(),
		rpl::mappers::_1 && rpl::mappers::_2 > 0
	) | rpl::filter([=](bool shown) {
		return (shown == _shadow->isHidden());
	}));
	setup();
}

JumpBar::~JumpBar() = default;

void JumpBar::setup() {
	_wrap.geometryValue(
	) | rpl::on_next([=](QRect rect) {
		updateShadowGeometry(rect);
	}, _wrap.lifetime());

	const auto button = _wrap.entity();
	button->resize(0, st::historyTranslateBarHeight);
	button->setAttribute(Qt::WA_OpaquePaintEvent);
	button->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		QPainter(button).fillRect(clip, st::historyComposeButtonBg);
	}, button->lifetime());
	button->setClickedCallback([=] {
		jump();
	});

	_label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		QString(),
		st::historyTranslateLabel);
	_label->setAttribute(Qt::WA_TransparentForMouseEvents);
	_close = Ui::CreateChild<Ui::LinkButton>(button, Tr("关闭", "Close"));
	_close->setClickedCallback([=] {
		_closed = true;
		refreshShown();
	});

	const auto session = &_history->session();
	rpl::combine(
		_controller->activeChatsFilter(),
		Settings::EnabledValue(QString::fromLatin1(kFeatureKey)),
		rpl::single(rpl::empty) | rpl::then(FoldersChanges(session))
	) | rpl::on_next([=](FilterId id, bool, auto) {
		if (_filterId != id) {
			_filterId = id;
			_lastJumped = 0;
			_closed = false;
		}
		refreshShown();
	}, _wrap.lifetime());

	session->data().newItemAdded(
	) | rpl::filter([=](not_null<HistoryItem*> item) {
		return item->history() == _history;
	}) | rpl::on_next([=] {
		refreshShown();
	}, _wrap.lifetime());
}

QStringList JumpBar::keywords() const {
	return HighlightKeywords(&_history->session(), _filterId);
}

std::vector<JumpBar::Hit> JumpBar::collectHits() const {
	const auto list = keywords();
	auto result = std::vector<Hit>();
	if (list.isEmpty()) {
		return result;
	}
	for (const auto &block : _history->blocks) {
		for (const auto &view : block->messages) {
			const auto item = view->data();
			if (!item->isRegular()) {
				continue;
			}
			const auto keyword = FirstMatch(TextOf(item), list);
			if (!keyword.isEmpty()) {
				result.push_back({ item->id, keyword });
			}
		}
	}
	if (const auto last = _history->lastMessage()) {
		const auto keyword = FirstMatch(TextOf(last), list);
		if (!keyword.isEmpty()
			&& !ranges::contains(result, last->id, &Hit::id)) {
			result.push_back({ last->id, keyword });
		}
	}
	// Newest first: jumping goes from the latest hit back in time.
	ranges::sort(result, std::greater<>(), &Hit::id);
	return result;
}

void JumpBar::refreshShown() {
	const auto shown = !_closed && !keywords().isEmpty();
	if (shown) {
		refreshLabel();
	}
	_wrap.toggle(shown, anim::type::normal);
}

void JumpBar::refreshLabel() {
	const auto hits = collectHits();
	auto keyword = QString();
	for (const auto &hit : hits) {
		if (!_lastJumped || hit.id < _lastJumped) {
			keyword = hit.keyword;
			break;
		}
	}
	if (keyword.isEmpty()) {
		const auto list = keywords();
		keyword = hits.empty()
			? (list.isEmpty() ? QString() : list.front())
			: hits.front().keyword;
	}
	_label->setText(Tr("跳到下一个「%1」", "Next “%1”").arg(keyword));
	relayout();
}

void JumpBar::jump() {
	const auto hits = collectHits();
	if (hits.empty()) {
		_controller->showToast(Tr(
			"最近的消息里没有找到关键词",
			"No keyword found in recent messages"));
		return;
	}
	auto next = std::optional<Hit>();
	for (const auto &hit : hits) {
		if (!_lastJumped || hit.id < _lastJumped) {
			next = hit;
			break;
		}
	}
	if (!next) {
		_controller->showToast(Tr(
			"最近的消息里没有更多匹配，已回到最新一条",
			"No more matches in recent messages, back to the latest"));
		next = hits.front();
	}
	_lastJumped = next->id;
	if (const auto item = _history->owner().message(
			_history->peer->id,
			next->id)) {
		_controller->showMessage(item, Window::SectionShow::Way::Forward);
	}
	refreshLabel();
}

void JumpBar::relayout() {
	const auto button = _wrap.entity();
	const auto width = _width;
	const auto right = st::boxRowPadding.right();
	const auto skip = st::semiboldFont->spacew * 2;
	const auto rowHeight = st::historyTranslateBarHeight;
	_close->moveToRight(right, (rowHeight - _close->height()) / 2, width);
	const auto available = std::max(
		width - 2 * (right + _close->width() + skip),
		1);
	_label->resizeToWidth(std::min(_label->textMaxWidth(), available));
	_label->moveToLeft(
		(width - _label->width()) / 2,
		(rowHeight - _label->height()) / 2,
		width);
	button->resize(width, rowHeight);
}

void JumpBar::updateShadowGeometry(QRect wrapGeometry) {
	const auto regular = QRect(
		wrapGeometry.x(),
		wrapGeometry.y() + wrapGeometry.height(),
		wrapGeometry.width(),
		st::lineWidth);
	_shadow->setGeometry(_shadowGeometryPostprocess
		? _shadowGeometryPostprocess(regular)
		: regular);
}

void JumpBar::raise() {
	_wrap.raise();
	_shadow->raise();
}

void JumpBar::finishAnimating() {
	_wrap.finishAnimating();
}

void JumpBar::setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess) {
	_shadowGeometryPostprocess = std::move(postprocess);
	updateShadowGeometry(_wrap.geometry());
}

void JumpBar::move(int x, int y) {
	_wrap.move(x, y);
}

void JumpBar::resizeToWidth(int width) {
	if (_width != width) {
		_width = width;
		relayout();
	}
}

int JumpBar::height() const {
	return _wrap.height();
}

rpl::producer<int> JumpBar::heightValue() const {
	return _wrap.heightValue();
}

} // namespace Luckgram::Keywords

namespace Settings {
namespace {

using Luckgram::Tr;
using Luckgram::TrValue;
namespace Keywords = Luckgram::Keywords;

void FillFolders(
		not_null<Ui::VerticalLayout*> list,
		not_null<Window::SessionController*> controller) {
	list->clear();
	const auto session = &controller->session();
	for (const auto &folder : Keywords::Folders(session)) {
		const auto id = Keywords::kFilterIdBase + folder.id;
		const auto line = Tr("关键词：", "Keywords: ")
			+ folder.keywords.join(Tr("、", ", "));
		AddButtonWithLabel(
			list,
			rpl::single(folder.name),
			rpl::single(line),
			st::settingsButtonNoIcon
		)->addClickHandler([=] {
			Keywords::ShowEdit(controller, id);
		});
	}
	list->resizeToWidth(list->width());
}

} // namespace

LuckgramKeywordFolders::LuckgramKeywordFolders(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> LuckgramKeywordFolders::title() {
	return TrValue("关键词文件夹", "Keyword Folders");
}

void LuckgramKeywordFolders::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);
	Ui::AddSkip(container);
	const auto list = container->add(object_ptr<Ui::VerticalLayout>(container));
	AddButtonWithIcon(
		container,
		TrValue("新建关键词文件夹", "New Keyword Folder"),
		st::settingsButtonActive
	)->addClickHandler([=] {
		Keywords::ShowCreate(controller);
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		TrValue(
			"关键词文件夹只保存在本机、按账号保存，不会同步到其他设备。最近消息里提到关键词的聊天会自动归入。",
			"Keyword folders are stored on this device for this account only and are not synced. Chats whose recent messages mention a keyword are added automatically."));

	const auto refill = [=] {
		FillFolders(list, controller);
	};
	Keywords::FoldersChanges(
		&controller->session()
	) | rpl::on_next(refill, list->lifetime());
	refill();

	Ui::ResizeFitChild(this, container);
}

} // namespace Settings
