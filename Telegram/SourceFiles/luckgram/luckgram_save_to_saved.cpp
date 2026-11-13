/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_save_to_saved.h"

#include "apiwrap.h"
#include "api/api_common.h"
#include "chat_helpers/share_message_phrase_factory.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_context_menu.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_list_widget.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

namespace Luckgram::SaveToSaved {
namespace {

[[nodiscard]] HistoryItemsList Items(
		not_null<Main::Session*> session,
		const MessageIdsList &ids) {
	auto result = HistoryItemsList();
	result.reserve(ids.size());
	for (const auto &id : ids) {
		if (const auto item = session->data().message(id)) {
			result.push_back(item);
		}
	}
	return result;
}

[[nodiscard]] bool Savable(not_null<HistoryItem*> item) {
	return item->isRegular()
		&& !item->isService()
		&& !item->isSponsored()
		&& item->allowsForward()
		&& !item->history()->peer->isSelf();
}

} // namespace

bool CanSave(
		not_null<Window::SessionController*> controller,
		const MessageIdsList &ids) {
	if (ids.empty()
		|| !Settings::IsEnabled(QString::fromLatin1(kFeature))) {
		return false;
	}
	const auto items = Items(&controller->session(), ids);
	return (items.size() == ids.size())
		&& ranges::all_of(items, Savable);
}

void Save(
		not_null<Window::SessionController*> controller,
		MessageIdsList ids,
		Fn<void()> done) {
	const auto session = &controller->session();
	auto items = Items(session, ids);
	items.erase(
		ranges::remove_if(items, [](not_null<HistoryItem*> item) {
			return !Savable(item);
		}),
		end(items));
	if (items.empty()) {
		return;
	}
	ranges::sort(items, [](
			not_null<HistoryItem*> a,
			not_null<HistoryItem*> b) {
		return a->fullId() < b->fullId();
	});
	const auto self = session->data().history(session->user());
	const auto weak = base::make_weak(controller);
	// ApiWrap splits large forwards into Telegram's 100-message requests.
	session->api().forwardMessages(
		Data::ResolvedForwardDraft{ .items = std::move(items) },
		Api::SendAction(self),
		[=] {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			auto text = TextWithEntities{
				Tr("已保存到收藏夹", "Saved to Saved Messages") + u" · "_q,
			}.append(Ui::Text::Link(Tr("查看", "View")));
			strong->showToast({
				.text = std::move(text),
				.filter = ChatHelpers::ForwardedToSavedMessagesFilter(
					&strong->session()),
			});
		});
	if (done) {
		done();
	}
}

void AddMenuAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		MessageIdsList ids,
		Fn<void()> done) {
	if (!CanSave(controller, ids)) {
		return;
	}
	const auto weak = base::make_weak(controller);
	menu->addAction(Tr("收藏", "Save"), [=] {
		if (const auto strong = weak.get()) {
			Save(strong, ids, done);
		}
	}, &st::menuIconSavedMessages);
}

void AddListMenuAction(
		not_null<Ui::PopupMenu*> menu,
		const HistoryView::ContextMenuRequest &request,
		not_null<HistoryView::ListWidget*> list) {
	const auto controller = list->controller();
	if (!request.selectedItems.empty()) {
		auto ids = MessageIdsList();
		for (const auto &selected : request.selectedItems) {
			ids.push_back(selected.msgId);
		}
		const auto weak = base::make_weak(list);
		AddMenuAction(menu, controller, std::move(ids), [=] {
			if (const auto strong = weak.get()) {
				strong->cancelSelection();
			}
		});
	} else if (const auto item = request.item) {
		const auto asGroup = (request.pointState
			!= HistoryView::PointState::GroupPart);
		AddMenuAction(
			menu,
			controller,
			(asGroup
				? item->history()->owner().itemOrItsGroup(item)
				: MessageIdsList{ 1, item->fullId() }));
	}
}

} // namespace Luckgram::SaveToSaved
