/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace MTP {
struct ProxyData;
} // namespace MTP

// D3 (mobile specs#117): exit IP / country of the direct connection and of
// each SOCKS5 proxy, for the proxy list. GET /v1/net/whoami (specs#116) on
// the Luckgram API entries in order, through exactly that proxy (with its
// user / password) or with no proxy at all for the direct one. At most 4
// checks at once, 10 s each, results kept in memory for 10 minutes; nothing
// on disk, nothing logged. MTProto / HTTP / web proxies can't carry the
// request, they show nothing.
namespace Luckgram::ProxyExit {

// nullptr: the direct connection.
[[nodiscard]] bool Supports(const MTP::ProxyData *proxy);

// Starts a check unless a fresh result or a running check exists (force:
// check again anyway).
void Check(const MTP::ProxyData *proxy, bool force = false);

// The line under a proxy row: 「正在检测出口…」, 「出口检测失败」 or
// 「出口：🇸🇬 新加坡 · 1.2.3.4」; empty when not supported / never checked.
[[nodiscard]] QString Text(const MTP::ProxyData *proxy);

// The direct row: 「直连（不使用代理）」 + the same exit text.
[[nodiscard]] QString DirectTitle();

// Fires after any check finished or started.
[[nodiscard]] rpl::producer<> Updates();

} // namespace Luckgram::ProxyExit
