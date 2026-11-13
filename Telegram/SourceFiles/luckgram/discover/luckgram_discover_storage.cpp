/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/discover/luckgram_discover_storage.h"

#include "core/application.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_storage.h"
#include "main/main_domain.h"

#include <map>

namespace Luckgram::DiscoverStorage {
namespace {

using MemoryKey = std::pair<const Main::Account*, QString>;

[[nodiscard]] std::map<MemoryKey, QString> &Memory() {
	static auto result = std::map<MemoryKey, QString>();
	return result;
}

} // namespace

QString SquareSaved(const QString &tab) {
	return u"square_saved_"_q + tab;
}

Main::Account *Active() {
	if (!Core::IsAppLaunched() || !Core::App().domain().started()) {
		return nullptr;
	}
	return &Core::App().activeAccount();
}

QString Read(
		Main::Account *account,
		const QString &name,
		const QString &legacyKey) {
	if (!account) {
		return QString();
	}
	if (!legacyKey.isEmpty() && Settings::Has(legacyKey)) {
		const auto old = Settings::GetString(legacyKey);
		if (!old.isEmpty()
			&& Storage::ReadAccount(account, name).isEmpty()) {
			Write(account, name, old);
		}
		// The plain copy goes even if only memory could take it.
		Settings::Remove(legacyKey);
	}
	const auto i = Memory().find({ account, name });
	if (i != end(Memory())) {
		return i->second;
	}
	return QString::fromUtf8(Storage::ReadAccount(account, name));
}

void Write(
		Main::Account *account,
		const QString &name,
		const QString &value) {
	if (!account) {
		return;
	}
	const auto key = MemoryKey{ account, name };
	if (value.isEmpty()) {
		Memory().erase(key);
		Storage::RemoveAccount(account, name);
	} else if (Storage::WriteAccount(account, name, value.toUtf8())) {
		Memory().erase(key);
	} else {
		Memory()[key] = value;
	}
}

void ClearMemory() {
	Memory().clear();
}

} // namespace Luckgram::DiscoverStorage
