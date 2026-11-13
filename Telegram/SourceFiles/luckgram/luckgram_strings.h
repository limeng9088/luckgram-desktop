/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// D2: Luckgram's own UI texts in Chinese and English. Telegram's cloud
// language packs don't have them, so they follow the current language like
// LuckgramStrings on mobile: Chinese for any zh* language pack, else English.
namespace Luckgram {

[[nodiscard]] bool IsChinese();
[[nodiscard]] QString Tr(const char *zh, const char *en);
// Re-emits when the language changes.
[[nodiscard]] rpl::producer<QString> TrValue(const char *zh, const char *en);

} // namespace Luckgram
