/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <array>

// D9 / mobile 08 (storage): the cache limit and keep-media choices, no UI
// or session types, so they run in test_luckgram_d9. Desktop differences
// (spec D9 「桌面端差异」): one keep-media value for the whole cache, no
// per-chat / per-category values, Saved Messages media not exempt.
namespace Luckgram::Cache {

// 1 GB, 2 GB, 5 GB, 10 GB, 20 GB, 50 GB, 不限 (0).
inline constexpr auto kLimitOptionsGb = std::array<int, 7>{
	1, 2, 5, 10, 20, 50, 0 };
// 1 小时, 1 天, 3 天, 1 周, 1 个月, 永久 (0), in seconds.
inline constexpr auto kKeepOptionsSeconds = std::array<int, 6>{
	3600, 86400, 3 * 86400, 7 * 86400, 30 * 86400, 0 };

// The cache database's size limit for an option: 0 = no limit.
[[nodiscard]] int64 LimitBytes(int index);
// The cache database's time limit for an option: 0 = forever.
[[nodiscard]] int KeepSeconds(int index);

// Option closest to a stored value (older builds stored any value).
// 0 (or a value above the largest option) means no limit / forever.
[[nodiscard]] int LimitIndex(int64 bytes);
[[nodiscard]] int KeepIndex(int64 seconds);

[[nodiscard]] QString LimitLabel(int index, bool chinese);
[[nodiscard]] QString KeepLabel(int index, bool chinese);

} // namespace Luckgram::Cache
