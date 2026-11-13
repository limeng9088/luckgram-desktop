/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_brand.h"

#include "lang_auto.h"

namespace Luckgram {
namespace {

struct Replacement {
	const char16_t *from;
	const char16_t *to;
};

// Longest first, so "Telegram Desktop app" never becomes "Luckgram app app".
constexpr Replacement kReplacements[] = {
	// Built-in English sentences that call the app "official".
	{ u"Welcome to the official Telegram Desktop app.", u"Welcome to Luckgram." },
	{ u"Official free messaging app based on", u"Free messaging app based on" },
	{ u"Telegram Desktop UWP", u"Luckgram" },
	{ u"Telegram Desktop", u"Luckgram" },
	{ u"Telegram桌面版", u"Luckgram" },
	{ u"Telegram 桌面版", u"Luckgram" },
	{ u"Telegram 桌面端", u"Luckgram" },
	{ u"Telegram桌面端", u"Luckgram" },
	{ u"Telegram 电脑版", u"Luckgram" },
	{ u"Nicegram", u"Luckgram" },
};

} // namespace

bool LooksChinese(const QString &value) {
	for (const auto ch : value) {
		const auto code = ch.unicode();
		if (code >= 0x4E00 && code <= 0x9FFF) {
			return true;
		}
	}
	return false;
}

QString RebrandText(QString value) {
	if (!value.contains(u"Telegram"_q, Qt::CaseSensitive)
		&& !value.contains(u"Nicegram"_q, Qt::CaseSensitive)
		&& !value.contains(u"Official free"_q, Qt::CaseSensitive)) {
		return value;
	}
	for (const auto &replacement : kReplacements) {
		value.replace(
			QString::fromUtf16(replacement.from),
			QString::fromUtf16(replacement.to));
	}
	return value;
}

QString RebrandLangValue(ushort key, QString value) {
	// Whole sentences that call the app "official" are replaced.
	if (key == tr::lng_intro_about.base) {
		return LooksChinese(value)
			? u"欢迎使用 Luckgram。\n快速、安全，基于 Telegram。"_q
			: u"Welcome to Luckgram.\nFast and secure, built on Telegram."_q;
	} else if (key == tr::lng_about_text1.base) {
		// Keeps the {api_link} placeholder used by the about box.
		return LooksChinese(value)
			? u"基于 {api_link} 的免费即时通讯应用，\n快速、安全。"_q
			: u"Free messaging app based on {api_link}\nfor speed and security."_q;
	}
	return RebrandText(std::move(value));
}

} // namespace Luckgram
