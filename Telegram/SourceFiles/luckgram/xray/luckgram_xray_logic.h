/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

#include <map>
#include <optional>
#include <set>
#include <vector>

// D12 (mobile 33): VLESS / VMess / Trojan / Shadowsocks nodes for the
// built-in Xray core. Same rules as Android XrayLogic / iOS LuckgramXrayLogic:
// every node is kept as one canonical share link
// (scheme://id@host:port?query#name); v2rayN VMess JSON, legacy ss:// and
// Clash / mihomo entries are converted to it.
//
// The desktop runs the xray executable as a child process, which has no
// share link parser (mobile uses libXray's), so the outbound JSON is built
// here too (Outbound()), plus the whole config: one 127.0.0.1 SOCKS5
// inbound with a password per node, routed to that node's outbound.
//
// Pure logic: QtCore only (luckgram_tests). The D3 proxy import
// (luckgram_proxy_import_logic, w-1136) calls LooksLikeLink / Parse /
// FromClash.
namespace Luckgram::Xray {

inline constexpr auto kVless = "vless";
inline constexpr auto kVmess = "vmess";
inline constexpr auto kTrojan = "trojan";
inline constexpr auto kShadowsocks = "ss";

struct Node {
	QString protocol; // vless, vmess, trojan, ss.
	QString host;
	int port = 0;
	QString name;
	QString link; // Canonical share link.

	// The link without its name: a node renamed in a subscription stays the
	// same node ("xray|" + link before '#').
	[[nodiscard]] QString key() const;
};

// "VLESS", "VMess", "Trojan", "SS".
[[nodiscard]] QString Label(const QString &protocol);

// The text starts with one of the four schemes (may still be incomplete).
[[nodiscard]] bool LooksLikeLink(const QString &raw);

// A share link as a node; nullopt when not one of the four or incomplete
// (also legacy VMess alterId > 0 and ss:// with a plugin, Xray has none).
[[nodiscard]] std::optional<Node> Parse(const QString &raw);

// One Clash / mihomo proxy, nested keys flattened with dots
// (ws-opts.path, ws-opts.headers.Host, reality-opts.public-key), as a
// canonical share link; empty when Xray can't use it.
[[nodiscard]] QString FromClash(const std::map<QString, QString> &proxy);

// --- Running the core.

// The Xray outbound for a canonical link (vnext / servers, streamSettings
// with tls / reality and tcp / ws / grpc / http / xhttp / httpupgrade);
// empty for anything it can't build.
[[nodiscard]] QJsonObject Outbound(const QString &link);

struct Run {
	int localPort = 0;
	QJsonObject outbound;
};

// The whole config: inbound "in-N" (socks, 127.0.0.1, user / password, udp)
// -> outbound "out-N"; anything unmatched goes to "block" (blackhole). Log
// level warning, no access log.
[[nodiscard]] QByteArray BuildConfig(
	const std::vector<Run> &runs,
	const QString &user,
	const QString &password);

// n free ports counting up from base (wrapping inside 20000-59999),
// skipping used.
[[nodiscard]] std::vector<int> PickPorts(
	int base,
	const std::set<int> &used,
	int n);

// --- Helpers (exposed for the tests).

// Base64 or base64url, padding optional, as UTF-8 text; nullopt if invalid.
[[nodiscard]] std::optional<QString> Base64Text(const QString &text);
[[nodiscard]] QString Base64UrlNoPad(const QByteArray &data);

} // namespace Luckgram::Xray
