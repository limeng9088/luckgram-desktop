/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/selectall/select_all.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_search_controller.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/selectall/select_all_logic.h"
#include "main/main_session.h"

namespace Luckgram::SelectAll {
namespace {

struct Loader {
	not_null<PeerData*> peer;
	PeerData *from = nullptr; // nullptr: every message (broadcast).
	Delegate delegate;
	base::flat_set<MsgId> seen;
	int loaded = 0;
};

[[nodiscard]] bool Broadcast(not_null<PeerData*> peer) {
	return peer->isChannel() && !peer->isMegagroup();
}

void LoadPage(std::shared_ptr<Loader> loader, MsgId offsetId) {
	const auto peer = loader->peer;
	using Flag = MTPmessages_Search::Flag;
	peer->session().api().request(MTPmessages_Search(
		MTP_flags(loader->from ? Flag::f_from_id : Flag(0)),
		peer->input(),
		MTP_string(), // q
		loader->from ? loader->from->input() : MTP_inputPeerEmpty(),
		MTPInputPeer(), // saved_peer_id
		MTPVector<MTPReaction>(), // saved_reaction
		MTPint(), // top_msg_id
		MTP_inputMessagesFilterEmpty(),
		MTP_int(0), // min_date
		MTP_int(0), // max_date
		MTP_int(offsetId.bare),
		MTP_int(0), // add_offset
		MTP_int(kPageSize),
		MTP_int(0), // max_id
		MTP_int(0), // min_id
		MTP_long(0) // hash
	)).done([=](const MTPmessages_Messages &result) {
		const auto parsed = Api::ParseSearchResult(
			peer,
			Storage::SharedMediaType::kCount,
			MsgId(0),
			Data::LoadDirection::Before,
			result);
		auto page = std::vector<not_null<HistoryItem*>>();
		auto minId = MsgId();
		for (const auto id : parsed.messageIds) {
			if (!minId || id < minId) {
				minId = id;
			}
			if (!loader->seen.emplace(id).second) {
				continue;
			}
			const auto item = peer->owner().message(peer, id);
			if (item && !item->isService() && item->canBeSelected()) {
				page.push_back(item);
			}
		}
		loader->loaded += int(page.size());
		if (!page.empty() && !loader->delegate.page(std::move(page))) {
			return; // Selection left: cancelled, no toast.
		}
		if (LoadMore(int(parsed.messageIds.size()), minId.bare)) {
			LoadPage(loader, minId);
		} else {
			loader->delegate.finished(true, loader->loaded);
		}
	}).fail([=] {
		loader->delegate.finished(false, loader->loaded);
	}).send();
}

} // namespace

bool Enabled() {
	return Settings::IsEnabled(QString::fromLatin1(kFeature));
}

QString MenuTitleFor(not_null<HistoryItem*> item) {
	if (!Enabled() || !item->isRegular() || !item->canBeSelected()) {
		return QString();
	}
	const auto peer = item->history()->peer;
	const auto chinese = IsChinese();
	if (peer->isUser()) {
		return MenuTitle(
			item->out() ? Sender::PrivateMine : Sender::PrivateTheirs,
			QString(),
			chinese);
	}
	const auto from = item->from();
	const auto name = Broadcast(peer)
		? peer->name()
		: from->isSelf()
		? MeName(chinese)
		: from->isUser()
		? from->asUser()->firstName
		: from->name();
	return MenuTitle(
		Sender::Named,
		name.isEmpty() ? from->shortName() : name,
		chinese);
}

void Start(not_null<HistoryItem*> item, Delegate delegate) {
	const auto peer = item->history()->peer;
	const auto loader = std::make_shared<Loader>(Loader{
		.peer = peer,
		.from = Broadcast(peer) ? nullptr : item->from().get(),
		.delegate = std::move(delegate),
	});
	LoadPage(loader, MsgId(0));
}

} // namespace Luckgram::SelectAll
