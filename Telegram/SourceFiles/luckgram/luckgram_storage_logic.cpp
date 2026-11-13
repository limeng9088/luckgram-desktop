/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_storage_logic.h"

namespace Luckgram::Storage {
namespace {

[[nodiscard]] QString Prefix() {
	return QString::fromLatin1(kFilePrefix);
}

} // namespace

bool IsValidName(const QString &name) {
	if (name.isEmpty() || name.size() > kMaxNameLength) {
		return false;
	}
	for (const auto ch : name) {
		const auto ok = (ch >= QChar('a') && ch <= QChar('z'))
			|| (ch >= QChar('0') && ch <= QChar('9'))
			|| (ch == QChar('_'))
			|| (ch == QChar('-'));
		if (!ok) {
			return false;
		}
	}
	return true;
}

QString FileName(const QString &name) {
	return IsValidName(name) ? (Prefix() + name) : QString();
}

bool IsLuckgramFile(const QString &fileName) {
	return fileName.startsWith(Prefix())
		&& (fileName.size() > Prefix().size());
}

QStringList RecordPaths(const QString &folder, const QString &name) {
	const auto file = FileName(name);
	if (file.isEmpty()) {
		return {};
	}
	const auto base = folder + file;
	return { base + QChar('0'), base + QChar('1'), base + QChar('s') };
}

QStringList LuckgramPaths(
		const QString &folder,
		const QStringList &fileNames) {
	auto result = QStringList();
	for (const auto &name : fileNames) {
		if (IsLuckgramFile(name)) {
			result.push_back(folder + name);
		}
	}
	return result;
}

QStringList ClearPaths(
		const QString &folder,
		const QStringList &fileNamesOnDisk,
		const QStringList &namesWritten) {
	auto result = LuckgramPaths(folder, fileNamesOnDisk);
	for (const auto &name : namesWritten) {
		result.append(RecordPaths(folder, name));
	}
	result.removeDuplicates();
	return result;
}

bool AccountUsable(bool hasLocalKey, bool hidden) {
	return hasLocalKey && !hidden;
}

} // namespace Luckgram::Storage
