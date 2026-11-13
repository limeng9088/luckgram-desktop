/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/platform/luckgram_update_platform.h"

#include "luckgram/luckgram_desktop.h"
#include "luckgram/update/luckgram_update_logic.h"

#include <QtCore/QDir>
#include <QtCore/QProcess>

namespace Luckgram::Update::Platform {

QString Name() {
	return u"windows"_q;
}

QString PackageSuffix() {
	return u".exe"_q;
}

bool CanInstall() {
	// D4 §7: the portable zip is never installed over.
	return !IsPortable();
}

bool Prepare(const QString &package) {
	return QFile::exists(package);
}

bool Install(const QString &package) {
	// Per-user Inno Setup installer (no UAC): silent, closes this process
	// if it is still running (CloseApplications=force) and starts the new
	// Luckgram afterwards (/luckgramupdate=1, luckgram.iss).
	return QProcess::startDetached(
		QDir::toNativeSeparators(package),
		InstallerArguments());
}

} // namespace Luckgram::Update::Platform
