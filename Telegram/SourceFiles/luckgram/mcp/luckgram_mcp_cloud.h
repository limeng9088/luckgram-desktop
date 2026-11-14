/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// Cloud MCP (「本机 MCP 接口」 page → 云端 MCP, off by default): this app
// connects out to mcp.luckgram.cc with a cloud key, so an AI tool anywhere
// reaches every computer that uses the same key at one address. The same
// tools and the same per-account skill switches as the local server; the
// relay passes requests on and stores no chats.
//
// The cloud key is made here and kept in the system credential store. The
// other computers get it pasted in. Whoever has it can use what is switched
// on, on every computer of the key.
namespace Luckgram::Mcp::Cloud {

// Once at launch. Connects when switched on and a key is there.
void Start();

[[nodiscard]] bool Enabled();
// On: makes a key when there is none.
void SetEnabled(bool enabled);

[[nodiscard]] QString Key(); // Empty when none was made or pasted.
// A key copied from another computer. False when it is not a cloud key.
[[nodiscard]] bool UseKey(const QString &key);
// A new key: the other computers and the AI tools need it again.
void RegenerateKey();

[[nodiscard]] QString DeviceName();
[[nodiscard]] bool Connected();
[[nodiscard]] QString LastError();
[[nodiscard]] rpl::producer<> StateChanges();

} // namespace Luckgram::Mcp::Cloud
