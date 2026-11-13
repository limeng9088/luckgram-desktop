/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "settings/settings_common_session.h"

namespace Settings {

// D12 (mobile 10, specs#150): 「Luckgram 功能 → 账号备份」. Pick the
// accounts, 「导出为文件」 (.luckgram-backup), 「从备份恢复」. Same rows and
// texts as Android LuckgramBackupActivity / iOS luckgramBackupController.
class LuckgramBackup : public Section<LuckgramBackup> {
public:
	LuckgramBackup(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings
