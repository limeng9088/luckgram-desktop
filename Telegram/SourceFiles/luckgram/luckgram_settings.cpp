/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_settings.h"

#include "luckgram/luckgram_features.h"
#include "luckgram/luckgram_secure_store.h"
#include "settings.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace Luckgram::Settings {
namespace {

// Same file as D1 (luckgram_desktop.cpp); D1 keeps its own top-level keys.
constexpr auto kFile = "tdata/luckgram.json";
constexpr auto kSettingsKey = "settings";
constexpr auto kRemoteKey = "remote_config";
constexpr auto kRemoteAtKey = "remote_config_at";

struct State {
	bool loaded = false;
	QJsonObject settings;
	QJsonObject remote;
	int64 remoteAt = 0;
	rpl::event_stream<QString> changes;
};

[[nodiscard]] State &Current() {
	static auto result = State();
	return result;
}

[[nodiscard]] QString Path() {
	return cWorkingDir() + QString::fromLatin1(kFile);
}

[[nodiscard]] QJsonObject ReadFile() {
	auto file = QFile(Path());
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return QJsonDocument::fromJson(file.readAll()).object();
}

[[nodiscard]] State &Loaded() {
	auto &state = Current();
	if (!state.loaded) {
		state.loaded = true;
		const auto file = ReadFile();
		state.settings = file.value(QString::fromLatin1(kSettingsKey)).toObject();
		state.remote = file.value(QString::fromLatin1(kRemoteKey)).toObject();
		state.remoteAt = int64(
			file.value(QString::fromLatin1(kRemoteAtKey)).toDouble());
	}
	return state;
}

// Read-modify-write, so the D1 keys in the same file are kept.
void Save() {
	const auto &state = Loaded();
	auto object = ReadFile();
	object.insert(QString::fromLatin1(kSettingsKey), state.settings);
	if (state.remote.isEmpty()) {
		object.remove(QString::fromLatin1(kRemoteKey));
		object.remove(QString::fromLatin1(kRemoteAtKey));
	} else {
		object.insert(QString::fromLatin1(kRemoteKey), state.remote);
		object.insert(
			QString::fromLatin1(kRemoteAtKey),
			double(state.remoteAt));
	}
	auto file = QSaveFile(Path());
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("Luckgram Error: could not write %1").arg(Path()));
		return;
	}
	file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
	file.commit();
}

void Put(const QString &key, QJsonValue value) {
	auto &state = Loaded();
	if (state.settings.value(key) == value) {
		return;
	}
	state.settings.insert(key, value);
	Save();
	state.changes.fire_copy(key);
}

} // namespace

bool GetBool(const QString &key, bool def) {
	const auto value = Loaded().settings.value(key);
	return value.isBool() ? value.toBool() : def;
}

void SetBool(const QString &key, bool value) {
	Put(key, value);
}

QString GetString(const QString &key, const QString &def) {
	const auto value = Loaded().settings.value(key);
	return value.isString() ? value.toString() : def;
}

void SetString(const QString &key, const QString &value) {
	Put(key, value);
}

int64 GetInt(const QString &key, int64 def) {
	const auto value = Loaded().settings.value(key);
	return value.isDouble() ? int64(value.toDouble()) : def;
}

void SetInt(const QString &key, int64 value) {
	Put(key, double(value));
}

bool Has(const QString &key) {
	return Loaded().settings.contains(key);
}

void Remove(const QString &key) {
	auto &state = Loaded();
	if (!state.settings.contains(key)) {
		return;
	}
	state.settings.remove(key);
	Save();
	state.changes.fire_copy(key);
}

QStringList KeysWithPrefix(const QString &prefix) {
	auto result = QStringList();
	for (const auto &key : Loaded().settings.keys()) {
		if (key.startsWith(prefix)) {
			result.push_back(key);
		}
	}
	return result;
}

rpl::producer<QString> Changes() {
	return Current().changes.events();
}

bool IsRemotelyAllowed(const QString &key) {
	// Same as mobile LuckgramRemoteConfig.isAllowed: "features" (per device,
	// computed by the server) first, then values."features.<key>.enabled".
	const auto &remote = Loaded().remote;
	const auto features = remote.value(u"features"_q).toObject();
	const auto direct = features.value(key);
	if (direct.isBool()) {
		return direct.toBool();
	}
	const auto values = remote.value(u"values"_q).toObject();
	const auto flag = values.value(u"features."_q + key + u".enabled"_q);
	return flag.isBool() ? flag.toBool() : true;
}

bool IsLocallyEnabled(const QString &key) {
	const auto feature = Features::Find(key);
	return GetBool(key, feature ? feature->defaultValue : false);
}

bool IsEnabled(const QString &key) {
	return IsRemotelyAllowed(key) && IsLocallyEnabled(key);
}

void SetEnabled(const QString &key, bool enabled) {
	SetBool(key, enabled);
}

rpl::producer<bool> EnabledValue(const QString &key) {
	return rpl::single(QString()) | rpl::then(
		Changes()
	) | rpl::filter([=](const QString &changed) {
		return changed.isEmpty() || changed == key;
	}) | rpl::map([=] {
		return IsEnabled(key);
	}) | rpl::distinct_until_changed();
}

QJsonObject RemoteConfig() {
	return Loaded().remote;
}

int64 RemoteConfigFetchedAt() {
	return Loaded().remoteAt;
}

void SetRemoteConfig(const QJsonObject &config, int64 fetchedAt) {
	auto &state = Loaded();
	state.remote = config;
	state.remoteAt = fetchedAt;
	Save();
	state.changes.fire(QString());
}

double RemoteNumber(const QString &key, double def) {
	const auto values = Loaded().remote.value(u"values"_q).toObject();
	const auto value = values.value(key);
	return value.isDouble() ? value.toDouble() : def;
}

void ClearAll() {
	auto &state = Loaded();
	state.settings = QJsonObject();
	state.remote = QJsonObject();
	state.remoteAt = 0;
	Save();
	SecureStore::Remove(SecureStore::kDeviceToken);
	state.changes.fire(QString());
}

} // namespace Luckgram::Settings
