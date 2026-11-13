/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Luckgram::Compose {
struct SlotContext;
struct Slot;
} // namespace Luckgram::Compose

// D7 (mobile 21): the ✦ button next to the message field and its menu:
// the five writing effects (polish, more formal, friendlier, shorter, fix
// grammar) with "已润色 · 撤销", plus the items 20 / 24 register in
// ai_writing_registry.h. Registered in luckgram_compose_slots.cpp.
namespace Luckgram::AI {

[[nodiscard]] Compose::Slot CreateWritingSlot(
	const Compose::SlotContext &context);

// Upstream's own AI compose button (ComposeAiButton, shown for long text)
// is hidden while ai_writing is on, so the field has one AI entry (✦);
// with ai_writing off it is back as upstream has it (w-1109).
[[nodiscard]] bool HidesUpstreamAiButton();
[[nodiscard]] rpl::producer<bool> HidesUpstreamAiButtonValue();

// 21 需求 1: the five effects (orders 100..140), from
// RegisterWritingFeatures().
void RegisterWritingEffects();

} // namespace Luckgram::AI
