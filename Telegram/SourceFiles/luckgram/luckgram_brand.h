/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Luckgram {

// Replaces the "Telegram Desktop" product name with "Luckgram" in a raw
// cloud language pack value (before {tag} parsing). Sentences
// about the Telegram service itself ("Telegram account", "Telegram cloud")
// are kept. `key` is the lang key index (tr::lng_*.base).
[[nodiscard]] QString RebrandLangValue(ushort key, QString value);

// Substring replacements only; safe on already parsed values (built-in
// English strings). Also used by RebrandLangValue.
[[nodiscard]] QString RebrandText(QString value);

// True if the text contains CJK ideographs (used to pick zh / en texts
// for keys whose whole sentence Luckgram replaces).
[[nodiscard]] bool LooksChinese(const QString &value);

} // namespace Luckgram
