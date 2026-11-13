/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "settings/settings_common_session.h"

namespace Settings {

// Spec 01: 「Luckgram 功能 → 消息与语言 → 收到消息翻译」: 翻译成,
// 不翻译的语言. Same rows and wording as mobile TranslateInSettingsActivity.
class LuckgramTranslateIn : public Section<LuckgramTranslateIn> {
public:
	LuckgramTranslateIn(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings
