/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_storage.h"

#include "luckgram/luckgram_double_bottom.h"
#include "luckgram/luckgram_storage_logic.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/localstorage.h"
#include "storage/serialize_common.h"
#include "storage/storage_account.h"

#include <QtCore/QDir>

namespace Luckgram::Storage {
namespace {

using namespace ::Storage::details;

// Folder -> names written in this run (ClearPaths).
[[nodiscard]] std::map<QString, QStringList> &Written() {
	static const auto result = new std::map<QString, QStringList>();
	return *result;
}

[[nodiscard]] QByteArray Read(
		const QString &folder,
		const MTP::AuthKeyPtr &key,
		const QString &name) {
	const auto file = FileName(name);
	if (!key || folder.isEmpty() || file.isEmpty()) {
		return QByteArray();
	}
	auto descriptor = FileReadDescriptor();
	if (!ReadEncryptedFile(descriptor, file, folder, key)) {
		return QByteArray();
	}
	auto result = QByteArray();
	descriptor.stream >> result;
	return CheckStreamStatus(descriptor.stream) ? result : QByteArray();
}

[[nodiscard]] bool Write(
		const QString &folder,
		const MTP::AuthKeyPtr &key,
		const QString &name,
		const QByteArray &data) {
	const auto file = FileName(name);
	if (!key || folder.isEmpty() || file.isEmpty()) {
		if (file.isEmpty()) {
			LOG(("Luckgram Storage Error: bad name '%1'.").arg(name));
		}
		return false;
	}
	if (!QDir().exists(folder)) {
		QDir().mkpath(folder);
	}
	auto encrypted = EncryptedDescriptor(Serialize::bytearraySize(data));
	encrypted.stream << data;
	auto descriptor = FileWriteDescriptor(file, folder);
	descriptor.writeEncrypted(encrypted, key);
	auto &written = Written()[folder];
	if (!written.contains(name)) {
		written.push_back(name);
	}
	return true;
}

void Remove(const QString &folder, const QString &name) {
	if (folder.isEmpty()) {
		return;
	}
	// Through the same queue as the writes, so a pending write can't bring
	// the file back after the removal.
	auto paths = RecordPaths(folder, name);
	ScheduleRemoveFiles({ paths.begin(), paths.end() });
}

[[nodiscard]] ::Storage::Account &AccountLocal(
		not_null<Main::Account*> account) {
	return account->local();
}

// D8: while an account is hidden in the second space its data is neither
// read nor written nor removed. Refusing only reads would let a caller
// write its empty in-memory state over the saved file.
[[nodiscard]] MTP::AuthKeyPtr AccountKey(not_null<Main::Account*> account) {
	auto key = AccountLocal(account).peekLegacyLocalKey();
	const auto hidden = Luckgram::DoubleBottom::IsAccountHidden(account);
	return AccountUsable(key != nullptr, hidden) ? key : nullptr;
}

} // namespace

bool AccountAvailable(not_null<Main::Account*> account) {
	return AccountKey(account) != nullptr;
}

QByteArray ReadAccount(
		not_null<Main::Account*> account,
		const QString &name) {
	return Read(AccountLocal(account).basePath(), AccountKey(account), name);
}

bool WriteAccount(
		not_null<Main::Account*> account,
		const QString &name,
		const QByteArray &data) {
	return Write(
		AccountLocal(account).basePath(),
		AccountKey(account),
		name,
		data);
}

void RemoveAccount(not_null<Main::Account*> account, const QString &name) {
	if (AccountAvailable(account)) {
		Remove(AccountLocal(account).basePath(), name);
	}
}

bool AccountAvailable(not_null<Main::Session*> session) {
	return AccountAvailable(&session->account());
}

QByteArray ReadAccount(
		not_null<Main::Session*> session,
		const QString &name) {
	return ReadAccount(&session->account(), name);
}

bool WriteAccount(
		not_null<Main::Session*> session,
		const QString &name,
		const QByteArray &data) {
	return WriteAccount(&session->account(), name, data);
}

void RemoveAccount(not_null<Main::Session*> session, const QString &name) {
	RemoveAccount(&session->account(), name);
}

void ClearAccount(const QString &basePath) {
	if (basePath.isEmpty()) {
		return;
	}
	const auto names = QDir(basePath).entryList(
		QDir::Files | QDir::Hidden | QDir::System);
	auto written = QStringList();
	if (const auto i = Written().find(basePath); i != end(Written())) {
		written = std::move(i->second);
		Written().erase(i);
	}
	// Queued after any pending write of these files, so it wins.
	auto paths = ClearPaths(basePath, names, written);
	ScheduleRemoveFiles({ paths.begin(), paths.end() });
}

bool SettingsAvailable() {
	return ::Local::luckgramSettingsKey() != nullptr;
}

QByteArray ReadSettings(const QString &name) {
	return Read(
		::Local::luckgramSettingsPath(),
		::Local::luckgramSettingsKey(),
		name);
}

bool WriteSettings(const QString &name, const QByteArray &data) {
	return Write(
		::Local::luckgramSettingsPath(),
		::Local::luckgramSettingsKey(),
		name,
		data);
}

void RemoveSettings(const QString &name) {
	Remove(::Local::luckgramSettingsPath(), name);
}

} // namespace Luckgram::Storage
