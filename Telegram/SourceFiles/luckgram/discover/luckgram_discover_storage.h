/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Main {
class Account;
} // namespace Main

// D10 user content on disk (square cached pages, names of blocked people,
// the nearby bio and manual location; D11 the call recording target chat)
// goes through D2 Luckgram::Storage at the account level: encrypted with
// the local key, removed on log out.
//
// When the account can't be written (not started yet, or hidden in the
// second space, D8) the value is kept in memory only. Nothing is cached
// past that, so the next read after DoubleBottom::ActiveChanges() already
// sees the right account's data.
namespace Luckgram::DiscoverStorage {

inline constexpr auto kBlockedNames = "discover_blocked_names";
inline constexpr auto kNearbyBio = "nearby_bio";
inline constexpr auto kNearbyManualLocation = "nearby_manual_location";
[[nodiscard]] QString SquareSaved(const QString &tab); // square_saved_<tab>

// Active account or nullptr while the app / domain isn't started.
[[nodiscard]] Main::Account *Active();

// `legacyKey`: the plain tdata/luckgram.json key used before D2 storage;
// its value is moved over once and the plain copy removed.
[[nodiscard]] QString Read(
	Main::Account *account,
	const QString &name,
	const QString &legacyKey = QString());
// Empty value removes the entry.
void Write(Main::Account *account, const QString &name, const QString &value);

// Forgets memory-only values (log out).
void ClearMemory();

} // namespace Luckgram::DiscoverStorage
