/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Main {
class Account;
class Session;
} // namespace Main

// D2: the one way Luckgram writes user data to disk (docs/desktop.md).
// Everything goes through upstream's EncryptedDescriptor and
// FileWriteDescriptor, like Telegram's own tdata files; never plain JSON.
// Plain on/off switches and preferences without user content may stay in
// tdata/luckgram.json (luckgram_settings.h).
//
// Names: lowercase latin, digits, '_' and '-' (IsValidName), stored as
// "luckgram_<name>". A bad name is refused (Write returns false).
namespace Luckgram::Storage {

// 1) Account level: messages, translations, AI chats, private notes,
// keywords, drafts, call transcripts, hidden chat lists...
//
// Stored in the account's own data folder and encrypted with its local key,
// so with a local passcode nothing is readable before unlock, and the data
// of an account hidden in the second space (D8) is hidden with it.
// Storage::Account::reset() (log out) removes all of it (ClearAccount).
//
// Before the account's local key exists (not started yet), and while the
// account is hidden in the second space (D8), Read returns an empty array,
// Write returns false and Remove does nothing: keep such data in memory only
// and don't drop the saved copy. Data loaded while hidden is empty: load
// again when Luckgram::DoubleBottom::ActiveChanges() fires.
[[nodiscard]] bool AccountAvailable(not_null<Main::Account*> account);
[[nodiscard]] QByteArray ReadAccount(
	not_null<Main::Account*> account,
	const QString &name);
bool WriteAccount(
	not_null<Main::Account*> account,
	const QString &name,
	const QByteArray &data);
void RemoveAccount(not_null<Main::Account*> account, const QString &name);

[[nodiscard]] bool AccountAvailable(not_null<Main::Session*> session);
[[nodiscard]] QByteArray ReadAccount(
	not_null<Main::Session*> session,
	const QString &name);
bool WriteAccount(
	not_null<Main::Session*> session,
	const QString &name,
	const QByteArray &data);
void RemoveAccount(not_null<Main::Session*> session, const QString &name);

// Called from Storage::Account::reset(): every Luckgram file of the folder.
void ClearAccount(const QString &basePath);

// 2) Settings level: data needed before login and before the local
// passcode is entered - proxies, subscription URLs, Xray nodes.
//
// Stored in tdata/ and encrypted with the key of upstream's "settings"
// file, exactly like upstream keeps its proxy list. That key does NOT
// depend on the local passcode: anyone with a copy of tdata/ can read it.
// Only switches, preferences and proxy data (proxies, subscription URLs,
// Xray nodes) go here - never messages, translations, AI chats, notes,
// keywords, drafts or anything else written by or to the user.
[[nodiscard]] bool SettingsAvailable();
[[nodiscard]] QByteArray ReadSettings(const QString &name);
bool WriteSettings(const QString &name, const QByteArray &data);
void RemoveSettings(const QString &name);

} // namespace Luckgram::Storage
