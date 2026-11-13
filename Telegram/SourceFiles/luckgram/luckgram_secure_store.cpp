/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_secure_store.h"

#include "luckgram/luckgram_desktop.h"
#include "luckgram/luckgram_settings.h"
#include "settings.h"

#include <QtCore/QCryptographicHash>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#elif defined Q_OS_MAC // Q_OS_WIN
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif // Q_OS_WIN || Q_OS_MAC

namespace Luckgram::SecureStore {
namespace {

#if !defined Q_OS_WIN && !defined Q_OS_MAC
[[nodiscard]] QByteArray Obfuscate(QByteArray data) {
	const auto key = QCryptographicHash::hash(
		cWorkingDir().toUtf8(),
		QCryptographicHash::Sha256);
	for (auto i = 0; i != data.size(); ++i) {
		data[i] = char(data[i] ^ key[i % key.size()]);
	}
	return data;
}

[[nodiscard]] QString FallbackKey(const char *name) {
	return u"secure_"_q + QString::fromLatin1(name);
}
#endif // !Q_OS_WIN && !Q_OS_MAC

#ifdef Q_OS_MAC
constexpr auto kService = "Luckgram";

[[nodiscard]] CFMutableDictionaryRef Query(const QString &account) {
	const auto result = CFDictionaryCreateMutable(
		kCFAllocatorDefault,
		0,
		&kCFTypeDictionaryKeyCallBacks,
		&kCFTypeDictionaryValueCallBacks);
	const auto service = QString::fromLatin1(kService).toCFString();
	const auto accountRef = account.toCFString();
	CFDictionarySetValue(result, kSecClass, kSecClassGenericPassword);
	CFDictionarySetValue(result, kSecAttrService, service);
	CFDictionarySetValue(result, kSecAttrAccount, accountRef);
	CFRelease(service);
	CFRelease(accountRef);
	return result;
}
#endif // Q_OS_MAC

} // namespace

QString TargetName(const char *name) {
	auto result = u"Luckgram/"_q + QString::fromLatin1(name);
	if (IsPortable()) {
		const auto hash = QCryptographicHash::hash(
			cWorkingDir().toUtf8(),
			QCryptographicHash::Sha256).toHex().left(12);
		result += u"/"_q + QString::fromLatin1(hash);
	}
	return result;
}

QString Get(const char *name) {
#ifdef Q_OS_WIN
	const auto target = TargetName(name).toStdWString();
	auto credential = PCREDENTIALW(nullptr);
	if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
		return QString();
	}
	const auto result = QString::fromUtf8(
		reinterpret_cast<const char*>(credential->CredentialBlob),
		int(credential->CredentialBlobSize));
	CredFree(credential);
	return result;
#elif defined Q_OS_MAC // Q_OS_WIN
	const auto query = Query(TargetName(name));
	CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
	CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
	auto data = CFTypeRef(nullptr);
	const auto status = SecItemCopyMatching(query, &data);
	CFRelease(query);
	if (status != errSecSuccess || !data) {
		return QString();
	}
	const auto result = QString::fromUtf8(
		QByteArray::fromCFData(static_cast<CFDataRef>(data)));
	CFRelease(data);
	return result;
#else // Q_OS_WIN || Q_OS_MAC
	const auto stored = Settings::GetString(FallbackKey(name));
	return stored.isEmpty()
		? QString()
		: QString::fromUtf8(
			Obfuscate(QByteArray::fromBase64(stored.toLatin1())));
#endif // Q_OS_WIN || Q_OS_MAC
}

void Set(const char *name, const QString &value) {
	if (value.isEmpty()) {
		Remove(name);
		return;
	}
	const auto bytes = value.toUtf8();
#ifdef Q_OS_WIN
	const auto target = TargetName(name).toStdWString();
	const auto user = std::wstring(L"Luckgram");
	auto credential = CREDENTIALW();
	credential.Type = CRED_TYPE_GENERIC;
	credential.TargetName = const_cast<LPWSTR>(target.c_str());
	credential.UserName = const_cast<LPWSTR>(user.c_str());
	credential.CredentialBlobSize = DWORD(bytes.size());
	credential.CredentialBlob = reinterpret_cast<LPBYTE>(
		const_cast<char*>(bytes.constData()));
	credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
	if (!CredWriteW(&credential, 0)) {
		LOG(("Luckgram Error: CredWriteW failed, %1").arg(GetLastError()));
	}
#elif defined Q_OS_MAC // Q_OS_WIN
	Remove(name);
	const auto query = Query(TargetName(name));
	const auto data = bytes.toCFData();
	CFDictionarySetValue(query, kSecValueData, data);
	CFDictionarySetValue(
		query,
		kSecAttrAccessible,
		kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
	const auto status = SecItemAdd(query, nullptr);
	CFRelease(data);
	CFRelease(query);
	if (status != errSecSuccess) {
		LOG(("Luckgram Error: SecItemAdd failed, %1").arg(int(status)));
	}
#else // Q_OS_WIN || Q_OS_MAC
	Settings::SetString(
		FallbackKey(name),
		QString::fromLatin1(Obfuscate(bytes).toBase64()));
#endif // Q_OS_WIN || Q_OS_MAC
}

void Remove(const char *name) {
#ifdef Q_OS_WIN
	const auto target = TargetName(name).toStdWString();
	CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
#elif defined Q_OS_MAC // Q_OS_WIN
	const auto query = Query(TargetName(name));
	SecItemDelete(query);
	CFRelease(query);
#else // Q_OS_WIN || Q_OS_MAC
	Settings::Remove(FallbackKey(name));
#endif // Q_OS_WIN || Q_OS_MAC
}

} // namespace Luckgram::SecureStore
