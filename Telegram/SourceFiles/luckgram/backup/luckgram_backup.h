/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/backup/luckgram_backup_format.h"

namespace Main {
class Account;
} // namespace Main

namespace Ui {
class Show;
} // namespace Ui

// D12 (mobile 10, specs#150): account backup to a local .luckgram-backup
// file and restore from one, the same file as on Android and iOS. Free,
// switch account_backup (default on). Auth keys are only held in memory and
// wiped as soon as they are used; only the ciphertext is written to disk;
// nothing about keys is logged.
namespace Luckgram::Backup {

inline constexpr auto kFeatureKey = "account_backup";

// Texts, same as Android LuckgramBackup / iOS LuckgramBackupStrings.
[[nodiscard]] QString TitleText();
[[nodiscard]] QString AboutText();
[[nodiscard]] QString SelectHeaderText();
[[nodiscard]] QString ExportText();
[[nodiscard]] QString RestoreText();
// Follows the language (the login page is built before a language switch).
[[nodiscard]] rpl::producer<QString> RestoreTextValue();
[[nodiscard]] QString ExportHintText();
[[nodiscard]] QString RestoreHintText();
[[nodiscard]] QString WarningText();
[[nodiscard]] QString InfoText();

// The logged-in accounts' keys, in that order (unreadable ones skipped).
// The caller wipes the result.
[[nodiscard]] std::vector<Account> Collect(
	const std::vector<not_null<Main::Account*>> &accounts);

// "Export to file": password twice (with the warning) -> encrypt off the
// main thread -> save dialog (luckgram-yyyyMMdd-HHmm.luckgram-backup).
void StartExport(
	std::shared_ptr<Ui::Show> show,
	std::vector<not_null<Main::Account*>> accounts);

// "Restore from backup": open dialog -> password (1, 2, 4… s back-off) ->
// choose accounts -> restore. Already logged-in accounts are skipped, the
// account limit applies, the first restored account is opened.
void StartRestore(std::shared_ptr<Ui::Show> show);

} // namespace Luckgram::Backup
