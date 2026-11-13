/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>

// D3 (mobile specs#116 / #117): the exit (public IP + country) the Luckgram
// server sees for a request sent directly or through one SOCKS5 proxy, the
// answer of GET /v1/net/whoami. Pure logic (QtCore only, luckgram_tests),
// same rules and test cases as Android ExitInfo.
namespace Luckgram::ProxyExit {

inline constexpr auto kPath = "/v1/net/whoami";
// Results are kept this long; the same proxy is not asked again within it.
inline constexpr auto kTtlMs = qint64(10 * 60 * 1000);
inline constexpr auto kTimeoutMs = 10 * 1000;
inline constexpr auto kMaxParallel = 4;

struct Info {
	QString ip;
	QString country; // ISO 3166-1 alpha-2 upper case, or empty.
	QString city;
};

// Trims; the country only when it is two letters and not XX / T1.
[[nodiscard]] Info MakeInfo(
	const QString &ip,
	const QString &country,
	const QString &city);

// The whoami JSON; nullopt when it has no usable IP.
[[nodiscard]] std::optional<Info> Parse(const QByteArray &json);

// Regional-indicator flag for a two-letter upper case code, else empty.
[[nodiscard]] QString Flag(const QString &country);

// Country name in Chinese or English, falling back to the code.
[[nodiscard]] QString CountryName(const QString &country, bool zh);

// "🇸🇬 新加坡 · 45.39.75.124", or just the IP when the country is unknown.
[[nodiscard]] QString Label(const Info &info, bool zh);

[[nodiscard]] bool IsFresh(qint64 checkedAtMs, qint64 nowMs);

} // namespace Luckgram::ProxyExit
