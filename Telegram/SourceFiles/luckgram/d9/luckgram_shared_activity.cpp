/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/d9/luckgram_shared_activity.h"

#include "base/call_delayed.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "luckgram/d9/luckgram_shared_activity_logic.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/userpic_view.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <deque>

namespace Luckgram::SharedActivity {
namespace {

constexpr auto kCommonChatsPerPage = 100;

struct Result {
	std::vector<FullMsgId> items;
	int groupCount = 0;
	bool partial = false;
	bool loaded = false;
	crl::time loadedAt = 0;
};

// Per account: user -> last full (not partial) result.
[[nodiscard]] base::flat_map<UserId, Result> &Cache(
		not_null<Main::Session*> session) {
	static auto caches = base::flat_map<
		not_null<Main::Session*>,
		base::flat_map<UserId, Result>>();
	if (!caches.contains(session)) {
		session->lifetime().add([=] {
			caches.remove(session);
		});
	}
	return caches[session];
}

[[nodiscard]] bool IsOwnGroup(not_null<PeerData*> peer) {
	if (peer->migrateTo()) {
		return false;
	} else if (const auto chat = peer->asChat()) {
		return chat->amIn();
	} else if (const auto channel = peer->asChannel()) {
		return channel->isMegagroup() && channel->amIn();
	}
	return false;
}

[[nodiscard]] int64 BareId(not_null<PeerData*> peer) {
	return peer->isChat()
		? int64(peerToChat(peer->id).bare)
		: int64(peerToChannel(peer->id).bare);
}

class Loader final : public base::has_weak_ptr {
public:
	Loader(not_null<UserData*> user, Fn<void(Result)> done);

	void start();

private:
	void loadCommonChats(int64 maxId);
	void startSearches();
	void pump();
	void search(not_null<PeerData*> group);
	void finish();
	[[nodiscard]] crl::time elapsed() const;

	const not_null<UserData*> _user;
	const Fn<void(Result)> _done;
	MTP::Sender _api;
	base::Timer _deadline;
	std::vector<not_null<PeerData*>> _groups;
	std::deque<not_null<PeerData*>> _queue;
	std::vector<std::vector<Dated>> _perGroup;
	std::vector<FullMsgId> _ids;
	crl::time _startedAt = 0;
	int _running = 0;
	bool _partial = false;
	bool _finished = false;

};

Loader::Loader(not_null<UserData*> user, Fn<void(Result)> done)
: _user(user)
, _done(std::move(done))
, _api(&user->session().mtp())
, _deadline([=] {
	// Time budget is over: show what has arrived (16 P1).
	_partial = true;
	finish();
}) {
}

void Loader::start() {
	_startedAt = crl::now();
	_deadline.callOnce(kTimeBudgetMs);
	loadCommonChats(0);
}

crl::time Loader::elapsed() const {
	return crl::now() - _startedAt;
}

void Loader::loadCommonChats(int64 maxId) {
	if (_finished) {
		return;
	}
	_api.request(MTPmessages_GetCommonChats(
		_user->inputUser(),
		MTP_long(maxId),
		MTP_int(kCommonChatsPerPage)
	)).done([=](const MTPmessages_Chats &result) {
		if (_finished) {
			return;
		}
		const auto &chats = result.match([](const auto &data) {
			return data.vchats().v;
		});
		auto last = int64(0);
		const auto owner = &_user->owner();
		for (const auto &chat : chats) {
			const auto peer = owner->processChat(chat);
			last = BareId(peer);
			// Only groups you are in yourself (16 requirement 6).
			if (IsOwnGroup(peer) && !ranges::contains(_groups, peer)) {
				_groups.push_back(peer);
			}
		}
		if (chats.size() >= kCommonChatsPerPage && last) {
			loadCommonChats(last);
		} else {
			startSearches();
		}
	}).fail([=](const MTP::Error &error) {
		if (_finished) {
			return;
		}
		const auto wait = FloodWaitSeconds(error.type());
		if (CanWaitFlood(wait, elapsed())) {
			base::call_delayed(wait * crl::time(1000) + 500, this, [=] {
				loadCommonChats(maxId);
			});
		} else {
			// The group list is incomplete: say so, not "no groups".
			_partial = true;
			startSearches();
		}
	}).handleFloodErrors().send();
}

void Loader::startSearches() {
	_queue.insert(end(_queue), begin(_groups), end(_groups));
	pump();
}

void Loader::pump() {
	if (_finished) {
		return;
	}
	while (_running < kMaxParallel && !_queue.empty()) {
		const auto group = _queue.front();
		_queue.pop_front();
		search(group);
	}
	if (!_running && _queue.empty()) {
		finish();
	}
}

void Loader::search(not_null<PeerData*> group) {
	using Flag = MTPmessages_Search::Flag;
	++_running;
	_api.request(MTPmessages_Search(
		MTP_flags(Flag::f_from_id),
		group->input(),
		MTP_string(), // q
		_user->input(),
		MTPInputPeer(), // saved_peer_id
		MTPVector<MTPReaction>(), // saved_reaction
		MTPint(), // top_msg_id
		MTP_inputMessagesFilterEmpty(),
		MTP_int(0), // min_date
		MTP_int(0), // max_date
		MTP_int(0), // offset_id
		MTP_int(0), // add_offset
		MTP_int(kPerGroup),
		MTP_int(0), // max_id
		MTP_int(0), // min_id
		MTP_long(0) // hash
	)).done([=](const MTPmessages_Messages &result) {
		--_running;
		if (_finished) {
			return;
		}
		const auto owner = &_user->owner();
		const auto list = result.match([&](
				const MTPDmessages_messagesNotModified &) {
			return (const QVector<MTPMessage>*)nullptr;
		}, [&](const auto &data) {
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());
			return &data.vmessages().v;
		});
		auto items = std::vector<Dated>();
		if (list) {
			for (const auto &message : *list) {
				const auto item = owner->addNewMessage(
					message,
					MessageFlags(),
					NewMessageType::Existing);
				if (!item || item->isService()) {
					continue;
				}
				items.push_back({
					.date = int(item->date()),
					.sortId = int64(item->id.bare),
					.index = int64(_ids.size()),
				});
				_ids.push_back(item->fullId());
			}
		}
		_perGroup.push_back(std::move(items));
		pump();
	}).fail([=](const MTP::Error &error) {
		--_running;
		if (_finished) {
			return;
		}
		const auto wait = FloodWaitSeconds(error.type());
		if (CanWaitFlood(wait, elapsed())) {
			// Sit the wait out, then try this group again (16 §5).
			++_running;
			base::call_delayed(wait * crl::time(1000) + 500, this, [=] {
				--_running;
				_queue.push_front(group);
				pump();
			});
			return;
		}
		_partial = true;
		pump();
	}).handleFloodErrors().send();
}

void Loader::finish() {
	if (_finished) {
		return;
	}
	_finished = true;
	_deadline.cancel();
	auto result = Result{
		.groupCount = int(_groups.size()),
		.partial = _partial,
		.loaded = true,
		.loadedAt = crl::now(),
	};
	for (const auto &dated : NewestFirst(_perGroup, kMaxItems)) {
		result.items.push_back(_ids[dated.index]);
	}
	if (!result.partial) {
		// A partial result is shown but not reused.
		Cache(&_user->session())[peerToUser(_user->id)] = result;
	}
	_done(std::move(result));
}

class ActivityRow final : public Ui::RippleButton {
public:
	ActivityRow(QWidget *parent, not_null<HistoryItem*> item);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const not_null<PeerData*> _group;
	const QString _name;
	const QString _date;
	const QString _preview;
	Ui::PeerUserpicView _userpic;

};

[[nodiscard]] QString DateText(TimeId date) {
	const auto parsed = base::unixtime::parse(date);
	return (parsed.date() == QDate::currentDate())
		? QLocale().toString(parsed.time(), QLocale::ShortFormat)
		: langDayOfMonthFull(parsed.date());
}

ActivityRow::ActivityRow(QWidget *parent, not_null<HistoryItem*> item)
: RippleButton(parent, st::defaultRippleAnimation)
, _group(item->history()->peer)
, _name(item->history()->peer->name())
, _date(DateText(item->date()))
, _preview(item->notificationText().text.simplified()) {
	resize(width(), st::peerListBoxItem.height);
	_group->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		update();
	}, lifetime());
}

void ActivityRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto &st = st::peerListBoxItem;
	p.fillRect(rect(), isOver() ? st::contactsBgOver : st::contactsBg);
	paintRipple(p, 0, 0);
	_group->paintUserpicLeft(
		p,
		_userpic,
		st.photoPosition.x(),
		st.photoPosition.y(),
		width(),
		st.photoSize);
	const auto left = st.namePosition.x();
	const auto right = st::boxRowPadding.right();
	const auto dateWidth = st::normalFont->width(_date);
	p.setFont(st::normalFont);
	p.setPen(st::contactsStatusFg);
	p.drawTextRight(right, st.namePosition.y(), width(), _date, dateWidth);
	const auto nameWidth = std::max(
		width() - left - right - dateWidth - st::normalFont->spacew * 2,
		1);
	p.setPen(st::contactsNameFg);
	p.setFont(st::semiboldFont);
	p.drawTextLeft(
		left,
		st.namePosition.y(),
		width(),
		st::semiboldFont->elided(_name, nameWidth));
	p.setPen(st::contactsStatusFg);
	p.setFont(st::normalFont);
	p.drawTextLeft(
		st.statusPosition.x(),
		st.statusPosition.y(),
		width(),
		st::normalFont->elided(_preview, std::max(width() - left - right, 1)));
}

} // namespace

bool Enabled() {
	return Settings::IsEnabled(QString::fromLatin1(kFeatureKey));
}

rpl::producer<bool> EnabledValue() {
	return Settings::EnabledValue(QString::fromLatin1(kFeatureKey));
}

bool AppliesTo(not_null<UserData*> user) {
	return !user->isSelf() && !user->isBot() && !user->isInaccessible();
}

QString Title() {
	return Tr("动态", "Activity");
}

void ActivityBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	struct State {
		std::unique_ptr<Loader> loader;
		rpl::variable<QString> header;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto session = &controller->session();

	box->setTitle(rpl::single(Title()));
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(st::boxMaxListHeight);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			state->header.value(),
			st::boxDividerLabel),
		st::boxRowPadding);
	box->addSkip(st::boxRowPadding.left() / 2);
	const auto list = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins());

	const auto show = [=](const Result &result) {
		list->clear();
		state->header = Header(
			result.groupCount,
			result.partial,
			IsChinese());
		auto shown = 0;
		for (const auto &id : result.items) {
			const auto item = session->data().message(id);
			if (!item) {
				continue;
			}
			const auto peerId = item->history()->peer->id;
			const auto msgId = item->id;
			const auto row = list->add(object_ptr<ActivityRow>(list, item));
			row->setClickedCallback([=] {
				box->closeBox();
				controller->showPeerHistory(
					peerId,
					Window::SectionShow::Way::Forward,
					msgId);
			});
			++shown;
		}
		if (!shown) {
			list->add(
				object_ptr<Ui::FlatLabel>(
					list,
					(result.groupCount || result.partial)
						? Tr(
							"最近没有在共同群组里发过消息",
							"No recent messages in your groups in common")
						: Tr("你们没有共同群组", "You have no groups in common"),
					st::boxDividerLabel),
				st::boxRowPadding);
		}
		list->resizeToWidth(list->width());
	};
	const auto load = [=](bool force) {
		const auto &cache = Cache(session);
		const auto i = cache.find(peerToUser(user->id));
		if (!force
			&& i != end(cache)
			&& IsFresh(i->second.loadedAt, crl::now())) {
			show(i->second);
			return;
		}
		state->header = Tr("加载中…", "Loading…");
		state->loader = std::make_unique<Loader>(user, crl::guard(box, show));
		state->loader->start();
	};

	box->addButton(rpl::single(Tr("刷新", "Refresh")), [=] {
		load(true);
	});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
	load(false);
}

} // namespace Luckgram::SharedActivity
