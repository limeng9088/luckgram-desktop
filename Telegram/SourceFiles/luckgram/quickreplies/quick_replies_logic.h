/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <optional>

// D6 (spec 02): quick replies, like mobile QuickRepliesLogic. Pure logic,
// QtCore only, covered by luckgram_quick_replies_tests.cpp.
namespace Luckgram::QuickReplies {

inline constexpr auto kFeature = "quick_replies";
inline constexpr auto kMaxCount = 100;
// In user-perceived characters (grapheme clusters), like mobile and iOS.
inline constexpr auto kMaxLength = 1000;
// 02.2: the list shows this many at once, then scrolls.
inline constexpr auto kVisible = 8;

// Trimmed and cut to kMaxLength graphemes without splitting an emoji;
// nullopt when nothing is left.
[[nodiscard]] std::optional<QString> Normalize(const QString &text);
[[nodiscard]] int GraphemeCount(const QString &text);

[[nodiscard]] bool CanAdd(int currentCount);

// 02.6: templates starting with `query` (after the 「/」), case
// insensitive, in their stored order; an empty query matches all.
[[nodiscard]] QStringList Matching(
	const QStringList &items,
	const QString &query);
// The 「/」 query of the field text, when the whole text is one.
[[nodiscard]] std::optional<QString> SlashQuery(const QString &field);

// Drag to reorder: the item at `from` moved to `to`.
[[nodiscard]] QStringList Move(const QStringList &items, int from, int to);

// 02.4: the four examples, Chinese for a Chinese app language.
[[nodiscard]] QStringList Presets(bool chinese);

// Stored per account (encrypted): {"items":[...],"seeded":true}. A
// missing or broken file is "not seeded" (the examples come once).
struct Data {
	QStringList items;
	bool seeded = false;
};
[[nodiscard]] QByteArray Serialize(const Data &data);
[[nodiscard]] Data Parse(const QByteArray &bytes);
// The first load: the examples once, then whatever the user keeps.
[[nodiscard]] Data Seeded(Data data, bool chinese);

} // namespace Luckgram::QuickReplies
