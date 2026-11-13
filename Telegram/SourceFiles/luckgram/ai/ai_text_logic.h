/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

// D7: cutting text to a length limit without splitting a surrogate pair
// (an emoji or a rare CJK character would turn into a broken character).
// QtCore only (luckgram_tests).
namespace Luckgram::AI::Text {

// At most n UTF-16 units from the start; one less when the n-th unit would
// leave a lone high surrogate.
[[nodiscard]] QString SafeLeft(const QString &s, int n);

// At most n UTF-16 units from the end; one less when the first one would be
// a lone low surrogate.
[[nodiscard]] QString SafeRight(const QString &s, int n);

// 18 需求 3: the quick buttons put their prompt prefix in front of what is
// typed, replacing another built-in prefix (Android fillPrefix).
[[nodiscard]] const QStringList &QuickPrefixes(); // zh and en of all four.
[[nodiscard]] QString FillPrefix(const QString &current, const QString &prefix);

} // namespace Luckgram::AI::Text
