/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/discover/luckgram_discover_logic.h"
#include "luckgram/discover/luckgram_scan_logic.h"

#include <QtCore/QJsonDocument>

// Cases ported from mobile ScanLogicTest and BlockLogicTest, plus the
// Discover rows (27).

namespace {

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] QJsonObject J(const char *json) {
	return QJsonDocument::fromJson(QByteArray(json)).object();
}

} // namespace

LUCKGRAM_TEST(ScanLogin) {
	using namespace Luckgram::Scan;
	LG_CHECK(KindOf(U("tg://login?token=AQIDBA")) == Kind::Login);
}

LUCKGRAM_TEST(ScanProxies) {
	using namespace Luckgram::Scan;
	LG_CHECK(KindOf(U("tg://proxy?server=1.2.3.4&port=443&secret=ee00")) == Kind::Proxy);
	LG_CHECK(KindOf(U("tg://socks?server=1.2.3.4&port=1080")) == Kind::Proxy);
	LG_CHECK(KindOf(U("https://t.me/proxy?server=1.2.3.4&port=443&secret=dd00")) == Kind::Proxy);
	LG_CHECK(KindOf(U("t.me/socks?server=h&port=1")) == Kind::Proxy);
	LG_CHECK(KindOf(U("HTTPS://TELEGRAM.ME/proxy?server=h&port=1&secret=x")) == Kind::Proxy);
}

LUCKGRAM_TEST(ScanTelegramLinks) {
	using namespace Luckgram::Scan;
	LG_CHECK(KindOf(U("https://t.me/luckgram")) == Kind::Telegram);
	LG_CHECK(KindOf(U("t.me/+8613800000000")) == Kind::Telegram);
	LG_CHECK(KindOf(U("https://t.me/+AbCdEf")) == Kind::Telegram);
	LG_CHECK(KindOf(U("tg://resolve?domain=luckgram")) == Kind::Telegram);
	LG_CHECK(KindOf(U("tg://user?id=123")) == Kind::Telegram);
	LG_CHECK(KindOf(U("https://www.t.me/luckgram")) == Kind::Telegram);
	LG_CHECK(KindOf(U("https://t.me/proxybot")) == Kind::Telegram);
}

LUCKGRAM_TEST(ScanWebAndText) {
	using namespace Luckgram::Scan;
	LG_CHECK(KindOf(U("https://example.com/a?b=1")) == Kind::Web);
	LG_CHECK(KindOf(U("http://t.me.example.com/x")) == Kind::Web);
	LG_CHECK(KindOf(U("https://t.me")) == Kind::Web);
	LG_CHECK(KindOf(U("hello world")) == Kind::Text);
	LG_CHECK(KindOf(U("WIFI:S:home;T:WPA;P:secret;;")) == Kind::Text);
	LG_CHECK(KindOf(QString()) == Kind::Text);
}

LUCKGRAM_TEST(ScanUrls) {
	using namespace Luckgram::Scan;
	LG_EQUAL(Url(U("t.me/luckgram")), U("https://t.me/luckgram"));
	LG_EQUAL(Url(U(" https://t.me/luckgram ")), U("https://t.me/luckgram"));
	LG_EQUAL(Url(U("tg://resolve?domain=x")), U("tg://resolve?domain=x"));
}

LUCKGRAM_TEST(ScanProxyStates) {
	using namespace Luckgram::Scan;
	LG_CHECK(StateOf(false, true) == ProxyState::Off);
	LG_CHECK(StateOf(true, false) == ProxyState::Connecting);
	LG_CHECK(StateOf(true, true) == ProxyState::Connected);
	LG_EQUAL(Texts(true).proxyState(ProxyState::Connecting), U("连接中"));
}

LUCKGRAM_TEST(BlocksParsesIdsDedupesAndToleratesObjects) {
	using namespace Luckgram::Blocks;
	const auto j = J(R"({"items":[{"user":"a"},{"user":{"id":"b"}},{"user":"a"},{"user":""},{}],"next_cursor":"c1"})");
	LG_EQUAL(ParseIds(j), (QStringList{ U("a"), U("b") }));
	LG_EQUAL(NextCursor(j), U("c1"));
	LG_CHECK(NextCursor(J(R"({"items":[]})")).isEmpty());
	LG_CHECK(NextCursor(J(R"({"next_cursor":null})")).isEmpty());
	LG_CHECK(ParseIds(QJsonObject()).isEmpty());
}

LUCKGRAM_TEST(BlocksPaths) {
	using namespace Luckgram::Blocks;
	LG_EQUAL(ListPath(QString()), U("/v1/blocks"));
	LG_EQUAL(ListPath(U("a+b=")), U("/v1/blocks?cursor=a%2Bb%3D"));
	LG_EQUAL(DeletePath(U("u/1")), U("/v1/blocks/u%2F1"));
	LG_EQUAL(BlockBody(U("u1")).value(U("user")).toString(), U("u1"));
}

LUCKGRAM_TEST(BlocksNameCache) {
	using namespace Luckgram::Blocks;
	auto c = PutName(QString(), U("a"), U(" Alice "));
	c = PutName(c, U("b"), U("Bob"));
	LG_EQUAL(Name(c, U("a")), U("Alice"));
	c = RemoveName(c, U("a"));
	LG_CHECK(Name(c, U("a")).isEmpty());
	LG_EQUAL(Name(c, U("b")), U("Bob"));
	LG_CHECK(Name(U("not json"), U("b")).isEmpty());
	LG_CHECK(Name(PutName(c, U("b"), QString()), U("b")).isEmpty());
}

LUCKGRAM_TEST(BlocksTextsBothLanguages) {
	using namespace Luckgram::Blocks;
	LG_EQUAL(Texts(true).title(), U("已拉黑的人"));
	LG_EQUAL(Texts(false).title(), U("Blocked People"));
}

LUCKGRAM_TEST(DiscoverRowsFollowTheirSwitches) {
	using namespace Luckgram::Discover;
	LG_CHECK(VisibleEntries(false, false).empty());
	const auto both = VisibleEntries(true, true);
	LG_EQUAL(int(both.size()), 2);
	LG_CHECK(both[0] == Entry::Square);
	LG_CHECK(both[1] == Entry::Nearby);
	const auto nearby = VisibleEntries(false, true);
	LG_EQUAL(int(nearby.size()), 1);
	LG_CHECK(nearby[0] == Entry::Nearby);
	LG_EQUAL(Texts(true).pageTitle(), U("发现"));
	LG_EQUAL(Texts(false).emptyText(), U("No features available right now"));
	LG_EQUAL(Texts(true).subtitle(Entry::Square), U("看看大家在分享什么"));
	LG_EQUAL(Texts(false).title(Entry::Nearby), U("People Nearby"));
}
