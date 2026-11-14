/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/mcp/luckgram_mcp_cloud.h"

#include "base/random.h"
#include "core/version.h"
#include "luckgram/luckgram_desktop.h"
#include "luckgram/luckgram_secure_store.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/mcp/luckgram_mcp.h"
#include "luckgram/mcp/luckgram_mcp_cloud_client.h"
#include "luckgram/mcp/luckgram_mcp_logic.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QSysInfo>

namespace Luckgram::Mcp::Cloud {
namespace {

constexpr auto kKeyName = "mcp_cloud_key";
constexpr auto kEnabledSetting = "mcp_cloud";
constexpr auto kDeviceSetting = "mcp_cloud_device";
constexpr auto kDeviceNameSetting = "mcp_cloud_device_name";

struct CloudState {
	std::unique_ptr<CloudClient> client;
	QString key;
	bool keyLoaded = false;
	bool started = false;
	rpl::event_stream<> changes;
};

[[nodiscard]] CloudState &State() {
	static auto result = CloudState();
	return result;
}

[[nodiscard]] QByteArray RandomBytes(int size) {
	auto result = QByteArray();
	while (result.size() < size) {
		const auto part = base::RandomValue<uint64>();
		result.append(reinterpret_cast<const char*>(&part), sizeof(part));
	}
	result.truncate(size);
	return result;
}

// Names this copy of the app to the relay; random, made once.
[[nodiscard]] QString DeviceId() {
	const auto key = QString::fromLatin1(kDeviceSetting);
	auto id = Settings::GetString(key);
	if (!IsValidDeviceId(id)) {
		id = QString::fromLatin1(RandomBytes(12).toBase64(
			QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
		Settings::SetString(key, id);
	}
	return id;
}

[[nodiscard]] QString Version() {
	const auto build = BuildNumber();
	return QString::fromLatin1(AppVersionStr)
		+ ((build > 0) ? (u"."_q + QString::number(build)) : QString());
}

void Apply() {
	auto &state = State();
	const auto wanted = state.started
		&& Enabled()
		&& IsValidCloudKey(Key());
	if (wanted && !state.client) {
		state.client = std::make_unique<CloudClient>(
			AppBackend(),
			[] {
				return CloudClient::Config{
					.key = Key(),
					.deviceId = DeviceId(),
					.deviceName = DeviceName(),
					.version = Version(),
				};
			},
			[] { State().changes.fire({}); });
		state.client->start();
		LOG(("Luckgram MCP: cloud on, device %1.").arg(DeviceId()));
	} else if (!wanted && state.client) {
		state.client = nullptr;
		LOG(("Luckgram MCP: cloud off."));
	}
	state.changes.fire({});
}

void StoreKey(const QString &key) {
	auto &state = State();
	state.key = key;
	state.keyLoaded = true;
	SecureStore::Set(kKeyName, key);
	// The connection is made again with the new key.
	if (state.client) {
		state.client = nullptr;
	}
	Apply();
}

} // namespace

void Start() {
	auto &state = State();
	if (state.started) {
		return;
	}
	state.started = true;
	// The client holds sockets: gone before the application object is.
	QObject::connect(
		QCoreApplication::instance(),
		&QCoreApplication::aboutToQuit,
		[] {
			State().started = false;
			State().client = nullptr;
		});
	Apply();
}

bool Enabled() {
	return Settings::GetBool(QString::fromLatin1(kEnabledSetting), false);
}

void SetEnabled(bool enabled) {
	if (enabled && !IsValidCloudKey(Key())) {
		StoreKey(CloudKeyFromBytes(RandomBytes(32)));
	}
	Settings::SetBool(QString::fromLatin1(kEnabledSetting), enabled);
	Apply();
}

QString Key() {
	auto &state = State();
	if (!state.keyLoaded) {
		state.keyLoaded = true;
		state.key = SecureStore::Get(kKeyName);
	}
	return IsValidCloudKey(state.key) ? state.key : QString();
}

bool UseKey(const QString &key) {
	const auto trimmed = key.trimmed();
	if (!IsValidCloudKey(trimmed)) {
		return false;
	}
	StoreKey(trimmed);
	return true;
}

void RegenerateKey() {
	StoreKey(CloudKeyFromBytes(RandomBytes(32)));
	LOG(("Luckgram MCP: cloud key regenerated."));
}

QString DeviceName() {
	const auto custom = Settings::GetString(
		QString::fromLatin1(kDeviceNameSetting)).trimmed();
	if (!custom.isEmpty()) {
		return custom.left(64);
	}
	const auto name = QSysInfo::machineHostName().trimmed();
	return name.isEmpty() ? u"Luckgram Desktop"_q : name.left(64);
}

void SetDeviceName(const QString &name) {
	const auto key = QString::fromLatin1(kDeviceNameSetting);
	const auto trimmed = name.trimmed().left(64);
	if (trimmed.isEmpty()) {
		Settings::Remove(key); // Back to the host name.
	} else {
		Settings::SetString(key, trimmed);
	}
	// Read at the next poll, within half a minute.
	State().changes.fire({});
}

bool Connected() {
	const auto &state = State();
	return state.client && state.client->connected();
}

QString LastError() {
	const auto &state = State();
	return state.client ? state.client->lastError() : QString();
}

rpl::producer<> StateChanges() {
	return State().changes.events();
}

} // namespace Luckgram::Mcp::Cloud
