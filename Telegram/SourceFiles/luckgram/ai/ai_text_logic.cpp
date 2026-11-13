/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_text_logic.h"

namespace Luckgram::AI::Text {

QString SafeLeft(const QString &s, int n) {
	if (n <= 0) {
		return QString();
	} else if (n >= s.size()) {
		return s;
	} else if (s[n - 1].isHighSurrogate() && s[n].isLowSurrogate()) {
		--n;
	}
	return s.left(n);
}

QString SafeRight(const QString &s, int n) {
	if (n <= 0) {
		return QString();
	} else if (n >= s.size()) {
		return s;
	}
	const auto start = int(s.size()) - n;
	if (s[start].isLowSurrogate() && s[start - 1].isHighSurrogate()) {
		--n;
	}
	return s.right(n);
}

} // namespace Luckgram::AI::Text
