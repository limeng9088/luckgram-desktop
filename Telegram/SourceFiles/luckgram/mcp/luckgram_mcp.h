/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// Local MCP server (「Luckgram 功能 → 本机 MCP 接口」, off by default): an
// AI agent on this computer lists the logged-in accounts and switches the
// active one, the same thing a click in the account switcher does. Nothing
// else is exposed - no chats, no messages, no sending.
//
// Listens on 127.0.0.1 only. Every request needs the access token shown on
// the settings page (kept in the system credential store, never in a file
// in plain text). The protocol and the checks are in luckgram_mcp_logic.h.
namespace Luckgram::Mcp {

// Once at launch (Luckgram::Start). Follows the switch from then on.
void Start();

[[nodiscard]] bool Running();
[[nodiscard]] int Port(); // The port in use, 0 when not running.
// Why it is not running although the switch is on, or empty.
[[nodiscard]] QString LastError();
// Fires when Running() / Port() / LastError() or the token changed.
[[nodiscard]] rpl::producer<> StateChanges();

// Created at the first call.
[[nodiscard]] QString Token();
// The old token stops working at once.
void RegenerateToken();

} // namespace Luckgram::Mcp
