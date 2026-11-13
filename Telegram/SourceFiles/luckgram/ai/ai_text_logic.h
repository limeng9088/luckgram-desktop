/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QString>

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

} // namespace Luckgram::AI::Text
