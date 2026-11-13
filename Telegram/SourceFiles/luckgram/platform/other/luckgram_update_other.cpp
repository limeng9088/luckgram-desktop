/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/platform/luckgram_update_platform.h"

// Systems without an in-app install (Linux; Windows and macOS have their
// own): the update is only shown and the download page opened.
namespace Luckgram::Update::Platform {

QString Name() {
	return u"linux"_q;
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
