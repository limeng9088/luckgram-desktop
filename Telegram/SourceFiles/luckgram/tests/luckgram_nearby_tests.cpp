/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/discover/luckgram_nearby_logic.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>

// Cases ported from mobile NearbyLogicTest (spec 25). The Android location
// provider chain cases don't apply to the desktop.

namespace {

using namespace Luckgram::Nearby;

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] QJsonObject J(const QByteArray &json) {
	return QJsonDocument::fromJson(json).object();
}

[[nodiscard]] int CodePoints(const QString &s) {
	return int(s.toUcs4().size());
}

} // namespace

LUCKGRAM_TEST(NearbyCoarsensToTwoDecimals) {
	LG_EQUAL(Coord(31.230416), U("31.23"));
	LG_EQUAL(Coord(121.473701), U("121.47"));
	LG_EQUAL(Coord(-33.868820), U("-33.87"));
	LG_EQUAL(Coord(-0.001), U("0.00"));
	const auto body = CardBody(U("A"), U("bio"), U("alice"), 31.230416, 121.473701, std::nullopt);
	const auto twoDecimals = QRegularExpression(U("^-?\\d+(\\.\\d{1,2})?$"));
	LG_CHECK(twoDecimals.match(QString::number(body.value(U("lat")).toDouble())).hasMatch());
	LG_CHECK(twoDecimals.match(QString::number(body.value(U("lon")).toDouble())).hasMatch());
	LG_CHECK(!body.contains(U("avatar")));
	LG_EQUAL(QueryPath(31.230416, 121.473701), U("/v1/nearby?lat=31.23&lon=121.47"));
	LG_EQUAL(CardBody(U("A"), QString(), U("alice"), 1, 1, QString()).value(U("avatar")).toString(), QString());
	LG_CHECK(CardBody(U("A"), QString(), U("alice"), 1, 1, QString()).contains(U("avatar")));
}

LUCKGRAM_TEST(NearbyBioIsTrimmedAndCut) {
	LG_EQUAL(CleanBio(QString(80, QChar(0x5B57))).size(), 70);
	LG_EQUAL(CleanBio(U("  a\nb ")), U("a b"));
	LG_EQUAL(CleanBio(QString()), QString());
	auto emoji = QString();
	for (auto i = 0; i != 71; ++i) {
		emoji += U("😀");
	}
	LG_EQUAL(CodePoints(CleanBio(emoji)), 70);
}

LUCKGRAM_TEST(NearbyPreconditions) {
	LG_CHECK(Check(QString()) == Precondition::NeedUsername);
	LG_CHECK(Check(U("alice")) == Precondition::Ready);
}

LUCKGRAM_TEST(NearbyParsesListDropsSelfAndBadItems) {
	const auto j = J(R"({"items":[
		{"id":"1","display_name":"A","bio":"x","username":"a","distance_bucket":"1km","avatar_url":"https://cdn/x.jpg"},
		{"id":"2","display_name":"Me","username":"ME","distance_bucket":"500m"},
		{"id":"","username":"nobody"},
		{"id":"4","username":"d","distance_bucket":"weird","avatar_url":"http://insecure"}]})");
	const auto l = ParseList(j, U("me"));
	LG_EQUAL(int(l.size()), 2);
	LG_EQUAL(l[0].avatarUrl, U("https://cdn/x.jpg"));
	LG_EQUAL(l[1].displayName, U("d"));
	LG_EQUAL(l[1].bucket, U("far"));
	LG_CHECK(l[1].avatarUrl.isEmpty());
}

LUCKGRAM_TEST(NearbyCapsAt100) {
	auto json = QByteArray("{\"items\":[");
	for (auto i = 0; i != 150; ++i) {
		if (i > 0) {
			json += ',';
		}
		json += "{\"id\":\"" + QByteArray::number(i)
			+ "\",\"username\":\"u" + QByteArray::number(i)
			+ "\",\"distance_bucket\":\"3km\"}";
	}
	json += "]}";
	LG_EQUAL(int(ParseList(J(json), QString()).size()), 100);
}

LUCKGRAM_TEST(NearbyGroupsInBucketOrderKeepingServerOrder) {
	auto l = std::vector<Person>();
	for (const auto &[id, bucket] : std::vector<std::pair<const char*, const char*>>{
			{ "a", "far" }, { "b", "500m" }, { "c", "3km" }, { "d", "500m" } }) {
		auto p = Person();
		p.id = U(id);
		p.bucket = U(bucket);
		l.push_back(p);
	}
	const auto g = Group(l);
	auto keys = QStringList();
	for (const auto &[bucket, people] : g) {
		keys.push_back(bucket);
	}
	LG_EQUAL(keys, (QStringList{ U("500m"), U("3km"), U("far") }));
	LG_EQUAL(g[0].second[0].id, U("b"));
	LG_EQUAL(g[0].second[1].id, U("d"));
}

