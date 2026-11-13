/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

// D10 (mobile 26 需求 3-4, SquareComposeActivity): posting in Square.
// Text (<= 1000), up to 9 photos (long edge <= 2048, re-encoded as JPEG so
// EXIF / GPS are gone) or one video (H.264 / AAC MP4, long edge <= 1280,
// <= square.max_video_seconds, no metadata, first frame as the cover),
// uploaded to the presigned URLs of POST /v1/square/uploads, then
// POST /v1/square/posts. The Square profile comes first.
namespace Luckgram::SquareCompose {

void Open(not_null<Window::SessionController*> controller);

} // namespace Luckgram::SquareCompose
