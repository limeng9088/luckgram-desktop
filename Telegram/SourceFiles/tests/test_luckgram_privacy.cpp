/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
// D8: luckgram_privacy_logic. The store-restriction cases are the mobile
// ones (StoreRestrictionsTest: ios, android, ios-android, all, all + ios).
#include "luckgram/luckgram_privacy_logic.h"

#include <cstdio>
#include <vector>

namespace {

using namespace Luckgram::Privacy;

struct Check {
	const char *name = nullptr;
	QString (*run)() = nullptr;
};

[[nodiscard]] QString Expect(bool condition, const char *failure) {
	return condition ? QString() : QString::fromLatin1(failure);
}

[[nodiscard]] QString All(std::initializer_list<QString> results) {
	for (const auto &result : results) {
		if (!result.isEmpty()) {
			return result;
		}
	}
	return QString();
}

[[nodiscard]] Restriction Entry(const char *platform, const char *text) {
	return { QString::fromLatin1(platform), QString::fromLatin1(text) };
}

// ---- 13

QString CheckStoreOnlyPlatforms() {
	return All({
		Expect(IsStoreOnly(u"ios"_q), "ios"),
		Expect(IsStoreOnly(u"android"_q), "android"),
		Expect(IsStoreOnly(u"ios-android"_q), "ios-android"),
		Expect(IsStoreOnly(u"android-ios"_q), "android-ios"),
		Expect(IsStoreOnly(u"IOS"_q), "IOS"),
		Expect(!IsStoreOnly(u"all"_q), "all"),
		Expect(!IsStoreOnly(u"android-all"_q), "android-all"),
		Expect(!IsStoreOnly(u"ios-all"_q), "ios-all"),
		Expect(!IsStoreOnly(u"windows"_q), "unknown platform kept"),
		Expect(!IsStoreOnly(QString()), "empty platform"),
	});
}

QString CheckIosOnly() {
	const auto list = std::vector{ Entry("ios", "store rule") };
	return All({
		Expect(!Ignore(false, u"ios"_q), "switch off keeps ios"),
		Expect(Ignore(true, u"ios"_q), "switch on skips ios"),
		Expect(FirstApplicable(list, false) == u"store rule"_q, "off: closed"),
		Expect(FirstApplicable(list, true).isEmpty(), "on: opens"),
	});
}

QString CheckAndroidOnly() {
	const auto list = std::vector{ Entry("android", "store rule") };
	return All({
		Expect(FirstApplicable(list, false) == u"store rule"_q, "off: closed"),
		Expect(FirstApplicable(list, true).isEmpty(), "on: opens"),
	});
}

QString CheckIosAndAndroid() {
	const auto list = std::vector{ Entry("ios-android", "store rule") };
	return All({
		Expect(Ignore(true, u"ios-android"_q), "on skips ios-android"),
		Expect(!Ignore(false, u"ios-android"_q), "off keeps ios-android"),
		Expect(FirstApplicable(list, false) == u"store rule"_q, "off: closed"),
		Expect(FirstApplicable(list, true).isEmpty(), "on: opens"),
	});
}

QString CheckAllPlatformsStay() {
	const auto list = std::vector{ Entry("all", "banned") };
	const auto mixed = std::vector{ Entry("android-all", "banned2") };
	return All({
		Expect(FirstApplicable(list, false) == u"banned"_q, "off: all"),
		Expect(FirstApplicable(list, true) == u"banned"_q, "on: all stays"),
		Expect(FirstApplicable(mixed, true) == u"banned2"_q, "android-all"),
	});
}

QString CheckAllTogetherWithIos() {
	const auto list = std::vector{
		Entry("ios", "store rule"),
		Entry("all", "banned"),
	};
	return All({
		Expect(FirstApplicable(list, false) == u"store rule"_q, "off: first"),
		Expect(FirstApplicable(list, true) == u"banned"_q, "on: all blocks"),
	});
}

QString CheckOtherPlatformsNeverApply() {
	const auto list = std::vector{
		Entry("ms", "windows store"),
		Entry("windows", "unknown"),
	};
	return All({
		Expect(FirstApplicable(list, false).isEmpty(), "off: not ours"),
		Expect(FirstApplicable(list, true).isEmpty(), "on: not ours"),
	});
}

// ---- 12

QString CheckIdsRoundTrip() {
	const auto ids = ParseIds(u" 3,1,,x,2,1,0 "_q);
	return All({
		Expect(ids == std::vector<quint64>{ 1, 2, 3 }, "parse"),
		Expect(SerializeIds({ 3, 1, 2, 2 }) == u"1,2,3"_q, "serialize"),
		Expect(SerializeIds({}).isEmpty(), "empty"),
		Expect(ParseIds(QString()).empty(), "parse empty"),
		Expect(
			ParseIds(u"18446744073709551615"_q)
				== std::vector<quint64>{ 18446744073709551615ULL },
			"64-bit peer id"),
	});
}

QString CheckWithHidden() {
	const auto added = WithHidden({ 1 }, 2, true);
	const auto removed = WithHidden({ 1, 2 }, 1, false);
	return All({
		Expect(added && *added == std::vector<quint64>{ 1, 2 }, "hide"),
		Expect(!WithHidden({ 1 }, 1, true), "hide twice"),
		Expect(removed && *removed == std::vector<quint64>{ 2 }, "unhide"),
		Expect(!WithHidden({ 1 }, 5, false), "unhide unknown"),
		Expect(!WithHidden({}, 0, true), "zero id"),
	});
}

QString CheckHiddenNotifications() {
	return All({
		Expect(SuppressHiddenNotification(true, true, false), "default"),
		Expect(!SuppressHiddenNotification(true, true, true), "notify on"),
		Expect(!SuppressHiddenNotification(true, false, false), "not hidden"),
		Expect(!SuppressHiddenNotification(false, true, false), "feature off"),
	});
}

QString CheckNameMatches() {
	const auto name = std::vector<QString>{ u"mia"_q, u"wong"_q };
	return All({
		Expect(NameMatches({ u"mi"_q }, name), "prefix"),
		Expect(NameMatches({ u"wo"_q, u"mia"_q }, name), "two words"),
		Expect(!NameMatches({ u"ia"_q }, name), "not a prefix"),
		Expect(!NameMatches({ u"mia"_q, u"x"_q }, name), "all words"),
		Expect(!NameMatches({}, name), "empty query"),
	});
}

// ---- 11

QString CheckNewSecondPasscode() {
	using E = SecondPasscodeError;
	return All({
		Expect(CheckNewSecondPasscode(u"2222"_q, u"2222"_q, u"1111"_q)
			== E::None, "ok"),
		Expect(CheckNewSecondPasscode(QString(), QString(), u"1111"_q)
			== E::Empty, "empty"),
		Expect(CheckNewSecondPasscode(u"2222"_q, u"2223"_q, u"1111"_q)
			== E::Mismatch, "mismatch"),
		Expect(CheckNewSecondPasscode(u"1111"_q, u"1111"_q, u"1111"_q)
			== E::SameAsMain, "same as main"),
	});
}

QString CheckAccountHidden() {
	const auto visible = std::vector<quint64>{ 10 };
	return All({
		Expect(!AccountHidden(false, 20, visible), "main space shows all"),
		Expect(AccountHidden(true, 20, visible), "second hides others"),
		Expect(!AccountHidden(true, 10, visible), "visible one"),
		Expect(!AccountHidden(true, 0, visible), "logged out account"),
	});
}

QString CheckToggleVisible() {
	const auto added = ToggleVisible({ 10 }, 20);
	const auto removed = ToggleVisible({ 10, 20 }, 10);
	return All({
		Expect(added && *added == std::vector<quint64>{ 10, 20 }, "add"),
		Expect(removed && *removed == std::vector<quint64>{ 20 }, "remove"),
		Expect(!ToggleVisible({ 10 }, 10), "at least one"),
		Expect(!ToggleVisible({ 10 }, 0), "no user"),
	});
}

QString CheckRecord() {
	const auto record = SecondRecord{
		.salt = QByteArray(kSaltSize, 's'),
		.nonce = QByteArray(kNonceSize, 'n'),
		.sealed = QByteArray(kTagSize + 4, 'c'),
		.iterations = 200000,
	};
	const auto decoded = DecodeRecord(EncodeRecord(record));
	auto wrongVersion = EncodeRecord(record);
	wrongVersion[0] = QChar('9');
	auto shortSalt = record;
	shortSalt.salt.chop(1);
	auto onlyTag = record;
	onlyTag.sealed = QByteArray(kTagSize, 'c');
	return All({
		Expect(decoded.has_value(), "round trip"),
		Expect(decoded && decoded->salt == record.salt
			&& decoded->nonce == record.nonce
			&& decoded->sealed == record.sealed
			&& decoded->iterations == record.iterations, "same fields"),
		Expect(!DecodeRecord(wrongVersion), "version"),
		Expect(!DecodeRecord(EncodeRecord(shortSalt)), "salt size"),
		Expect(!DecodeRecord(EncodeRecord(onlyTag)), "no ciphertext"),
		Expect(!DecodeRecord(u"1:200000:!!:!!:!!"_q), "bad base64"),
		Expect(!DecodeRecord(QString()), "empty"),
	});
}

[[nodiscard]] std::vector<Check> Checks() {
	return {
		{ "13.store-only-platforms", CheckStoreOnlyPlatforms },
		{ "13.ios-only", CheckIosOnly },
		{ "13.android-only", CheckAndroidOnly },
		{ "13.ios-and-android", CheckIosAndAndroid },
		{ "13.all-platforms-stay", CheckAllPlatformsStay },
		{ "13.all-together-with-ios", CheckAllTogetherWithIos },
		{ "13.other-platforms-never-apply", CheckOtherPlatformsNeverApply },
		{ "12.ids-round-trip", CheckIdsRoundTrip },
		{ "12.with-hidden", CheckWithHidden },
		{ "12.notifications", CheckHiddenNotifications },
		{ "12.name-matches", CheckNameMatches },
		{ "11.new-second-passcode", CheckNewSecondPasscode },
		{ "11.account-hidden", CheckAccountHidden },
		{ "11.toggle-visible", CheckToggleVisible },
		{ "11.record", CheckRecord },
	};
}

} // namespace

int main() {
	const auto checks = Checks();
	auto passed = 0;
	for (const auto &check : checks) {
		const auto failure = check.run();
		if (failure.isEmpty()) {
			std::printf("PASS %s\n", check.name);
			++passed;
		} else {
			std::printf(
				"FAIL %s: %s\n",
				check.name,
				failure.toUtf8().constData());
		}
	}
	std::printf("%d of %d checks passed.\n", passed, int(checks.size()));
	return (passed == int(checks.size())) ? 0 : 1;
}
