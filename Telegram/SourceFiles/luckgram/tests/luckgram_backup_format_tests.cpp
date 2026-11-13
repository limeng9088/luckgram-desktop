/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/backup/luckgram_backup_format.h"

// D12: ported from Android LuckgramBackupFormatTest. The vector is
// luckgram-specs docs/testdata/test-vector.luckgram-backup, made by an
// independent implementation (Python hashlib + cryptography, fake keys
// only); Android and iOS open the same file.
namespace {

using namespace Luckgram::Backup;

const char kVector[] =
	"{\"format\":\"luckgram-backup\",\"version\":1,\"kdf\":\"pbkdf2-sha256\",\"iterations\":600000,\"salt\":\"AAECAw"
	"QFBgcICQoLDA0ODw==\",\"nonce\":\"ZGVmZ2hpamtsbW5v\",\"ciphertext\":\"IAFA4M/I5FIOivsZRPu1UqEAxLnlpzVUXHZ"
	"SiCy04u4YOxrx0UgoID4LoeFsCNZLww5dX+KPADawlQI/0804RkTZmk0XxSDcuVjbLEoSakFppk2ijNzX73hFuSUo79F6idO"
	"qAaaDgV4Oe+6ni6TI86hPSCki3g/DqkNqCPXBtZCV0WKo7dJ8fz2lYHMJBtPyktQMzzTLC1GFMUBSd0MWnxlKlT94DWFm5OP"
	"mtWEuigXiMI6zD1Klm7aSOqkhVuIRHEmY8DIdiMe+ndfKiros4Dg3ov4ZwLf+ExcYApYO/dpsiyCVzPLgNQdV1MvS9kCz9NT"
	"sIC9QwIrPpr58D5Ry6sf0TLs76ui1gKbznokZ4b+1cVlZZ4njjgJHf7gSe8rJubMa+E4GCXMSFdhXjVuTgVIj9ILuH2FPoHw"
	"ftUwpeTg14ErSuUw64bvWgWfn/4GMpJ5e2+y9DM4CReQpXqAWYyTKXg9k0Xz8Owbnq4v1BUbCAT9G1gWMyig3jsM9c2UwD1O"
	"/IXIgN1mFx/xPc2i2kMH8lE5jkOIwfRbGNVqVC+eSFZQNN+yUZd6X2N5PQqsy+5tiW4yRwO6+JtxJbyarBVDhJNTzgBaoqcm"
	"kUcIsPPushOD8JP5MvhnCHULnneR7AIY/WRllPF9j+QEWONShluGbvB2HlYEXPBENULuuox2S7puBeuzc4GTSywx+2sztyCZ"
	"e5+X3VD2v+DHa5VCWqwMwipoXuZrPk80GqHmHR1ySFk/JWnMZRpN562vejsrLK+/6Yq2IbqVUtci9KzYZVBxIoOB0u9eTWJ8"
	"rD1YBXMM5XSV3qe/vna8ADfrYw+loj9d1UrEHHbChMKKOD6xnUf5DrBTu4Cs1Dmz2qNjzP3WePJBkF9iQlKNKgnnrJdsEP7x"
	"hPFviDX4Y3EwBTmvkRMvYqY8jEm5jfFABaZAggDEeSHBymf9Js92kNFpeCUXyAKwpUX+vjt6lJ9+/nWwstEHU8FblJCPL135"
	"tosF3JLxRuauVdiUjiXzP/86mq9U7bN7qt3/PU/9yZw38GordIieQ/hfutwLAlNKjGuU1AvmUnYoQyeQ2EGKx/Xrwu9Udd/I"
	"XSH2bTt20oK2OmrksoyXksyvPkhWKmZzGKVebkMtp4h+gDKbJQElg6OWSOwID503vOxxQnH36ZVIlv0lqtU7+RS1zY1WryGp"
	"bFEpufh4=\"}";

const auto kVectorPassword = QString::fromLatin1("luckgram-test-1");

std::vector<unsigned char> Pattern(bool reversed) {
	auto result = std::vector<unsigned char>(256);
	for (auto i = 0; i != 256; ++i) {
		result[i] = (unsigned char)(reversed ? (255 - i) : i);
	}
	return result;
}

QByteArray Bytes(int size, int first) {
	auto result = QByteArray(size, Qt::Uninitialized);
	for (auto i = 0; i != size; ++i) {
		result[i] = char(first + i);
	}
	return result;
}

} // namespace

LUCKGRAM_TEST(BackupPbkdf2MatchesTheReferenceKey) {
	const auto key = DeriveKey(kVectorPassword, Bytes(16, 0), kIterations);
	LG_EQUAL(
		QString::fromLatin1(key.toHex()),
		QString::fromLatin1("30e89dcab6c6af94ec1840fd9fdc6f0dac40d93ac26a56a0533012fcb708df3a"));
}

