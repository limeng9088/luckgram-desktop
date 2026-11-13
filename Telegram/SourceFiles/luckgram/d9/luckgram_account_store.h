/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

// D9: Luckgram data that belongs to one account (private notes, keyword
// folders). Kept in the account's own data folder, encrypted with its local
// key like Telegram's own files, never uploaded. Storage::Account::reset()
// (log out) removes it, so a later account in the same slot starts empty.
namespace Luckgram::AccountStore {

[[nodiscard]] QByteArray Read(
	not_null<Main::Session*> session,
	const QString &name);
void Write(
	not_null<Main::Session*> session,
	const QString &name,
	const QByteArray &data);

// Called from Storage::Account::reset(): removes every Luckgram file of the
// account folder.
void ClearAll(const QString &basePath);

} // namespace Luckgram::AccountStore
