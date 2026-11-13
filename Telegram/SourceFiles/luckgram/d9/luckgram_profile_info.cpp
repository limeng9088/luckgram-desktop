/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/d9/luckgram_profile_info.h"

#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "luckgram/d9/luckgram_profile_info_logic.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

namespace Luckgram::ProfileInfo {
namespace {

[[nodiscard]] PeerKind KindOf(not_null<PeerData*> peer) {
	if (peer->isUser()) {
		return PeerKind::User;
	} else if (peer->isChat()) {
		return PeerKind::LegacyGroup;
	}
	return PeerKind::Channel;
}

[[nodiscard]] int64 BareIdOf(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return int64(peerToUser(user->id).bare);
	} else if (const auto chat = peer->asChat()) {
		return int64(peerToChat(chat->id).bare);
	} else if (const auto channel = peer->asChannel()) {
		return int64(peerToChannel(channel->id).bare);
	}
	return 0;
}

[[nodiscard]] QString IdOf(not_null<PeerData*> peer) {
	return DisplayId(KindOf(peer), BareIdOf(peer));
}

void Copy(
		not_null<Window::SessionController*> controller,
		const QString &text,
		const QString &toast) {
	TextUtilities::SetClipboardText({ text });
	controller->showToast(toast);
}

} // namespace

bool Enabled() {
	return Settings::IsEnabled(QString::fromLatin1(kFeatureKey));
}

rpl::producer<bool> EnabledValue() {
	return Settings::EnabledValue(QString::fromLatin1(kFeatureKey));
}

rpl::producer<QString> IdLabelValue() {
	return rpl::single(u"ID"_q);
}

rpl::producer<TextWithEntities> IdTextValue(not_null<PeerData*> peer) {
	const auto id = IdOf(peer);
	return EnabledValue() | rpl::map([=](bool enabled) {
		return enabled
			? Ui::Text::Link(id, u"internal:luckgram_copy_id"_q)
			: TextWithEntities();
	});
}

void SetupIdLine(
		not_null<Ui::FlatLabel*> label,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	const auto id = IdOf(peer);
	const auto link = UserLink(KindOf(peer), BareIdOf(peer));
	const auto weak = base::make_weak(controller);
	const auto copyId = [=] {
		if (const auto strong = weak.get()) {
			Copy(strong, id, Tr("ID 已复制", "ID copied"));
		}
	};
	label->overrideLinkClickHandler(copyId);
	label->setContextMenuHook([=](Ui::FlatLabel::ContextMenuRequest request) {
		request.menu->addAction(
			Tr("复制 ID", "Copy ID"),
			copyId,
			&st::menuIconCopy);
		if (!link.isEmpty()) {
			request.menu->addAction(Tr("复制链接", "Copy link"), [=] {
				if (const auto strong = weak.get()) {
					Copy(strong, link, Tr("链接已复制", "Link copied"));
				}
			}, &st::menuIconLink);
		}
	});
}

rpl::producer<QString> RegisteredLabelValue() {
	return TrValue("注册时间 · 按 ID 估算", "Registered · estimated from ID");
}

rpl::producer<TextWithEntities> RegisteredTextValue(
		not_null<UserData*> user) {
	const auto bare = int64(peerToUser(user->id).bare);
	return rpl::combine(
		EnabledValue(),
		TrValue("", "")
	) | rpl::map([=](bool enabled, const QString &) {
		if (!enabled || user->isBot()) {
			return TextWithEntities();
		}
		return TextWithEntities{ FormatEstimate(
			EstimateRegistration(BundledTable(), bare),
			IsChinese()) };
	});
}

bool BioLinksEnabled() {
	return Enabled();
}

} // namespace Luckgram::ProfileInfo