LUCKGRAM_TEST(BackupOpensTheSharedTestVector) {
	auto decrypted = Decrypt(QByteArray(kVector), kVectorPassword);
	LG_CHECK(decrypted.error == Error::None);
	LG_EQUAL(int(decrypted.accounts.size()), 2);
	const auto &first = decrypted.accounts[0];
	LG_EQUAL(first.userId, uint64_t(1234567890ULL));
	LG_EQUAL(first.dcId, 2);
	LG_EQUAL(first.serverSalt, int64_t(-1234567890123456789LL));
	LG_EQUAL(first.name, QString::fromUtf8("工作"));
	LG_CHECK(first.authKey == Pattern(false));
	const auto &second = decrypted.accounts[1];
	LG_EQUAL(second.userId, uint64_t(987654321ULL));
	LG_EQUAL(second.dcId, 4);
	LG_EQUAL(second.serverSalt, int64_t(0));
	LG_CHECK(second.name.isEmpty());
	LG_CHECK(second.authKey == Pattern(true));
	Wipe(decrypted.accounts);
	LG_CHECK(decrypted.accounts[0].authKey == std::vector<unsigned char>(256, 0));
}

LUCKGRAM_TEST(BackupWrongPasswordIsReported) {
	const auto decrypted = Decrypt(
		QByteArray(kVector),
		QString::fromLatin1("luckgram-test-2"));
	LG_CHECK(decrypted.error == Error::WrongPassword);
	LG_CHECK(decrypted.accounts.empty());
}

LUCKGRAM_TEST(BackupNotABackupIsFormatError) {
	LG_CHECK(!LooksLikeBackup(QByteArray("\x89PNG\r\n")));
	LG_CHECK(LooksLikeBackup(QByteArray(kVector)));
	LG_CHECK(Decrypt(QByteArray("{}"), kVectorPassword).error == Error::Format);
	LG_CHECK(Decrypt(QByteArray(), kVectorPassword).error == Error::Format);
	auto newer = QByteArray(kVector);
	newer.replace("\"version\":1", "\"version\":2");
	LG_CHECK(Decrypt(newer, kVectorPassword).error == Error::Format);
}

LUCKGRAM_TEST(BackupRoundTripWithFewIterations) {
	auto accounts = std::vector<Account>();
	accounts.push_back({
		.userId = 42,
		.dcId = 5,
		.authKey = Pattern(false),
		.serverSalt = 7,
		.name = QString::fromLatin1("Work"),
	});
	const auto file = Encrypt(
		accounts,
		QString::fromLatin1("password-123"),
		QByteArray(16, 0),
		QByteArray(12, 0),
		1000);
	LG_CHECK(!file.isEmpty());
	const auto decoded = Decrypt(file, QString::fromLatin1("password-123"));
	LG_CHECK(decoded.error == Error::None);
	LG_EQUAL(int(decoded.accounts.size()), 1);
	LG_EQUAL(decoded.accounts[0].userId, uint64_t(42));
	LG_EQUAL(decoded.accounts[0].dcId, 5);
	LG_EQUAL(decoded.accounts[0].serverSalt, int64_t(7));
	LG_EQUAL(decoded.accounts[0].name, QString::fromLatin1("Work"));
	LG_CHECK(decoded.accounts[0].authKey == Pattern(false));
}

// Acceptance 4: no account id, key or name readable in the file.
LUCKGRAM_TEST(BackupFileHasNoPlaintext) {
	auto accounts = std::vector<Account>();
	accounts.push_back({
		.userId = 1234567890,
		.dcId = 2,
		.authKey = Pattern(false),
		.serverSalt = 99,
		.name = QString::fromLatin1("Work"),
	});
	const auto file = QString::fromUtf8(
		Encrypt(accounts, QString::fromLatin1("password-123")));
	LG_CHECK(!file.isEmpty());
	LG_CHECK(!file.contains(QString::fromLatin1("1234567890")));
	LG_CHECK(!file.contains(QString::fromLatin1("Work")));
	LG_CHECK(!file.contains(QString::fromLatin1("auth_key")));
	const auto keyBase64 = QString::fromLatin1(QByteArray(
		reinterpret_cast<const char*>(Pattern(false).data()),
		256).toBase64().left(16));
	LG_CHECK(!file.contains(keyBase64));
	LG_CHECK(file.contains(QString::fromLatin1("\"iterations\":600000")));
}

LUCKGRAM_TEST(BackupWaitSeconds) {
	LG_EQUAL(WaitSeconds(0), 0);
	LG_EQUAL(WaitSeconds(1), 1);
	LG_EQUAL(WaitSeconds(2), 2);
	LG_EQUAL(WaitSeconds(3), 4);
	LG_EQUAL(WaitSeconds(50), 1024);
}

LUCKGRAM_TEST(BackupMtpAuthorizationRoundTrip) {
	const auto written = WriteMtpAuthorization(1234567890ULL, 2, Pattern(true));
	const auto read = ReadMtpAuthorization(written);
	LG_EQUAL(read.userId, uint64_t(1234567890ULL));
	LG_EQUAL(read.mainDcId, 2);
	LG_CHECK(read.mainDcKey == Pattern(true));
	// Garbage: no key.
	LG_CHECK(ReadMtpAuthorization(QByteArray("xx")).mainDcKey.empty());
}
