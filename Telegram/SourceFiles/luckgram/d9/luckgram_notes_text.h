/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

// D9 / mobile 05 (private notes): text rules, no UI or session types, so
// they run in test_luckgram_d9. Same rules as Android UserNotesText.
namespace Luckgram::NotesText {

// Counted in user-perceived characters (grapheme clusters, specs#60).
inline constexpr auto kMaxLength = 2000;

[[nodiscard]] int GraphemeCount(const QString &text);
// UTF-16 offset where grapheme cluster number `index` starts, or the size.
[[nodiscard]] int OffsetOfGrapheme(const QString &text, int index);

// Text as stored: trimmed and cut to kMaxLength characters.
// Empty means "no note".
[[nodiscard]] QString Normalize(const QString &note);

// Characters left before the limit (negative when over).
[[nodiscard]] int Remaining(const QString &text);

// First non-empty line, trimmed: the collapsed chat bar and "All notes".
[[nodiscard]] QString FirstLine(const QString &note);

// "All notes" search: an empty query matches everything, otherwise the
// note or the name contains it (case-insensitive).
[[nodiscard]] bool Matches(
	const QString &query,
	const QString &note,
	const QString &name);

// Line under the name in "All notes": the first line containing the query,
// else the first line.
[[nodiscard]] QString Snippet(const QString &note, const QString &query);

} // namespace Luckgram::NotesText
