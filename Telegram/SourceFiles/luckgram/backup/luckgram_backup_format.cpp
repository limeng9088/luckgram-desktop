/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/backup/luckgram_backup_format.h"

#include <QtCore/QDataStream>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <memory>
#include <optional>

namespace Luckgram::Backup {
namespace {

constexpr auto kSaltLength = 16;
constexpr auto kNonceLength = 12;
constexpr auto kTagLength = 16;
constexpr auto kKeyLength = 32;
constexpr auto kAad = "luckgram-backup/1";
constexpr auto kWideIdsTag = ~uint64_t(0);

[[nodiscard]] QString L(const char *latin1) {
	return QString::fromLatin1(latin1);
}

[[nodiscard]] QByteArray Base64(const QByteArray &bytes) {
	return bytes.toBase64(); // Standard alphabet, padded.
}

// Empty and ok == false for anything that isn't strict Base64.
[[nodiscard]] QByteArray FromBase64(const QString &text, bool *ok) {
	auto result = QByteArray::fromBase64Encoding(
		text.toLatin1(),
		QByteArray::Base64Encoding
			| QByteArray::AbortOnBase64DecodingErrors);
	*ok = (result.decodingStatus == QByteArray::Base64DecodingStatus::Ok);
	return *ok ? std::move(result.decoded) : QByteArray();
}

struct CipherContextDeleter {
	void operator()(EVP_CIPHER_CTX *context) const {
		EVP_CIPHER_CTX_free(context);
	}
};
using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, CipherContextDeleter>;

[[nodiscard]] const unsigned char *U(const QByteArray &bytes) {
	return reinterpret_cast<const unsigned char*>(bytes.constData());
}

[[nodiscard]] unsigned char *U(QByteArray &bytes) {
	return reinterpret_cast<unsigned char*>(bytes.data());
}

// AES-256-GCM, ciphertext || tag. Empty on failure.
[[nodiscard]] QByteArray Seal(
		const QByteArray &key,
		const QByteArray &nonce,
		const QByteArray &plaintext) {
	const auto context = CipherContext(EVP_CIPHER_CTX_new());
	const auto aad = QByteArray(kAad);
	auto result = QByteArray(plaintext.size() + kTagLength, Qt::Uninitialized);
	auto length = 0;
	auto finalLength = 0;
	if (!context
		|| EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1
		|| EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) != 1
		|| EVP_EncryptInit_ex(context.get(), nullptr, nullptr, U(key), U(nonce)) != 1
		|| EVP_EncryptUpdate(context.get(), nullptr, &length, U(aad), aad.size()) != 1
		|| EVP_EncryptUpdate(context.get(), U(result), &length, U(plaintext), plaintext.size()) != 1
		|| EVP_EncryptFinal_ex(context.get(), U(result) + length, &finalLength) != 1
		|| EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, kTagLength, U(result) + length + finalLength) != 1) {
		return QByteArray();
	}
	result.resize(length + finalLength + kTagLength);
	return result;
}

// Reverses Seal(); ok == false when the tag doesn't match.
[[nodiscard]] QByteArray Open(
		const QByteArray &key,
		const QByteArray &nonce,
		const QByteArray &sealed,
		bool *ok) {
	*ok = false;
	if (sealed.size() < kTagLength) {
		return QByteArray();
	}
	const auto context = CipherContext(EVP_CIPHER_CTX_new());
	const auto aad = QByteArray(kAad);
	const auto ciphertextSize = int(sealed.size()) - kTagLength;
	auto tag = sealed.mid(ciphertextSize);
	auto result = QByteArray(ciphertextSize + kTagLength, Qt::Uninitialized);
	auto length = 0;
	auto finalLength = 0;
	if (!context
		|| EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1
		|| EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) != 1
		|| EVP_DecryptInit_ex(context.get(), nullptr, nullptr, U(key), U(nonce)) != 1
		|| EVP_DecryptUpdate(context.get(), nullptr, &length, U(aad), aad.size()) != 1
		|| EVP_DecryptUpdate(context.get(), U(result), &length, U(sealed), ciphertextSize) != 1
		|| EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, kTagLength, U(tag)) != 1
		|| EVP_DecryptFinal_ex(context.get(), U(result) + length, &finalLength) != 1) {
		WipeBytes(result);
		return QByteArray();
	}
	result.resize(length + finalLength);
	*ok = true;
	return result;
}

