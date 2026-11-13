/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// D7 / mobile 24 (AI 写帖子): 「写帖子」 in the ✦ menu, registered through
// 21's extension point (ai_writing_registry.h) and shown only in channels
// where the user can post. A topic plus the channel's last 20 loaded text
// posts (style examples, sent only with this request, never stored) give a
// streamed draft (/v1/ai/chat, task=post) in the panel above the field; it
// can be edited, made shorter, given more emoji, translated or rewritten -
// always based on the current draft - then posted now or scheduled through
// the field's own send path. Same request texts as Android AiPostText
// (ai_compose_logic.h Post).
namespace Luckgram::AI::Post {

// The registration line in RegisterWritingFeatures().
void Register();

} // namespace Luckgram::AI::Post
