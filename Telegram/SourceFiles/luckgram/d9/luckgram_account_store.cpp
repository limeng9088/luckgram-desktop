/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/d9/luckgram_account_store.h"

#include "main/main_session.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "storage/storage_account.h"

#include <QtCore/QDir>
#include <QtCore/QFile>

namespace Luckgram::AccountStore {
namespace {

// Telegram's own files have 16-hex-digit names ("map", "config"...), so a
// prefix with an underscore never collides with them.
const auto kPrefix = u"luckgram_"_q;

[[nodiscard]] QString FileName(const QString &name) {
	return kPrefix + name;
}

} // namespace

QByteArray Read(not_null<Main::Session*> session, const QString &name) {
	using namespace Storage::details;
	auto &local = session->local();
	const auto key = local.peekLegacyLocalKey();
	if (!key) {
		return QByteArray();
	}
	auto file = FileReadDescriptor();
	if (!ReadEncryptedFile(file, FileName(name), local.basePath(), key)) {
		return QByteArray();
	}
	auto result = QByteArray();
	file.stream >> result;
	return CheckStreamStatus(file.stream) ? result : QByteArray();
}

void Write(
		not_null<Main::Session*> session,
		const QString &name,
		const QByteArray &data) {
	using namespace Storage::details;
	auto &local = session->local();
	const auto key = local.peekLegacyLocalKey();
	if (!key) {
		return;
	}
	const auto basePath = local.basePath();
	if (!QDir().exists(basePath)) {
		QDir().mkpath(basePath);
	}
	auto encrypted = EncryptedDescriptor(Serialize::bytearraySize(data));
	encrypted.stream << data;
	auto file = FileWriteDescriptor(FileName(name), basePath);
	file.writeEncrypted(encrypted, key);
}

void ClearAll(const QString &basePath) {
	const auto dir = QDir(basePath);
	const auto names = dir.entryList(
		{ kPrefix + u"*"_q },
		QDir::Files | QDir::Hidden);
	for (const auto &name : names) {
		QFile::remove(dir.filePath(name));
	}
}

} // namespace Luckgram::AccountStore
