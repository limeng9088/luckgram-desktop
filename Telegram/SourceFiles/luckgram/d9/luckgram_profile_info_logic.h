/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <optional>
#include <vector>

// D9 / mobile 07 (profile details): pure rules, no UI or session types, so
// they run in test_luckgram_d9. Same rules as Android ProfileInfoLogic and
// iOS LuckgramProfileInfoLogic.
namespace Luckgram::ProfileInfo {

enum class PeerKind {
	User, // Users and bots.
	LegacyGroup,
	Channel, // Channels and supergroups.
};

// ID as shown and copied: users / bots as is, legacy groups -id,
// channels / supergroups -100id.
[[nodiscard]] QString DisplayId(PeerKind kind, int64 id);

// Link for "Copy link": only users / bots have one (tg://user?id=...),
// empty otherwise.
[[nodiscard]] QString UserLink(PeerKind kind, int64 id);

// One table row: first user id seen at year-month.
struct Point {
	int64 id = 0;
	int monthIndex = 0; // year * 12 + (month - 1)
};

struct Estimate {
	bool before2014 = false;
	int year = 0;
	int month = 0; // 1-12
};

// Parses the bundled table ("id,YYYY-MM" per line, '#' comments).
// Malformed or out-of-order lines are skipped.
[[nodiscard]] std::vector<Point> ParseTable(const QString &text);

// Approximate registration month by linear interpolation between rows.
// Below the first row: before 2014. Newer than the last row, no table or a
// bad id: nullopt, the row is not shown rather than guessed.
[[nodiscard]] std::optional<Estimate> EstimateRegistration(
	const std::vector<Point> &points,
	int64 userId);

// "约 2019 年 10 月" / "about Oct 2019"; "早于 2014 年" / "before 2014";
// empty for nullopt.
[[nodiscard]] QString FormatEstimate(
	const std::optional<Estimate> &estimate,
	bool chinese);

// The table shipped with the app, same file as mobile
// (Android res/raw/luckgram_registration_dates.txt).
[[nodiscard]] QString BundledTableText();

// ParseTable(BundledTableText()), parsed once.
[[nodiscard]] const std::vector<Point> &BundledTable();

} // namespace Luckgram::ProfileInfo
