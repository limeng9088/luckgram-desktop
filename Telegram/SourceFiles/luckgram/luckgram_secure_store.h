/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// D2 §6: secrets (the device token) never land on disk in plain text:
// Windows Credential Manager, macOS Keychain. Other platforms (not shipped)
// keep an obfuscated copy in tdata/luckgram.json.
namespace Luckgram::SecureStore {

inline constexpr auto kDeviceToken = "device_token";

[[nodiscard]] QString Get(const char *name);
void Set(const char *name, const QString &value);
void Remove(const char *name);

// Credential Manager target / Keychain account, e.g. "Luckgram/device_token"
// ("Luckgram/device_token/<hash of the folder>" for a portable copy, so two
// copies don't share a token). The installer removes the plain one.
[[nodiscard]] QString TargetName(const char *name);

} // namespace Luckgram::SecureStore
