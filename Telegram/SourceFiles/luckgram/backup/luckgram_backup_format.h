/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <cstdint>
#include <vector>

// D12 (mobile 10 / #150): the .luckgram-backup file, exactly as
// luckgram-specs docs/backup-format.md and Android / iOS
// LuckgramBackupFormat, so files move between desktop and phones:
//
//   {"format":"luckgram-backup","version":1,"kdf":"pbkdf2-sha256",
//    "iterations":600000,"salt":"<b64>","nonce":"<b64>",
//    "ciphertext":"<b64 of AES-256-GCM ciphertext || 16 byte tag>"}
//
// Key: PBKDF2-HMAC-SHA256 of the UTF-8 password, 32 bytes; AAD
// "luckgram-backup/1". Plaintext:
//   {"accounts":[{"user_id":"123","dc_id":2,"auth_key":"<b64, 256 bytes>",
//                 "server_salt":"0","name":"Work"}]}
//
// Pure logic: QtCore and OpenSSL only (luckgram_tests). Auth keys live in
// plain byte vectors that Wipe() clears; nothing here logs.
namespace Luckgram::Backup {

inline constexpr auto kFileExtension = "luckgram-backup";
inline constexpr auto kFormat = "luckgram-backup";
inline constexpr auto kVersion = 1;
inline constexpr auto kKdf = "pbkdf2-sha256";
inline constexpr auto kIterations = 600000;
inline constexpr auto kMaxIterations = 10000000;
inline constexpr auto kMinPasswordLength = 8;
inline constexpr auto kAuthKeySize = 256;
// Bigger files are not backups (a backup of 100 accounts is ~50 KB).
inline constexpr auto kMaxFileSize = 1024 * 1024;

struct Account {
	uint64_t userId = 0;
	int dcId = 0;
	std::vector<unsigned char> authKey; // kAuthKeySize bytes.
	int64_t serverSalt = 0; // Desktop keeps none: 0, the server sends one.
	QString name; // The name on this device, may be empty.

	void wipe();
};

void Wipe(std::vector<Account> &accounts);
void WipeKey(std::vector<unsigned char> &key);

enum class Error {
	None,
	WrongPassword, // Or a damaged file: GCM can't tell them apart.
	Format, // Not a Luckgram backup, or a version this app doesn't know.
};

struct Decrypted {
	std::vector<Account> accounts;
	Error error = Error::None;
};

// Random salt and nonce, kIterations. Empty on a crypto failure.
[[nodiscard]] QByteArray Encrypt(
	const std::vector<Account> &accounts,
	const QString &password);
// Fixed salt / nonce / iterations: tests only.
[[nodiscard]] QByteArray Encrypt(
	const std::vector<Account> &accounts,
	const QString &password,
	const QByteArray &salt,
	const QByteArray &nonce,
	int iterations);
[[nodiscard]] Decrypted Decrypt(
	const QByteArray &file,
	const QString &password);
// Whether the file looks like a backup at all (format and version), before
// spending a second on the key derivation.
[[nodiscard]] bool LooksLikeBackup(const QByteArray &file);

// PBKDF2-HMAC-SHA256, 32 bytes.
[[nodiscard]] QByteArray DeriveKey(
	const QString &password,
	const QByteArray &salt,
	int iterations);

// Wrong-password back-off: 1, 2, 4… seconds after 1, 2, 3… failures (at
// most 1024 seconds).
[[nodiscard]] int WaitSeconds(int failedAttempts);

// --- Telegram Desktop's serialized MTP authorization
// (Main::Account::serializeMtpAuthorization / setMtpAuthorization):
// QDataStream Qt_5_1: quint64 ~0 (wide ids tag), quint64 user id, qint32
// main dc, keys (qint32 count, then qint32 dc + 256 raw bytes each), keys to
// destroy (same).

struct MtpAuthorization {
	uint64_t userId = 0;
	int mainDcId = 0;
	std::vector<unsigned char> mainDcKey; // Empty when not found.
};

// The user id, main dc and that dc's key; empty key if unreadable.
[[nodiscard]] MtpAuthorization ReadMtpAuthorization(
	const QByteArray &serialized);
// One key for the main dc, nothing to destroy.
[[nodiscard]] QByteArray WriteMtpAuthorization(
	uint64_t userId,
	int mainDcId,
	const std::vector<unsigned char> &key);
// Zeroes a QByteArray's own buffer (after detaching it).
void WipeBytes(QByteArray &bytes);

} // namespace Luckgram::Backup
