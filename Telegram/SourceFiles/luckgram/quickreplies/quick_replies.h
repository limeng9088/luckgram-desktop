/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/luckgram_compose_slots.h"

namespace Main {
class Session;
} // namespace Main

// D6 (spec 02): quick replies, like mobile QuickRepliesStore / Sheet.
// Per account, in its encrypted storage (D2), never uploaded; the four
// examples come once. A 「快捷回复」 button between the input field and the
// send button (or in the 「+」 menu of the compact bar) lists them: click
// inserts into the field, right-click sends at once.
namespace Luckgram::QuickReplies {

[[nodiscard]] bool Enabled();

[[nodiscard]] QStringList Items(not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<> Changes(not_null<Main::Session*> session);
// False when over the limit (kMaxCount) or empty after Normalize().
bool Add(not_null<Main::Session*> session, const QString &text);
bool Edit(
	not_null<Main::Session*> session,
	int index,
	const QString &text);
void Remove(not_null<Main::Session*> session, int index);
void Move(not_null<Main::Session*> session, int from, int to);

[[nodiscard]] Compose::Slot CreateSlot(const Compose::SlotContext &context);

} // namespace Luckgram::QuickReplies
