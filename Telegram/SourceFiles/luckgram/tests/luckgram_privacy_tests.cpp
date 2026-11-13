/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/luckgram_privacy_logic.h"

// D8. The store-restriction cases are the mobile ones (StoreRestrictionsTest:
// ios, android, ios-android, all, all + ios).

namespace {

using namespace Luckgram::Privacy;
using Ids = std::vector<quint64>;

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] Restriction Entry(const char *platform, const char *text) {
	return { U(platform), U(text) };
}

} // namespace

// ---- 13

LUCKGRAM_TEST(PrivacyStoreOnlyPlatforms) {
	LG_CHECK(IsStoreOnly(U("ios")));
	LG_CHECK(IsStoreOnly(U("android")));
	LG_CHECK(IsStoreOnly(U("ios-android")));
	LG_CHECK(IsStoreOnly(U("android-ios")));
	LG_CHECK(IsStoreOnly(U("IOS")));
	LG_CHECK(!IsStoreOnly(U("all")));
	LG_CHECK(!IsStoreOnly(U("android-all")));
	LG_CHECK(!IsStoreOnly(U("ios-all")));
	LG_CHECK(!IsStoreOnly(U("windows"))); // unknown: keep
	LG_CHECK(!IsStoreOnly(QString()));
}

LUCKGRAM_TEST(PrivacyIosOnly) {
	const auto list = std::vector{ Entry("ios", "store rule") };
	LG_CHECK(!Ignore(false, U("ios")));
	LG_CHECK(Ignore(true, U("ios")));
	LG_EQUAL(FirstApplicable(list, false), U("store rule")); // switch off: closed
	LG_CHECK(FirstApplicable(list, true).isEmpty()); // switch on: opens
}

LUCKGRAM_TEST(PrivacyAndroidOnly) {
	const auto list = std::vector{ Entry("android", "store rule") };
	LG_EQUAL(FirstApplicable(list, false), U("store rule"));
	LG_CHECK(FirstApplicable(list, true).isEmpty());
}

LUCKGRAM_TEST(PrivacyIosAndAndroid) {
	const auto list = std::vector{ Entry("ios-android", "store rule") };
	LG_CHECK(Ignore(true, U("ios-android")));
	LG_CHECK(!Ignore(false, U("ios-android")));
	LG_EQUAL(FirstApplicable(list, false), U("store rule"));
	LG_CHECK(FirstApplicable(list, true).isEmpty());
}

LUCKGRAM_TEST(PrivacyAllPlatformsStay) {
	const auto list = std::vector{ Entry("all", "banned") };
	LG_EQUAL(FirstApplicable(list, false), U("banned"));
	LG_EQUAL(FirstApplicable(list, true), U("banned"));
	LG_EQUAL(
		FirstApplicable(std::vector{ Entry("android-all", "banned2") }, true),
		U("banned2"));
}

LUCKGRAM_TEST(PrivacyAllTogetherWithIos) {
	const auto list = std::vector{
		Entry("ios", "store rule"),
		Entry("all", "banned"),
	};
	LG_EQUAL(FirstApplicable(list, false), U("store rule"));
	// Store entries skipped, the all-platform one still blocks.
	LG_EQUAL(FirstApplicable(list, true), U("banned"));
}

LUCKGRAM_TEST(PrivacyOtherPlatformsNeverApply) {
	const auto list = std::vector{
		Entry("ms", "windows store"),
		Entry("windows", "unknown"),
	};
	LG_CHECK(FirstApplicable(list, false).isEmpty());
	LG_CHECK(FirstApplicable(list, true).isEmpty());
}

// ---- 12

LUCKGRAM_TEST(PrivacyIdsRoundTrip) {
	LG_CHECK(ParseIds(U(" 3,1,,x,2,1,0 ")) == (Ids{ 1, 2, 3 }));
	LG_EQUAL(SerializeIds({ 3, 1, 2, 2 }), U("1,2,3"));
	LG_CHECK(SerializeIds({}).isEmpty());
	LG_CHECK(ParseIds(QString()).empty());
	LG_CHECK(ParseIds(U("18446744073709551615"))
		== (Ids{ 18446744073709551615ULL })); // 64-bit peer id
}

LUCKGRAM_TEST(PrivacyWithHidden) {
	const auto added = WithHidden({ 1 }, 2, true);
	const auto removed = WithHidden({ 1, 2 }, 1, false);
	LG_CHECK(added && *added == (Ids{ 1, 2 }));
	LG_CHECK(!WithHidden({ 1 }, 1, true)); // already hidden
	LG_CHECK(removed && *removed == (Ids{ 2 }));
	LG_CHECK(!WithHidden({ 1 }, 5, false)); // not hidden
	LG_CHECK(!WithHidden({}, 0, true)); // no id
}

LUCKGRAM_TEST(PrivacyHiddenNotifications) {
	LG_CHECK(SuppressHiddenNotification(true, true, false)); // default
	LG_CHECK(!SuppressHiddenNotification(true, true, true)); // notify on
	LG_CHECK(!SuppressHiddenNotification(true, false, false)); // not hidden
	LG_CHECK(!SuppressHiddenNotification(false, true, false)); // feature off
}

LUCKGRAM_TEST(PrivacyNameMatches) {
	const auto name = std::vector<QString>{ U("mia"), U("wong") };
	LG_CHECK(NameMatches({ U("mi") }, name));
	LG_CHECK(NameMatches({ U("wo"), U("mia") }, name));
	LG_CHECK(!NameMatches({ U("ia") }, name)); // not a prefix
	LG_CHECK(!NameMatches({ U("mia"), U("x") }, name)); // every word
	LG_CHECK(!NameMatches({}, name)); // empty query
}

// ---- 11

LUCKGRAM_TEST(PrivacyNewSecondPasscode) {
	using E = SecondPasscodeError;
	LG_CHECK(CheckNewSecondPasscode(U("2222"), U("2222"), U("1111")) == E::None);
	LG_CHECK(CheckNewSecondPasscode(QString(), QString(), U("1111")) == E::Empty);
	LG_CHECK(CheckNewSecondPasscode(U("2222"), U("2223"), U("1111"))
		== E::Mismatch);
	LG_CHECK(CheckNewSecondPasscode(U("1111"), U("1111"), U("1111"))
		== E::SameAsMain);
}

LUCKGRAM_TEST(PrivacyAccountHidden) {
	const auto visible = Ids{ 10 };
	LG_CHECK(!AccountHidden(false, 20, visible)); // main space shows all
	LG_CHECK(AccountHidden(true, 20, visible));
	LG_CHECK(!AccountHidden(true, 10, visible));
	LG_CHECK(!AccountHidden(true, 0, visible)); // logged out
}

LUCKGRAM_TEST(PrivacyToggleVisible) {
	const auto added = ToggleVisible({ 10 }, 20);
	const auto removed = ToggleVisible({ 10, 20 }, 10);
	LG_CHECK(added && *added == (Ids{ 10, 20 }));
	LG_CHECK(removed && *removed == (Ids{ 20 }));
	LG_CHECK(!ToggleVisible({ 10 }, 10)); // at least one
	LG_CHECK(!ToggleVisible({ 10 }, 0));
}

LUCKGRAM_TEST(PrivacyRecordRoundTrip) {
	const auto record = SecondRecord{
		.salt = QByteArray(kSaltSize, 's'),
		.nonce = QByteArray(kNonceSize, 'n'),
		.sealed = QByteArray(256 + kTagSize, 'c'), // local key + GCM tag
		.iterations = 100000,
	};
	const auto decoded = DecodeRecord(EncodeRecord(record));
	LG_CHECK(decoded.has_value());
	LG_CHECK(decoded && decoded->salt == record.salt);
	LG_CHECK(decoded && decoded->nonce == record.nonce);
	LG_CHECK(decoded && decoded->sealed == record.sealed);
	LG_CHECK(decoded && decoded->iterations == record.iterations);

	auto wrongVersion = EncodeRecord(record);
	wrongVersion[0] = QChar('9');
	LG_CHECK(!DecodeRecord(wrongVersion));
	auto shortSalt = record;
	shortSalt.salt.chop(1);
	LG_CHECK(!DecodeRecord(EncodeRecord(shortSalt)));
	auto onlyTag = record;
	onlyTag.sealed = QByteArray(kTagSize, 'c');
	LG_CHECK(!DecodeRecord(EncodeRecord(onlyTag))); // no ciphertext
	LG_CHECK(!DecodeRecord(U("1:100000:!!:!!:!!"))); // bad base64
	LG_CHECK(!DecodeRecord(QString()));
	LG_CHECK(decoded && decoded->visibleNonce.isEmpty()); // none yet
}

LUCKGRAM_TEST(PrivacyRecordVisibleList) {
	const auto record = SecondRecord{
		.salt = QByteArray(kSaltSize, 's'),
		.nonce = QByteArray(kNonceSize, 'n'),
		.sealed = QByteArray(256 + kTagSize, 'c'),
		.visibleNonce = QByteArray(kNonceSize, 'v'),
		.visibleSealed = QByteArray(8 + kTagSize, 'w'),
		.iterations = 100000,
	};
	const auto decoded = DecodeRecord(EncodeRecord(record));
	LG_CHECK(decoded && decoded->visibleNonce == record.visibleNonce);
	LG_CHECK(decoded && decoded->visibleSealed == record.visibleSealed);

	auto half = record;
	half.visibleSealed = QByteArray();
	LG_CHECK(!DecodeRecord(EncodeRecord(half))); // both or none

	// Version 1 (before the list moved in) still opens.
	const auto v1 = QStringList{
		U("1"),
		U("100000"),
		QString::fromLatin1(record.salt.toBase64()),
		QString::fromLatin1(record.nonce.toBase64()),
		QString::fromLatin1(record.sealed.toBase64()),
	}.join(QChar(':'));
	const auto old = DecodeRecord(v1);
	LG_CHECK(old && old->sealed == record.sealed);
	LG_CHECK(old && old->visibleNonce.isEmpty());
	LG_CHECK(!DecodeRecord(v1 + U("::"))); // version 1 has 5 parts
}
