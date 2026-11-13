/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "settings/settings_common_session.h"

namespace Settings {

// Spec 02: 「Luckgram 功能 → 快捷回复 → 管理」, like mobile
// QuickRepliesManageActivity: add, edit (click), delete, reorder
// (right-click 「上移 / 下移」 instead of dragging).
class LuckgramQuickReplies : public Section<LuckgramQuickReplies> {
public:
	LuckgramQuickReplies(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings
