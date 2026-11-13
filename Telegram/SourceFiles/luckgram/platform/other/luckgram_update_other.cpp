/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/platform/luckgram_update_platform.h"

// Systems without an in-app install (until M2 adds macOS): the update is
// only shown and the download page opened.
namespace Luckgram::Update::Platform {

QString Name() {
#ifdef Q_OS_MAC
	return u"macos"_q;
#else // Q_OS_MAC
	return u"linux"_q;
#endif // Q_OS_MAC
}

QString PackageSuffix() {
	return u".zip"_q;
}

bool CanInstall() {
	return false;
}

bool Prepare(const QString &) {
	return false;
}

bool Install(const QString &) {
	return false;
}

} // namespace Luckgram::Update::Platform
