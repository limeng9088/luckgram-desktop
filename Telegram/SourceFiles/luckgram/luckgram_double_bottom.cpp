/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_double_bottom.h"

#include "core/application.h"
#include "luckgram/luckgram_privacy_logic.h"
#include "luckgram/luckgram_settings.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/storage_domain.h"
#include "window/window_controller.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>

namespace Luckgram::DoubleBottom {
namespace {

constexpr auto kRecordKey = "double_bottom.record";
constexpr auto kVisibleKey = "double_bottom.visible";
constexpr auto kIterations = 100000; // Upstream kStrongIterationsCount.
constexpr auto kKeySize = 32;

using Key = std::array<unsigned char, kKeySize>;

[[nodiscard]] QString Name(const char *key) {
	return QString::fromLatin1(key);
}

[[nodiscard]] rpl::variable<bool> &ActiveState() {
	static auto result = rpl::variable<bool>(false);
	return result;
}

[[nodiscard]] rpl::event_stream<> &VisibleStream() {
	static auto result = rpl::event_stream<>();
	return result;
}

void SetActive(bool active) {
	ActiveState() = active;
}

[[nodiscard]] std::optional<Luckgram::Privacy::SecondRecord> Record() {
	const auto value = Settings::GetString(Name(kRecordKey));
	return value.isEmpty()
		? std::nullopt
		: Luckgram::Privacy::DecodeRecord(value);
}

[[nodiscard]] Key DeriveKey(
		const QByteArray &passcode,
		const QByteArray &salt,
		int iterations) {
	auto result = Key();
	PKCS5_PBKDF2_HMAC(
		passcode.constData(),
		passcode.size(),
		reinterpret_cast<const unsigned char*>(salt.constData()),
		salt.size(),
		iterations,
		EVP_sha512(),
		int(result.size()),
		result.data());
	return result;
}

[[nodiscard]] QByteArray Seal(
		const Key &key,
		const QByteArray &nonce,
		const QByteArray &plain) {
	using namespace Luckgram::Privacy;
	auto result = QByteArray(plain.size() + kTagSize, Qt::Uninitialized);
	const auto out = reinterpret_cast<unsigned char*>(result.data());
	const auto context = EVP_CIPHER_CTX_new();
	if (!context) {
		return {};
	}
	auto length = 0;
	auto written = 0;
	auto ok = (EVP_EncryptInit_ex(
		context,
		EVP_aes_256_gcm(),
		nullptr,
		nullptr,
		nullptr) == 1)
		&& (EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_SET_IVLEN,
			int(nonce.size()),
			nullptr) == 1)
		&& (EVP_EncryptInit_ex(
			context,
			nullptr,
			nullptr,
			key.data(),
			reinterpret_cast<const unsigned char*>(nonce.constData())) == 1);
	if (ok && !plain.isEmpty()) {
		ok = (EVP_EncryptUpdate(
			context,
			out,
			&length,
			reinterpret_cast<const unsigned char*>(plain.constData()),
			int(plain.size())) == 1);
		written = length;
	}
	ok = ok
		&& (EVP_EncryptFinal_ex(context, out + written, &length) == 1)
		&& (EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_GET_TAG,
			kTagSize,
			out + plain.size()) == 1);
	EVP_CIPHER_CTX_free(context);
	return ok ? result : QByteArray();
}

[[nodiscard]] std::optional<QByteArray> Open(
		const Key &key,
		const QByteArray &nonce,
		const QByteArray &sealed) {
	using namespace Luckgram::Privacy;
	if (sealed.size() <= kTagSize) {
		return std::nullopt;
	}
	const auto plainSize = int(sealed.size() - kTagSize);
	auto result = QByteArray(plainSize, Qt::Uninitialized);
	auto tag = sealed.mid(plainSize);
	const auto out = reinterpret_cast<unsigned char*>(result.data());
	const auto context = EVP_CIPHER_CTX_new();
	if (!context) {
		return std::nullopt;
	}
	auto length = 0;
	auto written = 0;
	auto ok = (EVP_DecryptInit_ex(
		context,
		EVP_aes_256_gcm(),
		nullptr,
		nullptr,
		nullptr) == 1)
		&& (EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_SET_IVLEN,
			int(nonce.size()),
			nullptr) == 1)
		&& (EVP_DecryptInit_ex(
			context,
			nullptr,
			nullptr,
			key.data(),
			reinterpret_cast<const unsigned char*>(nonce.constData())) == 1)
		&& (EVP_DecryptUpdate(
			context,
			out,
			&length,
			reinterpret_cast<const unsigned char*>(sealed.constData()),
			plainSize) == 1);
	written = length;
	ok = ok
		&& (EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_SET_TAG,
			kTagSize,
			tag.data()) == 1)
		&& (EVP_DecryptFinal_ex(context, out + written, &length) == 1);
	EVP_CIPHER_CTX_free(context);
	if (!ok) {
		OPENSSL_cleanse(result.data(), result.size());
		return std::nullopt;
	}
	return result;
}

void Cleanse(QByteArray &bytes) {
	if (!bytes.isEmpty()) {
		OPENSSL_cleanse(bytes.data(), bytes.size());
	}
}

void Cleanse(Key &key) {
	OPENSSL_cleanse(key.data(), key.size());
}

[[nodiscard]] Main::Domain *StartedDomain() {
	if (!Core::IsAppLaunched()) {
		return nullptr;
	}
	const auto domain = &Core::App().domain();
	return domain->started() ? domain : nullptr;
}

} // namespace

