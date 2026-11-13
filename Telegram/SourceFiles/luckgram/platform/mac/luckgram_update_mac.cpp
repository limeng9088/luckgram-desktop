/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/platform/luckgram_update_platform.h"

#include "luckgram/update/luckgram_update_logic.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QProcess>

// M2: the update is Luckgram-<version>-<build>-mac.zip with Luckgram.app.
// Prepare() unpacks it next to the download and checks the bundle id and
// the signature; Install() hands over to a small shell script that waits
// for this process to quit, puts the new Luckgram.app where the running
// one is (same path, the old one kept until the new one is in place),
// removes the quarantine attribute and opens it. Chat data is in
// ~/Library/Application Support/Luckgram, not in the bundle.
namespace Luckgram::Update::Platform {
namespace {

constexpr auto kBundleId = "cc.luckgram.desktop";

[[nodiscard]] QString RunningBundle() {
	// .../Luckgram.app/Contents/MacOS
	auto dir = QDir(QCoreApplication::applicationDirPath());
	if (!dir.cdUp() || !dir.cdUp()) {
		return QString();
	}
	const auto path = dir.absolutePath();
	return path.endsWith(u".app"_q) ? path : QString();
}

[[nodiscard]] QString UnpackedDir(const QString &package) {
	return package + u".unpacked"_q;
}

[[nodiscard]] QString UnpackedApp(const QString &package) {
	return UnpackedDir(package) + u"/Luckgram.app"_q;
}

[[nodiscard]] QString Run(const QString &program, const QStringList &args) {
	auto process = QProcess();
	process.start(program, args);
	if (!process.waitForFinished(120 * 1000)
		|| process.exitStatus() != QProcess::NormalExit
		|| process.exitCode() != 0) {
		return QString();
	}
	const auto output = QString::fromUtf8(process.readAllStandardOutput());
	return output.isEmpty() ? u"ok"_q : output.trimmed();
}

} // namespace

QString Name() {
	return u"macos"_q;
}

QString PackageSuffix() {
	return u".zip"_q;
}

bool CanInstall() {
	// Run from the disk image, or moved by Gatekeeper into a read-only
	// App Translocation folder, or no write access (M2 §6): manual install.
	const auto bundle = RunningBundle();
	if (bundle.isEmpty()
		|| bundle.startsWith(u"/Volumes/"_q)
		|| bundle.contains(u"/AppTranslocation/"_q)) {
		return false;
	}
	const auto info = QFileInfo(bundle);
	const auto parent = QFileInfo(info.absolutePath());
	return info.isWritable() && parent.isWritable();
}

bool Prepare(const QString &package) {
	const auto dir = UnpackedDir(package);
	QDir(dir).removeRecursively();
	// Older unpacked updates.
	const auto folder = QFileInfo(package).absoluteDir();
	for (const auto &name : folder.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		if (name.startsWith(QString::fromLatin1(kFilePrefix))
			&& name.endsWith(u".unpacked"_q)) {
			QDir(folder.filePath(name)).removeRecursively();
		}
	}
	if (!QDir().mkpath(dir)
		|| Run(u"/usr/bin/ditto"_q, { u"-x"_q, u"-k"_q, package, dir }).isEmpty()) {
		return false;
	}
	const auto app = UnpackedApp(package);
	const auto id = Run(u"/usr/libexec/PlistBuddy"_q, {
		u"-c"_q,
		u"Print :CFBundleIdentifier"_q,
		app + u"/Contents/Info.plist"_q,
	});
	if (id != QString::fromLatin1(kBundleId)) {
		LOG(("Luckgram Update Error: unexpected bundle id '%1'.").arg(id));
		QDir(dir).removeRecursively();
		return false;
	}
	// Ad-hoc or Developer ID: the bundle must be intact.
	if (Run(u"/usr/bin/codesign"_q, {
			u"--verify"_q,
			u"--deep"_q,
			u"--strict"_q,
			app,
		}).isEmpty()) {
		LOG(("Luckgram Update Error: the new Luckgram.app fails codesign."));
		QDir(dir).removeRecursively();
		return false;
	}
	return true;
}

bool Install(const QString &package) {
	const auto bundle = RunningBundle();
	const auto app = UnpackedApp(package);
	if (bundle.isEmpty() || !QFileInfo(app).isDir()) {
		return false;
	}
	const auto script = QFileInfo(package).absolutePath()
		+ u"/luckgram-update.sh"_q;
	auto file = QFile(script);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	// $1 pid, $2 the running Luckgram.app, $3 the new one.
	file.write(R"(#!/bin/sh
PID="$1"
OLD="$2"
NEW="$3"
[ -n "$PID" ] && [ -n "$OLD" ] && [ -n "$NEW" ] || exit 1
case "$OLD" in *.app) ;; *) exit 1 ;; esac
i=0
while kill -0 "$PID" 2>/dev/null; do
  i=$((i + 1))
  [ "$i" -gt 600 ] && exit 1
  sleep 0.1
done
BAK="${OLD%.app}.luckgram-old.app"
rm -rf "${BAK:?}"
if ! mv "$OLD" "$BAK"; then
  open "$OLD"
  exit 1
fi
if ! ditto "$NEW" "$OLD"; then
  rm -rf "${OLD:?}"
  mv "$BAK" "$OLD"
  open "$OLD"
  exit 1
fi
xattr -dr com.apple.quarantine "$OLD" 2>/dev/null
rm -rf "${BAK:?}" "${NEW:?}"
open "$OLD"
)");
	file.close();
	file.setPermissions(QFileDevice::ReadOwner
		| QFileDevice::WriteOwner
		| QFileDevice::ExeOwner);
	return QProcess::startDetached(u"/bin/sh"_q, {
		script,
		QString::number(QCoreApplication::applicationPid()),
		bundle,
		app,
	});
}

} // namespace Luckgram::Update::Platform
