/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

// D2: file naming of the encrypted Luckgram storage (luckgram_storage.h).
// Pure logic, QtCore only, covered by luckgram_storage_tests.cpp.
namespace Luckgram::Storage {

// Telegram's own files have 16-hex-digit names (plus "map", "settings",
// "configs"...), so a prefix with an underscore never collides with them
// or with upstream's legacy file cleanup.
inline constexpr auto kFilePrefix = "luckgram_";
inline constexpr auto kMaxNameLength = 64;

// Names are chosen by the code, never by the user: lowercase latin letters,
// digits, '_' and '-', at most kMaxNameLength, e.g. "translate_cache".
[[nodiscard]] bool IsValidName(const QString &name);

// "translate_cache" -> "luckgram_translate_cache". Empty for a bad name.
[[nodiscard]] QString FileName(const QString &name);

// Whether a file of the folder belongs to Luckgram (any record suffix).
[[nodiscard]] bool IsLuckgramFile(const QString &fileName);

// Every file upstream's FileWriteDescriptor may leave for one record
// (<base>0, <base>1, <base>s), for removal. Empty for a bad name.
[[nodiscard]] QStringList RecordPaths(
	const QString &folder,
	const QString &name);

// The Luckgram files among the names of a folder, as full paths.
[[nodiscard]] QStringList LuckgramPaths(
	const QString &folder,
	const QStringList &fileNames);

// Account level: whether an account's data may be read, written or removed
// now. Needs the local key, and the account must not be hidden in the
// second space (D8). Reads AND writes are refused together: a caller that
// read nothing and writes its empty state back would overwrite the file.
[[nodiscard]] bool AccountUsable(bool hasLocalKey, bool hidden);

} // namespace Luckgram::Storage
