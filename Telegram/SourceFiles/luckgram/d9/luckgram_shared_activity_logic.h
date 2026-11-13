/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <algorithm>
#include <vector>

// D9 / mobile 16 (activity in shared groups): pure rules, no UI or session
// types, so they run in test_luckgram_d9. Same rules as Android
// SharedActivityLogic and iOS LuckgramSharedActivityLogic.
namespace Luckgram::SharedActivity {

// Messages taken from each common group (messages.search, from_id = user).
inline constexpr auto kPerGroup = 20;
// At most this many messages are shown.
inline constexpr auto kMaxItems = 100;
// At most this many search requests run at the same time.
inline constexpr auto kMaxParallel = 3;
// Results are reused for this long; Refresh reloads.
inline constexpr auto kCacheMs = int64(10 * 60 * 1000);
// Longest FLOOD_WAIT we sit out; anything longer skips that group.
inline constexpr auto kMaxFloodWaitSeconds = 120;
// The whole load gets this long; then whatever arrived is shown as partial.
inline constexpr auto kTimeBudgetMs = int64(15'000);

struct Dated {
	int date = 0;
	int64 sortId = 0;
	int64 index = 0; // Caller's own reference (position in its storage).
};

// Merges the per-group lists, newest first (ties: higher id first),
// keeping at most `limit`.
[[nodiscard]] std::vector<Dated> NewestFirst(
	const std::vector<std::vector<Dated>> &perGroup,
	int limit = kMaxItems);

// Seconds to wait for FLOOD_WAIT_N / FLOOD_PREMIUM_WAIT_N, -1 otherwise.
[[nodiscard]] int FloodWaitSeconds(const QString &errorType);

// Sit out a FLOOD_WAIT only if it ends inside the time budget.
[[nodiscard]] bool CanWaitFlood(int waitSeconds, int64 elapsedMs);

[[nodiscard]] bool IsFresh(int64 loadedAtMs, int64 nowMs);

// "最近消息 · 3 个共同群组" / "Recent messages · 3 groups in common", plus
// the partial note when some groups didn't answer in time. Desktop says
// 「点击」 where mobile says 「点」 (tap).
[[nodiscard]] QString Header(int groupCount, bool partial, bool chinese);

} // namespace Luckgram::SharedActivity
