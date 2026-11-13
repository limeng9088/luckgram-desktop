/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_strings.h"

#include "lang/lang_instance.h"

namespace Luckgram {

bool IsChinese() {
	const auto id = Lang::GetInstance().id().toLower();
	return id.startsWith(u"zh"_q) || id.contains(u"-zh"_q);
}

QString Tr(const char *zh, const char *en) {
	return IsChinese() ? QString::fromUtf8(zh) : QString::fromUtf8(en);
}

rpl::producer<QString> TrValue(const char *zh, const char *en) {
	return rpl::single(rpl::empty) | rpl::then(
		Lang::GetInstance().updated()
	) | rpl::map([=] {
		return Tr(zh, en);
	});
}

} // namespace Luckgram
