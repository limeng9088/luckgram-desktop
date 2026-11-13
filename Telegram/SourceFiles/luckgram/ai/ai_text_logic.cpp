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

const QStringList &QuickPrefixes() {
	static const auto result = QStringList{
		QString::fromUtf8("总结一下："),
		QString::fromUtf8("改写一下："),
		QString::fromUtf8("讲解一下："),
		QString::fromUtf8("解这道题："),
		QStringLiteral("Summarize this: "),
		QStringLiteral("Rewrite this: "),
		QStringLiteral("Explain this: "),
		QStringLiteral("Solve this: "),
	};
	return result;
}

QString FillPrefix(const QString &current, const QString &prefix) {
	auto rest = current;
	for (const auto &known : QuickPrefixes()) {
		if (rest.startsWith(known)) {
			rest = rest.mid(known.size());
			break;
		}
	}
	return prefix + rest;
}

} // namespace Luckgram::AI::Text
