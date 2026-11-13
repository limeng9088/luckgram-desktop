/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// D7 (mobile 20): 「帮我回复」 in the ✦ menu (registered through 21's
// extension point). Sends the chat's last 20 messages and the draft with
// task=reply; three candidates appear one by one in the panel above the
// field; clicking one replaces the field's text (undoable), never sends.
namespace Luckgram::AI {

// The registration line used by RegisterWritingFeatures().
void RegisterReplyItem();

} // namespace Luckgram::AI
