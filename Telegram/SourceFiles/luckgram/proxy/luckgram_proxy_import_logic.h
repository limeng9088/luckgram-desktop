/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/xray/luckgram_xray_logic.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>
#include <set>
#include <vector>

// D3 (mobile specs#118): what a pasted / scanned text means for the proxy
// list, and the proxies in a mihomo / Clash subscription. SOCKS5 and MTProto
// go into Telegram's list as they are; VLESS, VMess, Trojan and Shadowsocks
// (mobile 33) are Xray entries handed to the built-in core (D12,
// Luckgram::Xray). Everything else in a subscription counts as skipped.
//
// Pure logic (QtCore only, luckgram_tests), a line-by-line port of Android
// ProxyImportLogic with the same test cases.
namespace Luckgram::ProxyImport {

inline constexpr auto kTypeSocks5 = "socks5";
inline constexpr auto kTypeMtproto = "mtproto";
inline constexpr auto kTypeXray = "xray";

// Largest subscription accepted.
inline constexpr auto kMaxBytes = 2 * 1024 * 1024;

enum class Kind {
	Unknown,
	Proxy,
	Subscription,
};

// One proxy the app can use.
struct Entry {
	QString type;
	QString host;
	int port = 0;
	QString user;
	QString password;
	QString secret;
	std::optional<Xray::Node> node; // kTypeXray only.

	// Identity used to recognise the same proxy across refreshes.
	[[nodiscard]] QString key() const;
};

[[nodiscard]] Entry MakeEntry(
	const QString &type,
	const QString &host,
	int port,
	const QString &user = QString(),
	const QString &password = QString(),
	const QString &secret = QString());
[[nodiscard]] Entry MakeXrayEntry(const Xray::Node &node);

// Proxies found in a subscription, plus how many entries can't be used.
struct Result {
	std::vector<Entry> proxies;
	int skipped = 0;
};

// What a refresh changes.
struct Diff {
	std::vector<Entry> add;
	std::vector<QString> remove;
};

[[nodiscard]] Kind KindOf(const QString &raw);

// Host of an http(s) URL in lower case, or an empty string.
[[nodiscard]] QString HostOf(const QString &url);

// tg://socks|proxy?..., https://t.me/socks|proxy?... (also telegram.me /
// telegram.dog), socks5://[user:pass@]host:port (socks://, socks5h://), or
// a vless:// vmess:// trojan:// ss:// share link. nullopt when it is none
// of these or is incomplete.
[[nodiscard]] std::optional<Entry> ParseLink(const QString &raw);

// A Clash / mihomo YAML config, or a (base64) list of links.
[[nodiscard]] Result ParseSubscription(const QString &body);

// Standard base64 with padding, strict; nullopt when invalid.
[[nodiscard]] std::optional<QByteArray> Base64(const QString &text);

// Proxies now in the subscription that were not imported before are added
// (unless the list has them already); keys imported before that are gone
// are removed. existing: keys of every proxy in the list.
[[nodiscard]] Diff MakeDiff(
	const std::set<QString> &previouslyImported,
	const std::vector<Entry> &now,
	const std::set<QString> &existing);

// Keys a subscription owns after a refresh: what it still lists that it
// owned before or adds now. A proxy added by hand is never owned, so
// deleting the subscription doesn't remove it.
[[nodiscard]] std::set<QString> Owned(
	const std::set<QString> &previouslyOwned,
	const std::vector<Entry> &now,
	const Diff &diff);

} // namespace Luckgram::ProxyImport
