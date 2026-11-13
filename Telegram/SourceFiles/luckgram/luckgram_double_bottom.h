/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Main {
class Account;
} // namespace Main

// D8 (mobile 11 双重空间): a second local passcode. Unlocking with it shows
// only the accounts picked in 「第二密码下可见的账号」 (account lists, the
// switcher, the tray / dock menus, notifications, the unread badge);
// unlocking with the main passcode shows everything.
//
// Desktop difference: Telegram Desktop's local passcode wraps the local
// data key, so at a cold start the second passcode alone can't open the
// data. The record therefore keeps upstream's local key (never the main
// passcode) sealed with AES-256-GCM under PBKDF2-HMAC-SHA512(second
// passcode, random salt, 100000) - the KDF strength of upstream's passcode
// key - and nothing in plain text. A second passcode that opens the record
// is the proof; Storage::Domain::luckgramStartWithLocalKey() then starts
// with that key. Upstream re-wraps the same local key when the main passcode
// changes, so the record stays valid.
//
// Threat model (as mobile 11, w-1139 review): the second space hides
// accounts in the UI. All accounts share one local key, so someone holding
// the second passcode and a copy of tdata can still decrypt every account's
// local data offline; the main passcode itself is never exposed.
namespace Luckgram::DoubleBottom {

inline constexpr auto kFeatureKey = "double_bottom";

[[nodiscard]] bool HasLocalPasscode();
[[nodiscard]] bool HasSecondPasscode();
// Switch on, a record and the local passcode: unlocking can pick a space.
[[nodiscard]] bool Configured();

// The app was unlocked with the second passcode (runtime only, every
// unlock decides again).
[[nodiscard]] bool Active();
[[nodiscard]] rpl::producer<bool> ActiveValue();
[[nodiscard]] rpl::producer<bool> ActiveChanges();

[[nodiscard]] std::vector<quint64> VisibleUserIds();
void SetVisibleUserIds(const std::vector<quint64> &ids);
[[nodiscard]] rpl::producer<> VisibleChanges();

// Must not appear anywhere right now.
[[nodiscard]] bool IsAccountHidden(not_null<const Main::Account*> account);

// Lock screen. Returns false when the typed text can't be the second
// passcode (feature not set up) - the caller goes on with the main check
// at once. Otherwise done(true) comes when it was the second passcode and
// the app is open in the second space (the domain is started first if the
// app was locked since launch), done(false) when the main check should
// run. done is called on the main thread.
[[nodiscard]] bool TryUnlock(const QString &passcode, Fn<void(bool)> done);
// The main passcode (or the system unlock) opened the app.
void OnMainUnlock();
// After an unlock: closes the windows of hidden accounts and moves the
// primary window off a hidden account.
void ApplySpace();

// Setup (Luckgram 功能 → 双重空间), the domain started. The passcode comes
// checked by the caller (CheckNewSecondPasscode, main verified). done(false)
// when sealing failed.
void SetSecondPasscode(const QString &second, Fn<void(bool ok)> done);
// Forgets the second passcode and the visible list, switch off.
void TurnOff();

} // namespace Luckgram::DoubleBottom