[[nodiscard]] QByteArray Plaintext(const std::vector<Account> &accounts) {
	auto array = QJsonArray();
	for (const auto &account : accounts) {
		auto key = QByteArray(
			reinterpret_cast<const char*>(account.authKey.data()),
			int(account.authKey.size()));
		auto item = QJsonObject();
		item.insert(L("user_id"), QString::number(account.userId));
		item.insert(L("dc_id"), account.dcId);
		item.insert(L("auth_key"), QString::fromLatin1(Base64(key)));
		item.insert(L("server_salt"), QString::number(account.serverSalt));
		item.insert(L("name"), account.name);
		array.push_back(item);
		WipeBytes(key);
	}
	auto root = QJsonObject();
	root.insert(L("accounts"), array);
	return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

[[nodiscard]] Decrypted ParsePlaintext(const QByteArray &plaintext) {
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(plaintext, &error);
	const auto array = document.object().value(L("accounts"));
	if (error.error != QJsonParseError::NoError || !array.isArray()) {
		return { .error = Error::Format };
	}
	auto result = Decrypted();
	for (const auto &value : array.toArray()) {
		const auto item = value.toObject();
		auto keyOk = false;
		auto key = FromBase64(item.value(L("auth_key")).toString(), &keyOk);
		auto userOk = false;
		const auto userId = item.value(L("user_id")).toString().toULongLong(
			&userOk);
		const auto dcId = item.value(L("dc_id")).toInt(0);
		if (!keyOk || key.size() != kAuthKeySize || !userOk || !userId || dcId <= 0) {
			WipeBytes(key);
			Wipe(result.accounts);
			return { .error = Error::Format };
		}
		auto account = Account{
			.userId = userId,
			.dcId = dcId,
			.authKey = std::vector<unsigned char>(U(key), U(key) + key.size()),
			.serverSalt = item.value(L("server_salt")).toString().toLongLong(),
			.name = item.value(L("name")).toString(),
		};
		WipeBytes(key);
		result.accounts.push_back(std::move(account));
	}
	return result;
}

struct Header {
	int iterations = 0;
	QByteArray salt;
	QByteArray nonce;
	QByteArray ciphertext;
};

[[nodiscard]] std::optional<Header> ReadHeader(const QByteArray &file) {
	if (file.isEmpty() || file.size() > kMaxFileSize) {
		return std::nullopt;
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(file, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return std::nullopt;
	}
	const auto json = document.object();
	if (json.value(L("format")).toString() != L(kFormat)
		|| json.value(L("version")).toInt(0) != kVersion
		|| json.value(L("kdf")).toString() != L(kKdf)) {
		return std::nullopt;
	}
	auto result = Header();
	result.iterations = json.value(L("iterations")).toInt(0);
	auto saltOk = false;
	auto nonceOk = false;
	auto ciphertextOk = false;
	result.salt = FromBase64(json.value(L("salt")).toString(), &saltOk);
	result.nonce = FromBase64(json.value(L("nonce")).toString(), &nonceOk);
	result.ciphertext = FromBase64(
		json.value(L("ciphertext")).toString(),
		&ciphertextOk);
	if (!saltOk || !nonceOk || !ciphertextOk
		|| result.iterations < 1
		|| result.iterations > kMaxIterations
		|| result.salt.size() < 8
		|| result.nonce.size() != kNonceLength
		|| result.ciphertext.size() < kTagLength) {
		return std::nullopt;
	}
	return result;
}

} // namespace

void Account::wipe() {
	if (!authKey.empty()) {
		OPENSSL_cleanse(authKey.data(), authKey.size());
	}
}

void WipeKey(std::vector<unsigned char> &key) {
	if (!key.empty()) {
		OPENSSL_cleanse(key.data(), key.size());
	}
}

void Wipe(std::vector<Account> &accounts) {
	for (auto &account : accounts) {
		account.wipe();
	}
}

void WipeBytes(QByteArray &bytes) {
	if (!bytes.isEmpty()) {
		OPENSSL_cleanse(bytes.data(), size_t(bytes.size()));
	}
}

QByteArray DeriveKey(
		const QString &password,
		const QByteArray &salt,
		int iterations) {
	auto utf8 = password.toUtf8();
	auto result = QByteArray(kKeyLength, Qt::Uninitialized);
	const auto ok = PKCS5_PBKDF2_HMAC(
		utf8.constData(),
		utf8.size(),
		U(salt),
		salt.size(),
		iterations,
		EVP_sha256(),
		kKeyLength,
		U(result));
	WipeBytes(utf8);
	return (ok == 1) ? result : QByteArray();
}

QByteArray Encrypt(
		const std::vector<Account> &accounts,
		const QString &password) {
	auto salt = QByteArray(kSaltLength, Qt::Uninitialized);
	auto nonce = QByteArray(kNonceLength, Qt::Uninitialized);
	if (RAND_bytes(U(salt), salt.size()) != 1
		|| RAND_bytes(U(nonce), nonce.size()) != 1) {
		return QByteArray();
	}
	return Encrypt(accounts, password, salt, nonce, kIterations);
}

QByteArray Encrypt(
		const std::vector<Account> &accounts,
		const QString &password,
		const QByteArray &salt,
		const QByteArray &nonce,
		int iterations) {
	auto plaintext = Plaintext(accounts);
	auto key = DeriveKey(password, salt, iterations);
	const auto sealed = key.isEmpty()
		? QByteArray()
		: Seal(key, nonce, plaintext);
	WipeBytes(key);
	WipeBytes(plaintext);
	if (sealed.isEmpty()) {
		return QByteArray();
	}
	auto file = QJsonObject();
	file.insert(L("format"), L(kFormat));
	file.insert(L("version"), kVersion);
	file.insert(L("kdf"), L(kKdf));
	file.insert(L("iterations"), iterations);
	file.insert(L("salt"), QString::fromLatin1(Base64(salt)));
	file.insert(L("nonce"), QString::fromLatin1(Base64(nonce)));
	file.insert(L("ciphertext"), QString::fromLatin1(Base64(sealed)));
	return QJsonDocument(file).toJson(QJsonDocument::Compact);
}

bool LooksLikeBackup(const QByteArray &file) {
	return ReadHeader(file).has_value();
}

Decrypted Decrypt(const QByteArray &file, const QString &password) {
	const auto header = ReadHeader(file);
	if (!header) {
		return { .error = Error::Format };
	}
	auto key = DeriveKey(password, header->salt, header->iterations);
	if (key.isEmpty()) {
		return { .error = Error::Format };
	}
	auto ok = false;
	auto plaintext = Open(key, header->nonce, header->ciphertext, &ok);
	WipeBytes(key);
	if (!ok) {
		return { .error = Error::WrongPassword };
	}
	auto result = ParsePlaintext(plaintext);
	WipeBytes(plaintext);
	return result;
}

int WaitSeconds(int failedAttempts) {
	if (failedAttempts <= 0) {
		return 0;
	}
	return 1 << std::min(failedAttempts - 1, 10);
}

MtpAuthorization ReadMtpAuthorization(const QByteArray &serialized) {
	auto stream = QDataStream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	auto result = MtpAuthorization();
	auto first = quint64();
	stream >> first;
	if (first == kWideIdsTag) {
		auto userId = quint64();
		auto mainDcId = qint32();
		stream >> userId >> mainDcId;
		result.userId = userId;
		result.mainDcId = mainDcId;
	} else {
		// Legacy: qint32 user id, qint32 main dc, read as one quint64.
		result.userId = quint64(quint32(first >> 32));
		result.mainDcId = qint32(quint32(first & 0xFFFFFFFFULL));
	}
	auto count = qint32();
	stream >> count;
	if (stream.status() != QDataStream::Ok || count < 0 || count > 64) {
		return {};
	}
	auto buffer = std::vector<unsigned char>(kAuthKeySize);
	for (auto i = 0; i != count; ++i) {
		auto dcId = qint32();
		stream >> dcId;
		const auto read = stream.readRawData(
			reinterpret_cast<char*>(buffer.data()),
			kAuthKeySize);
		if (stream.status() != QDataStream::Ok || read != kAuthKeySize) {
			OPENSSL_cleanse(buffer.data(), buffer.size());
			result.mainDcKey.clear();
			return result;
		}
		if (dcId == result.mainDcId && result.mainDcKey.empty()) {
			result.mainDcKey = buffer;
		}
	}
	OPENSSL_cleanse(buffer.data(), buffer.size());
	return result;
}

QByteArray WriteMtpAuthorization(
		uint64_t userId,
		int mainDcId,
		const std::vector<unsigned char> &key) {
	auto result = QByteArray();
	{
		auto stream = QDataStream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream
			<< quint64(kWideIdsTag)
			<< quint64(userId)
			<< qint32(mainDcId)
			<< qint32(1) // Keys.
			<< qint32(mainDcId);
		stream.writeRawData(
			reinterpret_cast<const char*>(key.data()),
			int(key.size()));
		stream << qint32(0); // Keys to destroy.
	}
	return result;
}

} // namespace Luckgram::Backup
