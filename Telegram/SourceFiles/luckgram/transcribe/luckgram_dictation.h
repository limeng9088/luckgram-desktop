/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/luckgram_compose_slots.h"

// D6 (spec 17.5-17.6): voice input, like mobile LuckgramDictationSheet.
// A 「语音输入」 button between the input field and the send button (apart
// from the voice message microphone): AI consent, microphone permission,
// then a box records with live levels until 「停止」, the audio goes to
// POST /v1/ai/transcribe and the text is inserted at the cursor.
// 「语音输入 AI 整理」 (default off) tidies it first (task=writing,
// effect=dictation). Switch "dictation".
namespace Luckgram::Transcribe {

[[nodiscard]] Compose::Slot CreateDictationSlot(
	const Compose::SlotContext &context);

} // namespace Luckgram::Transcribe
