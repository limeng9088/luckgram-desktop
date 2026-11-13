/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/luckgram_compose_slots.h"

// D6 (spec 01 C): translation before sending. A translate button between
// the input field and the send button (a Luckgram compose slot) while the
// field has text, and 「翻译成…」 / 「翻译」 in the send button's right-click
// menu. The text is translated in place, 「已翻译成 X · 撤销」 lets the user
// take it back. Switch "translate_out".
namespace Luckgram::Translate {

[[nodiscard]] bool OutEnabled();

[[nodiscard]] Compose::Slot CreateSendSlot(
	const Compose::SlotContext &context);

} // namespace Luckgram::Translate
