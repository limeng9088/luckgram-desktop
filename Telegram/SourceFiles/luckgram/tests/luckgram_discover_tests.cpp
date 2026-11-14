/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/discover/luckgram_discover_logic.h"
#include "luckgram/discover/luckgram_discover_offline.h"
#include "luckgram/discover/luckgram_nearby_logic.h"
#include "luckgram/discover/luckgram_scan_logic.h"
#include "luckgram/discover/luckgram_square_logic.h"

#include <QtCore/QJsonArray>
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

LUCKGRAM_TEST(ScanLoginError) {
	using Luckgram::Scan::Texts;
	LG_EQUAL(
		Texts(false).loginError(QStringLiteral("AUTH_TOKEN_EXCEPTION")),
		QStringLiteral("This account is already logged in from this app."));
	LG_EQUAL(
		Texts(false).loginError(QStringLiteral("AUTH_TOKEN_EXPIRED")),
		QStringLiteral("An error occurred.\nAUTH_TOKEN_EXPIRED"));
	LG_CHECK(Texts(true).loginError(QStringLiteral("X")).endsWith(
		QStringLiteral("\nX")));
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
	LG_EQUAL(Texts(false).proxyState(ProxyState::Off), U("Off"));
	LG_EQUAL(Texts(true).fromClipboard(), U("从剪贴板识别"));
	LG_CHECK(Texts(false).noCamera().contains(U("clipboard")));
	LG_CHECK(!Texts(true).menuAbout().contains(U("⋮")));
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

// #137: the CI session answers Discover locally, in the shapes the parsers
// read, and with nothing to download.
LUCKGRAM_TEST(DiscoverOfflineRoutes) {
	using namespace Luckgram::Discover::Offline;
	LG_CHECK(Handles(U("/v1/square/feed?tab=new")));
	LG_CHECK(Handles(U("/v1/square")));
	LG_CHECK(Handles(U("/v1/nearby?lat=1&lon=2")));
	LG_CHECK(Handles(U("/v1/nearby/me")));
	LG_CHECK(Handles(U("/v1/blocks?cursor=x")));
	LG_CHECK(Handles(U("/v1/link?nonce=n")));
	LG_CHECK(Handles(U("/v1/link/start")));
	LG_CHECK(!Handles(U("/v1/squares")));
	LG_CHECK(!Handles(U("/v1/version?platform=windows")));
	LG_CHECK(!Handles(U("/v1/ai/chat")));
	LG_CHECK(!Answer("GET", U("/v1/config"), 0).has_value());

	const auto write = Answer("POST", U("/v1/square/posts"), 0);
	LG_CHECK(write.has_value());
	LG_EQUAL(write.value_or(Reply()).status, 503);
	LG_CHECK(Answer("PUT", U("/v1/nearby/me"), 0).value_or(Reply()).status == 503);
	LG_CHECK(Answer("POST", U("/v1/link/start"), 0).value_or(Reply()).status == 503);
}

LUCKGRAM_TEST(DiscoverOfflineData) {
	using namespace Luckgram::Discover::Offline;
	const auto now = qint64(1794645833) * 1000;
	const auto body = [&](const char *path) {
		const auto answer = Answer("GET", U(path), now);
		LG_CHECK(answer.has_value());
		if (!answer) {
			return QJsonObject();
		}
		LG_EQUAL(answer->status, 200);
		LG_CHECK(!answer->body.contains("http"));
		return QJsonDocument::fromJson(answer->body).object();
	};

	const auto feed = Luckgram::Square::ParsePosts(body("/v1/square/feed?tab=hot"));
	LG_EQUAL(int(feed.items.size()), 3);
	LG_CHECK(feed.nextCursor.isEmpty());
	for (const auto &post : feed.items) {
		LG_CHECK(post.images.empty());
		LG_CHECK(!post.video.has_value());
		LG_CHECK(post.author.avatarUrl.isEmpty());
		LG_CHECK(!post.author.displayName.isEmpty());
		LG_CHECK(post.createdAtMs > 0 && post.createdAtMs < now);
		LG_CHECK(!post.isHidden());
	}

	const auto people = Luckgram::Nearby::ParseList(
		body("/v1/nearby?lat=1&lon=2"),
		QString());
	LG_EQUAL(int(people.size()), 3);
	LG_EQUAL(people[0].bucket, U("500m"));
	LG_CHECK(people[0].avatarUrl.isEmpty());

	LG_CHECK(!Luckgram::Nearby::ParseMe(body("/v1/nearby/me"), now).visible);
	LG_CHECK(body("/v1/link").value(U("linked")).toBool());
	LG_CHECK(!body("/v1/square/profile").value(U("display_name")).toString().isEmpty());
	LG_CHECK(body("/v1/square/posts/x/comments").value(U("items")).toArray().isEmpty());
	LG_CHECK(body("/v1/blocks").value(U("items")).toArray().isEmpty());
}