bool HasLocalPasscode() {
	if (!Core::IsAppLaunched()) {
		return false;
	}
	const auto &domain = Core::App().domain();
	// Before the start the key data isn't read yet; only the lock screen
	// asks then, and it is shown only when a passcode is set.
	return !domain.started() || domain.local().hasLocalPasscode();
}

bool HasSecondPasscode() {
	return Record().has_value();
}

bool Configured() {
	return Settings::IsEnabled(Name(kFeatureKey))
		&& HasSecondPasscode()
		&& HasLocalPasscode()
		&& !VisibleUserIds().empty();
}

bool Active() {
	return ActiveState().current();
}

rpl::producer<bool> ActiveValue() {
	return ActiveState().value();
}

rpl::producer<bool> ActiveChanges() {
	return ActiveState().changes();
}

std::vector<quint64> VisibleUserIds() {
	return Luckgram::Privacy::ParseIds(Settings::GetString(Name(kVisibleKey)));
}

void SetVisibleUserIds(const std::vector<quint64> &ids) {
	const auto value = Luckgram::Privacy::SerializeIds(ids);
	if (value.isEmpty()) {
		Settings::Remove(Name(kVisibleKey));
	} else {
		Settings::SetString(Name(kVisibleKey), value);
	}
	VisibleStream().fire({});
}

rpl::producer<> VisibleChanges() {
	return VisibleStream().events();
}

bool IsAccountHidden(not_null<const Main::Account*> account) {
	if (!Active() || !account->sessionExists()) {
		return false;
	}
	return Luckgram::Privacy::AccountHidden(
		true,
		quint64(account->session().userId().bare),
		VisibleUserIds());
}

bool TryUnlock(const QString &passcode, Fn<void(bool)> done) {
	if (passcode.isEmpty() || !Configured()) {
		return false;
	}
	const auto record = Record();
	if (!record) {
		return false;
	}
	auto typed = passcode.toUtf8();
	crl::async([
			record = *record,
			typed = std::move(typed),
			done = std::move(done)]() mutable {
		auto key = DeriveKey(typed, record.salt, record.iterations);
		Cleanse(typed);
		auto local = Open(key, record.nonce, record.sealed);
		Cleanse(key);
		crl::on_main([
				local = std::move(local),
				done = std::move(done)]() mutable {
			if (!local || Core::Quitting()) {
				done(false);
				return;
			}
			auto &domain = Core::App().domain();
			if (domain.started()) {
				// The record opened: this is the second passcode.
				Cleanse(*local);
				SetActive(true);
				done(true);
				return;
			}
			// Locked since launch: the sealed local key opens the data, the
			// space is set first so nothing of a hidden account shows up.
			SetActive(true);
			const auto started = domain.luckgramStartWithLocalKey(*local);
			Cleanse(*local);
			if (!started) {
				SetActive(false);
			}
			done(started);
		});
	});
	return true;
}

void OnMainUnlock() {
	SetActive(false);
}

void ApplySpace() {
	const auto domain = StartedDomain();
	if (!domain || !Active()) {
		return;
	}
	auto toClose = std::vector<not_null<Window::Controller*>>();
	Core::App().enumerateWindows([&](not_null<Window::Controller*> window) {
		if (!window->isPrimary() && IsAccountHidden(&window->account())) {
			toClose.push_back(window);
		}
	});
	for (const auto window : toClose) {
		window->close();
	}
	if (!IsAccountHidden(&domain->active())) {
		return;
	}
	for (const auto &account : domain->orderedAccounts()) {
		if (account->sessionExists()) { // orderedAccounts() skips hidden.
			domain->activate(account);
			return;
		}
	}
}

void SetSecondPasscode(const QString &second, Fn<void(bool ok)> done) {
	using namespace Luckgram::Privacy;
	const auto domain = StartedDomain();
	auto plain = domain ? domain->local().luckgramLocalKey() : QByteArray();
	if (plain.isEmpty()) {
		done(false);
		return;
	}
	auto salt = QByteArray(kSaltSize, Qt::Uninitialized);
	auto nonce = QByteArray(kNonceSize, Qt::Uninitialized);
	const auto random = (RAND_bytes(
		reinterpret_cast<unsigned char*>(salt.data()),
		salt.size()) == 1)
		&& (RAND_bytes(
			reinterpret_cast<unsigned char*>(nonce.data()),
			nonce.size()) == 1);
	if (!random) {
		done(false);
		return;
	}
	auto typed = second.toUtf8();
	crl::async([=, typed = std::move(typed), plain = std::move(plain)]() mutable {
		auto key = DeriveKey(typed, salt, kIterations);
		Cleanse(typed);
		const auto sealed = Seal(key, nonce, plain);
		Cleanse(key);
		Cleanse(plain);
		crl::on_main([=] {
			if (sealed.isEmpty()) {
				done(false);
				return;
			}
			Settings::SetString(Name(kRecordKey), EncodeRecord({
				.salt = salt,
				.nonce = nonce,
				.sealed = sealed,
				.iterations = kIterations,
			}));
			done(true);
		});
	});
}

void TurnOff() {
	Settings::Remove(Name(kRecordKey));
	Settings::Remove(Name(kVisibleKey));
	Settings::SetEnabled(Name(kFeatureKey), false);
	VisibleStream().fire({});
	SetActive(false);
}

} // namespace Luckgram::DoubleBottom
