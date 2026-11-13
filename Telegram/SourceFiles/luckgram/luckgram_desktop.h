/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Ui {
class Checkbox;
} // namespace Ui

// D1: desktop scaffold hooks. Upstream files call into here with one line
// each, so merging new upstream tags stays easy.
namespace Luckgram {

// Cloud language pack used when the system language is Chinese
// (Telegram's official Simplified Chinese translation).
inline constexpr auto kChineseLangPackId = "zh-hans-beta";

// --- Build info (CI passes LUCKGRAM_BUILD_NUMBER / LUCKGRAM_COMMIT). ---
[[nodiscard]] int BuildNumber();
[[nodiscard]] QString CommitHash(); // Short hash, may be empty.
// " (build 12 · abc1234)" appended to the version on the about page.
[[nodiscard]] QString VersionSuffix(bool withCommit);

// --- Working folder. ---
// Name of the file that turns a folder into a portable install: if it is
// next to Luckgram.exe, data (tdata) is kept in that folder. The portable
// zip ships it; the installer does not, so the installed app keeps data in
// %APPDATA%\Luckgram and uninstalling the program keeps chat data.
inline constexpr auto kPortableMarker = "luckgram-portable";
[[nodiscard]] bool IsPortable();

// --- Small settings file (tdata/luckgram.json). ---
[[nodiscard]] bool TgSchemeEnabled();
void SetTgSchemeEnabled(bool enabled);

// --- tg:// association. Off by default; never registered on first run. ---
void RegisterTgScheme();
void UnregisterTgScheme();
// Called at launch instead of upstream's unconditional registration.
void AutoRegisterTgScheme();
// "Open tg:// links with Luckgram" checkbox, both settings variants.
[[nodiscard]] rpl::producer<QString> TgSchemeTitle();
void BindTgSchemeCheckbox(not_null<Ui::Checkbox*> checkbox);

// --- Language. ---
// True once: the system language is Chinese, the user never picked a
// language and Luckgram has not switched before.
[[nodiscard]] bool ShouldSwitchToChinese(
	const QString &currentLangPackId,
	const QString &systemLangCode);
void MarkSwitchedToChinese();

} // namespace Luckgram
