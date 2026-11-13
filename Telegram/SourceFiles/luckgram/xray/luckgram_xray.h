/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/xray/luckgram_xray_logic.h"
#include "mtproto/mtproto_proxy_data.h"

// D12 (mobile 33, Android XrayManager): the built-in Xray core.
//
// The xray executable shipped next to the app (xray.exe / xray, pinned
// version) runs as a child process with every imported node as a
// 127.0.0.1 SOCKS5 inbound (user "lg", a password made per install, a port
// fixed per node). Telegram's proxy list holds those loopback SOCKS5
// proxies, so connections, pings and the online state work as for any
// SOCKS5. Only Luckgram's own traffic, no VPN / TUN, no system proxy.
//
// The config goes to xray over stdin (never written to disk); the nodes,
// ports and password are in the settings-level Luckgram storage (encrypted
// like upstream's proxy list, readable before login and passcode unlock);
// no node detail is logged. A core
// that doesn't start or dies never turns a node into a direct connection:
// the loopback port is just closed and Problem() says why.
namespace Luckgram::Xray {

inline constexpr auto kFeatureKey = "xray_core"; // Default on.

// After Luckgram::Start(): loads the nodes, drops those no longer in
// Telegram's proxy list, starts the core when there are any.
void Start();

// The xray executable is part of this build.
[[nodiscard]] bool Available();
// The switch is on and the core is available.
[[nodiscard]] bool Enabled();
[[nodiscard]] bool Running();
// Why the nodes can't be used now (not in this build / switched off /
// starting / not running: reason); empty while the core runs.
[[nodiscard]] QString Problem();
// Fires when Running() / Problem() may have changed.
[[nodiscard]] rpl::producer<> StateChanges();

// Adds (or finds, by Node::key()) the node and returns the loopback SOCKS5
// proxy to put into Telegram's list; nullopt when no outbound can be built
// or no port is free. The core restarts with it right after.
[[nodiscard]] std::optional<MTP::ProxyData> Register(const Node &node);

// The node behind one of Telegram's proxies (for the list: real address,
// protocol label, the share link), or nullopt for an ordinary proxy.
[[nodiscard]] std::optional<Node> Find(const MTP::ProxyData &proxy);

// Telegram's proxy list changed (the proxies box closed): nodes whose
// proxy is gone are dropped, the core restarted if anything changed.
void Reconcile();

// Restarts the core with the current nodes (stops it when there are none
// or it is switched off).
void Apply();

// Texts (mobile XrayManager.T).
[[nodiscard]] QString CopyLinkText();
[[nodiscard]] QString LinkCopiedText();
[[nodiscard]] QString DeleteText();
[[nodiscard]] QString UnavailableText();

} // namespace Luckgram::Xray
