/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "settings/settings_common_session.h"

namespace Settings {

// 「Luckgram 功能 → 本机 MCP 接口」: where the local MCP server listens,
// copy the address / the access token / a ready client config, and a new
// token. The switch itself is the row on the Luckgram page.
class LuckgramMcp : public Section<LuckgramMcp> {
public:
	LuckgramMcp(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings
