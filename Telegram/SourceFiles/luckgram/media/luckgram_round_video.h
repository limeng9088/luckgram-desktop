/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

class PeerData;
struct FullReplyTo;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class PopupMenu;
struct PreparedList;
} // namespace Ui

// D11 (mobile 03): 「作为圆形视频发送」 in the 「…」 menu of the send files
// box when exactly one local video is chosen. Opens the circle viewfinder
// (drag, wheel zoom, up to 60 s), converts with Media::Transcode to a
// 400 x 400 H.264 / AAC square (the size of upstream round videos) and
// sends it as a round video message. Cancel stops the conversion; nothing
// is written to disk (the video is converted in memory).
namespace Luckgram::RoundVideo {

void AddSendFilesMenuAction(
	not_null<Ui::PopupMenu*> menu,
	std::shared_ptr<ChatHelpers::Show> show,
	const Ui::PreparedList &list,
	not_null<PeerData*> peer,
	const FullReplyTo &replyTo,
	bool scheduled,
	Fn<void()> closeBox);

} // namespace Luckgram::RoundVideo
