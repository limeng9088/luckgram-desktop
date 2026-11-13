/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QJsonObject>

// D2 (mobile 00 / S4): Luckgram settings of this device. Stored in
// tdata/luckgram.json (next to the D1 keys, under "settings"), shared by all
// accounts, never synced to the cloud. Every change fires Changes() with the
// key so the UI refreshes live.
namespace Luckgram::Settings {

[[nodiscard]] bool GetBool(const QString &key, bool def);
void SetBool(const QString &key, bool value);
[[nodiscard]] QString GetString(const QString &key, const QString &def = {});
void SetString(const QString &key, const QString &value);
[[nodiscard]] int64 GetInt(const QString &key, int64 def);
void SetInt(const QString &key, int64 value);
[[nodiscard]] bool Has(const QString &key);
void Remove(const QString &key);
[[nodiscard]] QStringList KeysWithPrefix(const QString &prefix);

// Fires the changed key; an empty key means "everything" (remote config
// merged, all cleared).
[[nodiscard]] rpl::producer<QString> Changes();

// Registered switches (luckgram_features.h): on when the user's switch
// (falling back to the registry default) is on and the server did not
// switch the feature off (GET /v1/config, rule of S4 App side).
[[nodiscard]] bool IsEnabled(const QString &key);
[[nodiscard]] bool IsLocallyEnabled(const QString &key);
[[nodiscard]] bool IsRemotelyAllowed(const QString &key);
void SetEnabled(const QString &key, bool enabled);
[[nodiscard]] rpl::producer<bool> EnabledValue(const QString &key);

// Remote config (S4): the last GET /v1/config answer and when it came.
[[nodiscard]] QJsonObject RemoteConfig();
[[nodiscard]] int64 RemoteConfigFetchedAt(); // Unix seconds, 0 = never.
void SetRemoteConfig(const QJsonObject &config, int64 fetchedAt);
// A value from the config's "values" ("ai.transcribe_max_mb"...), or def.
[[nodiscard]] double RemoteNumber(const QString &key, double def);

// Logged out of every account: Luckgram settings and the device token go.
void ClearAll();

} // namespace Luckgram::Settings
