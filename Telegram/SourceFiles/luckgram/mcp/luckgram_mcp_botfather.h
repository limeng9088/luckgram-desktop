/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/mcp/luckgram_mcp_logic.h"

namespace Main {
class Session;
} // namespace Main

// MCP skill "botfather": the account's own bots, through its chat with
// @BotFather. A tool sends the lines of its script (luckgram_mcp_logic.h,
// BotFatherScript) one after another, waits for the answer to each and
// reports the last one; botfather_press presses a button under a message.
// Only the chat with the real @BotFather (checked by user id) is touched.
namespace Luckgram::Mcp {

void RunBotFather(
	not_null<Main::Session*> session,
	const QString &tool,
	const QJsonObject &arguments,
	ToolDone done);

} // namespace Luckgram::Mcp