LUCKGRAM_TEST(NearbyExpiryAndReminder) {
	const auto now = qint64(1'000'000'000);
	const auto exp = now + qint64(24) * 3600 * 1000;
	LG_EQUAL(ReminderAt(exp, now), exp - qint64(3'600'000));
	LG_EQUAL(ReminderAt(now + 30 * 60 * 1000, now), qint64(0));
	LG_CHECK(!IsExpired(exp, now));
	LG_CHECK(IsExpired(exp, exp));
	const auto s = ParseMe(J(R"({"visible":true,"expires_at":"2026-11-02T08:00:00Z"})"), now);
	LG_CHECK(s.visible);
	LG_EQUAL(s.expiresAtMs, qint64(1793606400000));
	LG_CHECK(!ParseMe(J(R"({"visible":false})"), now).visible);
	LG_EQUAL(ParseMe(J(R"({"visible":true})"), now).expiresAtMs, now + kDefaultVisibleMs);
}

LUCKGRAM_TEST(NearbyTextsInBothLanguages) {
	const auto zh = Texts(true);
	const auto en = Texts(false);
	for (const auto &b : Buckets()) {
		LG_CHECK(zh.bucket(b) != en.bucket(b));
	}
	for (const auto &r : ReportReasons()) {
		LG_CHECK(zh.reportReason(r) != en.reportReason(r));
	}
	LG_EQUAL(zh.bucket(U("500m")), U("500 米内"));
	LG_EQUAL(en.bucket(U("1km")), U("Within 1 km"));
	LG_CHECK(zh.rateLimited(61).contains(U("2 分钟")));
	LG_CHECK(en.rateLimited(0).contains(U("1 min")));
	LG_CHECK(en.locationFailed().contains(U("Click Refresh")));
	LG_CHECK(zh.introText().contains(U("不离开这台电脑")));
}

LUCKGRAM_TEST(NearbyCardNameMatchesIosAndIsCapped) {
	LG_EQUAL(CardName(U("Ann"), U("Lee"), U("ann")), U("Ann"));
	LG_EQUAL(CardName(U(" "), U("Lee"), U("ann")), U("Lee"));
	LG_EQUAL(CardName(QString(), QString(), U("ann")), U("ann"));
	auto b = QString();
	for (auto i = 0; i != 70; ++i) {
		b += U("😀");
	}
	LG_EQUAL(CodePoints(CardName(b, QString(), U("u"))), 64);
	const auto body = CardBody(b, QString(), U("u"), 31.2, 121.4, std::nullopt);
	LG_EQUAL(CodePoints(body.value(U("display_name")).toString()), 64);
}

LUCKGRAM_TEST(NearbyMissedReminderAfterRestart) {
	const auto hour = qint64(60) * 60 * 1000;
	const auto exp = qint64(1'000'000'000'000);
	LG_CHECK(MissedReminder(exp, exp - hour / 2, 0));
	LG_CHECK(!MissedReminder(exp, exp - hour / 2, exp));
	LG_CHECK(!MissedReminder(exp, exp - 2 * hour, 0));
	LG_CHECK(!MissedReminder(exp, exp, 0));
	LG_CHECK(!MissedReminder(0, exp, 0));
}

LUCKGRAM_TEST(NearbyFallbackFixMustBeFresh) {
	const auto now = qint64(1'800'000'000'000);
	LG_CHECK(IsFreshFix(now - 29 * 60 * 1000, now));
	LG_CHECK(IsFreshFix(now - 30 * 60 * 1000, now));
	LG_CHECK(!IsFreshFix(now - 31 * 60 * 1000, now));
	LG_CHECK(!IsFreshFix(0, now));
	LG_CHECK(!IsFreshFix(now + 10 * 60 * 1000, now));
}

LUCKGRAM_TEST(NearbyNoLinkWordingInTexts) {
	for (const auto zh : { true, false }) {
		const auto tx = Texts(zh);
		for (const auto &v : {
				tx.title(), tx.featureDesc(), tx.needUsernameTitle(),
				tx.needUsernameText(), tx.goUsername(), tx.introTitle(),
				tx.introText(), tx.introContinue(), tx.noPermission(),
				tx.locationFailed(), tx.locationOff(), tx.visibleSwitch(),
				tx.visibleOffInfo(), tx.renew(), tx.renewed(), tx.myCard(),
				tx.empty(), tx.loading(), tx.disabled(), tx.underReview(),
				tx.connecting(), tx.connectFailed(), tx.banned() }) {
			LG_CHECK(!v.contains(U("绑定"))
				&& !v.contains(U("解绑"))
				&& !v.toLower().contains(U("link")));
		}
	}
}
