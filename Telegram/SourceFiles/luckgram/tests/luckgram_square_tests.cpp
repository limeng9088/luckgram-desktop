/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/discover/luckgram_square_logic.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

// Cases ported from mobile SquareLogicTest (spec 26).

namespace {

using namespace Luckgram::Square;

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] QJsonObject J(const char *json) {
	return QJsonDocument::fromJson(QByteArray(json)).object();
}

[[nodiscard]] Comment C(const char *id, const char *parent, const char *author) {
	auto x = Comment();
	x.id = U(id);
	x.parentId = parent ? U(parent) : QString();
	x.author.id = U(author);
	return x;
}

[[nodiscard]] Post P(const char *id, const char *author = "a") {
	auto p = Post();
	p.id = U(id);
	p.author.id = U(author);
	return p;
}

[[nodiscard]] Page<Post> PageOf(const char *cursor, std::vector<const char*> ids) {
	auto page = Page<Post>();
	for (const auto id : ids) {
		page.items.push_back(P(id));
	}
	page.nextCursor = cursor ? U(cursor) : QString();
	return page;
}

[[nodiscard]] QString Ids(const Feed &f) {
	auto list = QStringList();
	for (const auto &p : f.posts) {
		list.push_back(p.id);
	}
	return list.join(QChar(' '));
}

} // namespace

LUCKGRAM_TEST(SquareParsesPostsPageAndDropsBadImages) {
	const auto j = J(R"({"items":[{"id":"p1","author":{"id":"a","display_name":"Alice","username":"alice","avatar_url":"http://x/a.png"},
		"text":"hi","images":[{"url":"https://cdn/1.jpg","width":10,"height":20},{"url":"ftp://bad"}],
		"like_count":3,"comment_count":-1,"liked":true,"created_at":"2026-11-01T10:00:00Z","status":"weird"},
		{"id":""}],"next_cursor":"abc"})");
	const auto page = ParsePosts(j);
	LG_EQUAL(int(page.items.size()), 1);
	LG_EQUAL(page.nextCursor, U("abc"));
	const auto &p = page.items[0];
	LG_EQUAL(p.author.displayName, U("Alice"));
	LG_CHECK(p.author.avatarUrl.isEmpty());
	LG_EQUAL(int(p.images.size()), 1);
	LG_CHECK(!p.video.has_value());
	LG_EQUAL(p.likeCount, 3);
	LG_EQUAL(p.commentCount, 0);
	LG_CHECK(p.liked);
	LG_EQUAL(p.status, U(kStatusPublished));
	LG_CHECK(!p.isHidden());
	LG_CHECK(p.createdAtMs > 0);
	LG_CHECK(ParsePosts(J(R"({"items":[]})")).nextCursor.isEmpty());
}

LUCKGRAM_TEST(SquareParsesHiddenAndVideo) {
	const auto p = ParsePost(J(R"({"id":"p2","status":"hidden","hidden_reason":"spam",
		"images":[{"url":"https://cdn/x.jpg"}],
		"video":{"url":"https://cdn/v.mp4","cover_url":"https://cdn/c.jpg","width":1280,"height":720,"duration_seconds":42}})"));
	LG_CHECK(p.isHidden());
	LG_EQUAL(p.hiddenReason, U("spam"));
	LG_CHECK(p.video.has_value());
	LG_EQUAL(p.video->durationSeconds, 42);
	LG_EQUAL(p.video->coverUrl, U("https://cdn/c.jpg"));
	LG_CHECK(p.images.empty());
	LG_CHECK(!ParseVideo(J(R"({"url":"http://plain/v.mp4"})")).has_value());
	const auto c = ParseComment(J(R"({"id":"c1","status":"hidden","reason":"x"})"));
	LG_EQUAL(c.status, U(kStatusHidden));
	LG_EQUAL(c.hiddenReason, U("x"));
}

LUCKGRAM_TEST(SquareParsesUploads) {
	const auto u = ParseUploads(J(R"({"uploads":[{"key":"square/1.jpg","url":"https://s3/1"},{"key":"square/2.jpg","url":"http://s3/2"},{"url":"https://s3/3"}]})"));
	LG_EQUAL(int(u.size()), 1);
	LG_EQUAL(u[0].key, U("square/1.jpg"));
	LG_CHECK(ParseUploads(QJsonObject()).empty());
}

LUCKGRAM_TEST(SquareAppendsWithoutDuplicates) {
	auto into = std::vector<Post>();
	AppendUnique(into, { P("a"), P("b") });
	AppendUnique(into, { P("a"), P("b") });
	LG_EQUAL(int(into.size()), 2);
}

LUCKGRAM_TEST(SquareThreadsOneLevelOfReplies) {
	const auto c1 = C("c1", nullptr, "a");
	const auto r1 = C("r1", "c1", "b");
	const auto rr = C("rr", "r1", "c");
	const auto c2 = C("c2", nullptr, "d");
	const auto orphan = C("o", "missing", "e");
	const auto out = Thread({ rr, c2, r1, c1, orphan });
	auto ids = QStringList();
	for (const auto &x : out) {
		ids.push_back(x.id + QChar(':') + QString::number(x.depth));
	}
	LG_EQUAL(ids, (QStringList{ U("c2:0"), U("c1:0"), U("rr:1"), U("r1:1"), U("o:0") }));
	LG_EQUAL(ReplyParentId({ c1, r1, rr }, rr), U("c1"));
	LG_EQUAL(ReplyParentId({ c1, r1, rr }, c1), U("c1"));
}

LUCKGRAM_TEST(SquarePermissions) {
	auto post = Post();
	post.author.id = U("owner");
	const auto mine = C("m", nullptr, "me");
	const auto other = C("o", nullptr, "x");
	LG_CHECK(CanDeletePost(post, U("owner")));
	LG_CHECK(!CanDeletePost(post, U("me")));
	LG_CHECK(!CanDeletePost(post, QString()));
	LG_CHECK(CanDeleteComment(mine, &post, U("me")));
	LG_CHECK(CanDeleteComment(other, &post, U("owner")));
	LG_CHECK(!CanDeleteComment(other, &post, U("me")));
	LG_CHECK(!CanDeleteComment(other, nullptr, QString()));
}

LUCKGRAM_TEST(SquareValidation) {
	const auto long1000 = QString(1000, QChar(0x5B57));
	LG_CHECK(IsValidPost(long1000, 0, false));
	LG_CHECK(!IsValidPost(long1000 + U("x"), 0, false));
	LG_CHECK(!IsValidPost(U("   "), 0, false));
	LG_CHECK(IsValidPost(QString(), 1, false));
	LG_CHECK(IsValidPost(QString(), 0, true));
	LG_CHECK(!IsValidPost(QString(), 10, false));
	LG_CHECK(!IsValidPost(U("x"), 1, true));
	LG_CHECK(IsValidComment(U(" ok ")));
	LG_CHECK(!IsValidComment(QString()));
	LG_CHECK(!IsValidComment(QString(301, QChar('a'))));
	LG_EQUAL(Length(U("😀😀")), 2);
}

LUCKGRAM_TEST(SquareImageAndVideoSizing) {
	LG_CHECK(ScaledSize(4096, 3072) == (Size{ 2048, 1536 }));
	LG_CHECK(ScaledSize(3072, 4096) == (Size{ 1536, 2048 }));
	LG_CHECK(ScaledSize(800, 600) == (Size{ 800, 600 }));
	LG_CHECK(ScaledSize(100000, 10) == (Size{ 2048, 1 }));
	LG_CHECK(ScaledSize(0, 10) == (Size{ 0, 0 }));
	LG_EQUAL(SampleSize(2048, 100), 1);
	LG_EQUAL(SampleSize(4096, 100), 2);
	LG_EQUAL(SampleSize(9000, 9000), 4);
	LG_CHECK(VideoSize(1920, 1080) == (Size{ 1280, 720 }));
	LG_CHECK(VideoSize(1080, 1920) == (Size{ 720, 1280 }));
	LG_CHECK(VideoSize(640, 480) == (Size{ 640, 480 }));
	LG_CHECK(VideoSize(1921, 1079) == (Size{ 1280, 718 }));
	LG_EQUAL(VideoBitrate(1280, 720, 0), 2'500'000);
	LG_EQUAL(VideoBitrate(1280, 720, 1'000'000), 1'000'000);
	LG_EQUAL(VideoBitrate(160, 120, 0), 600'000);
	LG_EQUAL(FormatDuration(7), U("0:07"));
	LG_EQUAL(FormatDuration(60), U("1:00"));
	LG_EQUAL(FormatDuration(754), U("12:34"));
	LG_EQUAL(WholeSeconds(75'020), 75);
	LG_EQUAL(WholeSeconds(40'999), 40);
	LG_EQUAL(WholeSeconds(60'000), 60);
	LG_EQUAL(WholeSeconds(-5), 0);
	LG_EQUAL(FormatDuration(WholeSeconds(75'020)), U("1:15"));
}

LUCKGRAM_TEST(SquarePathsAndBodies) {
	LG_EQUAL(FeedPath(U("nope"), QString()), U("/v1/square/feed?tab=latest"));
	LG_EQUAL(FeedPath(U(kTabHot), U("a/b")), U("/v1/square/feed?tab=hot&cursor=a%2Fb"));
	LG_EQUAL(WithCursor(U("/v1/square/users/x/posts"), U("1")), U("/v1/square/users/x/posts?cursor=1"));
	LG_EQUAL(PathId(U("a b")), U("a%20b"));
	const auto body = PostBody(U("  hi "), { U("k1"), U("k2") });
	LG_EQUAL(body.value(U("text")).toString(), U("hi"));
	LG_EQUAL(int(body.value(U("image_keys")).toArray().size()), 2);
	const auto vb = PostBodyWithVideo(U("v"), U("square/v.mp4"), U("square/c.jpg"));
	LG_EQUAL(vb.value(U("video_key")).toString(), U("square/v.mp4"));
	LG_EQUAL(vb.value(U("cover_key")).toString(), U("square/c.jpg"));
	LG_CHECK(!vb.contains(U("image_keys")));
	const auto ub = UploadsBody(U(kVideoContentType), { 1234 });
	LG_EQUAL(ub.value(U("content_type")).toString(), U("video/mp4"));
	LG_EQUAL(ub.value(U("count")).toInt(), 1);
	LG_EQUAL(qint64(ub.value(U("sizes")).toArray().at(0).toDouble()), qint64(1234));
	LG_CHECK(!CommentBody(U("x"), QString()).contains(U("parent_id")));
	LG_EQUAL(CommentBody(U("x"), U("c1")).value(U("parent_id")).toString(), U("c1"));
	const auto r = ReportBody(U("post"), U("p1"), U("spam"));
	LG_EQUAL(r.value(U("target")).toString(), U("post"));
	LG_EQUAL(ReportReasons().size(), 7);
}

LUCKGRAM_TEST(SquareSilentLinkHelpers) {
	LG_EQUAL(BotUsername(U("https://t.me/LuckgramBot?start=abc")), U("LuckgramBot"));
	LG_EQUAL(BotUsername(U("t.me/lg_bot/")), U("lg_bot"));
	LG_CHECK(BotUsername(U("https://example.com/LuckgramBot?start=abc")).isEmpty());
	LG_CHECK(BotUsername(U("https://t.me/?start=abc")).isEmpty());
	LG_CHECK(BotUsername(QString()).isEmpty());
	LG_EQUAL(StartCommand(U("n0nce")), U("/start n0nce"));
}

LUCKGRAM_TEST(SquareTextsCoverBothLanguagesAndWaitTimes) {
	const auto zh = Texts(true);
	const auto en = Texts(false);
	LG_EQUAL(zh.title(), U("广场"));
	LG_EQUAL(en.title(), U("Square"));
	LG_CHECK(zh.hiddenInfo(U("x")).contains(U("原因：x")));
	LG_CHECK(zh.hiddenInfo(U("spam")).contains(U("垃圾广告")));
	LG_CHECK(en.hiddenInfo(U("hate")).contains(U("Hate speech")));
	LG_CHECK(en.hiddenInfo(QString()).startsWith(U("This post has been hidden")));
	LG_CHECK(zh.waitSeconds(30).contains(U("30 秒")));
	LG_CHECK(en.waitSeconds(90).contains(U("2 min")));
	LG_CHECK(zh.waitSeconds(7200).contains(U("小时")));
	LG_CHECK(en.waitSeconds(0).startsWith(U("You're doing that too often")));
	LG_EQUAL(zh.timeAgo(1000, 2000), U("刚刚"));
	LG_EQUAL(en.timeAgo(1, 1 + qint64(3) * 3600 * 1000), U("3 h ago"));
	LG_EQUAL(en.timeAgo(0, 5), QString());
	for (const auto &reason : ReportReasons()) {
		LG_CHECK(!zh.reportReason(reason).isEmpty());
		LG_CHECK(!en.reportReason(reason).isEmpty());
	}
	LG_CHECK(en.dailyLimitInfo(10).contains(U("10")));
	LG_CHECK(en.videoTooLong(60, 95).contains(U("1:35")));
	LG_CHECK(zh.attachmentsInfo(60).contains(U("60 秒")));
	LG_EQUAL(zh.videoLabel(42), U("视频 · 0:42"));
	LG_EQUAL(en.moreFailed(), U("Couldn't load more. Click to retry."));
	for (const auto &tx : { zh, en }) {
		for (const auto &s : { tx.connecting(), tx.connectFailed(), tx.needContent(), tx.profileNeeded(), tx.empty() }) {
			LG_CHECK(!s.contains(U("绑定")) && !s.toLower().contains(U("link")));
		}
	}
	LG_EQUAL(zh.refreshed(3), U("更新了 3 条新内容"));
	LG_EQUAL(zh.refreshed(0), U("已是最新内容"));
	LG_EQUAL(en.refreshed(1), U("1 new post"));
	LG_EQUAL(en.refreshed(12), U("12 new posts"));
	LG_EQUAL(en.uploadingVideo(42), U("Uploading video… 42%"));
}

LUCKGRAM_TEST(SquareRefreshKeepsTheListUntilTheNewPageArrives) {
	auto f = Feed(U(kTabLatest));
	LG_CHECK(f.footer() == Feed::Footer::Skeleton);
	LG_CHECK(f.beginRefresh());
	LG_CHECK(!f.beginRefresh());
	LG_EQUAL(f.applyRefresh(PageOf("c1", { "p3", "p2", "p1" }), 1000), 0);
	LG_EQUAL(Ids(f), U("p3 p2 p1"));
	LG_CHECK(f.footer() == Feed::Footer::LoadingMore);

	LG_CHECK(f.beginRefresh());
	LG_EQUAL(Ids(f), U("p3 p2 p1"));
	LG_CHECK(f.footer() == Feed::Footer::LoadingMore);
	LG_EQUAL(f.applyRefresh(PageOf(nullptr, { "p5", "p4", "p3" }), 2000), 2);
	LG_EQUAL(Ids(f), U("p5 p4 p3"));
	LG_CHECK(!f.refreshing);
	LG_CHECK(f.footer() == Feed::Footer::End);
	LG_EQUAL(f.applyRefresh(PageOf(nullptr, { "p5", "p4", "p3" }), 3000), 0);
}

LUCKGRAM_TEST(SquareFailedRefreshLeavesTheListAlone) {
	auto f = Feed(U(kTabHot));
	f.beginRefresh();
	f.failRefresh();
	LG_CHECK(f.footer() == Feed::Footer::FirstError);
	LG_CHECK(f.isStale(0));

	f.beginRefresh();
	LG_CHECK(f.footer() == Feed::Footer::Skeleton);
	f.applyRefresh(PageOf("c", { "p1", "p2" }), 10);
	f.beginRefresh();
	f.failRefresh();
	LG_EQUAL(Ids(f), U("p1 p2"));
	LG_EQUAL(f.nextCursor, U("c"));
	LG_CHECK(f.footer() == Feed::Footer::LoadingMore);
	LG_CHECK(f.beginRefresh());
}

LUCKGRAM_TEST(SquareLoadsMoreBeforeTheEndAndDropsPagesOfAReplacedList) {
	auto f = Feed(U(kTabLatest));
	LG_CHECK(!f.canLoadMore());
	f.beginRefresh();
	f.applyRefresh(PageOf("c1", { "p1", "p2", "p3", "p4", "p5", "p6", "p7", "p8" }), 0);
	LG_CHECK(!f.shouldLoadMore(2));
	LG_CHECK(f.shouldLoadMore(7 - Feed::kPrefetch));
	auto g = f.beginMore();
	LG_CHECK(!f.canLoadMore());
	LG_CHECK(f.applyMore(PageOf(nullptr, { "p8", "p9" }), g));
	LG_EQUAL(Ids(f), U("p1 p2 p3 p4 p5 p6 p7 p8 p9"));
	LG_CHECK(!f.shouldLoadMore(8));
	LG_CHECK(f.footer() == Feed::Footer::End);

	f.beginRefresh();
	f.applyRefresh(PageOf("c2", { "n1", "n2" }), 0);
	g = f.beginMore();
	f.beginRefresh();
	f.applyRefresh(PageOf("c3", { "m1" }), 0);
	LG_CHECK(!f.loadingMore);
	LG_CHECK(!f.applyMore(PageOf(nullptr, { "n3" }), g));
	f.failMore(g);
	LG_CHECK(!f.moreFailed);
	LG_EQUAL(Ids(f), U("m1"));

	g = f.beginMore();
	f.failMore(g);
	LG_CHECK(f.footer() == Feed::Footer::MoreError);
	LG_CHECK(!f.shouldLoadMore(0));
	LG_CHECK(f.canLoadMore());
}

LUCKGRAM_TEST(SquareStaleAfterFiveMinutesAndEmptyStates) {
	auto f = Feed(U(kTabRecommend));
	f.beginRefresh();
	f.applyRefresh(PageOf(nullptr, {}), 1000);
	LG_CHECK(f.footer() == Feed::Footer::Empty);
	LG_CHECK(!f.isStale(1000 + Feed::kStaleMs - 1));
	LG_CHECK(f.isStale(1000 + Feed::kStaleMs));
	f.beginRefresh();
	LG_CHECK(f.footer() == Feed::Footer::Skeleton);

	const auto left = 1000 + 3 * Feed::kStaleMs;
	f.markSeen(left);
	LG_CHECK(!f.isStale(left + 60'000));
	LG_CHECK(f.isStale(left + Feed::kStaleMs));
}

LUCKGRAM_TEST(SquareQuietMergeUpdatesCountsInPlace) {
	auto f = Feed(U(kTabLatest));
	f.beginRefresh();
	f.applyRefresh(PageOf("c", { "p1", "p2", "p3" }), 0);
	auto liked = P("p2");
	liked.likeCount = 5;
	liked.liked = true;
	LG_EQUAL(f.mergeSilently({ P("p9"), liked, P("p3") }), 1);
	LG_EQUAL(Ids(f), U("p1 p2 p3"));
	LG_EQUAL(f.posts[1].likeCount, 5);
	LG_EQUAL(f.nextCursor, U("c"));

	f.posts[0].author.id = U("b");
	LG_EQUAL(f.removeAuthor(U("b")), 1);
	LG_CHECK(f.removePost(U("p3")));
	LG_CHECK(!f.removePost(U("p3")));
	LG_EQUAL(Ids(f), U("p2"));
}

LUCKGRAM_TEST(SquareSavedFirstPageIsShownAtOnceAndRefreshedWhenOld) {
	const auto first = J(R"({"items":[{"id":"p1"},{"id":"p2"}],"next_cursor":"c"})");
	const auto stored = EncodeSaved(first, 1000);
	const auto saved = DecodeSaved(stored, 2000);
	LG_CHECK(saved.has_value());
	LG_EQUAL(saved->savedAtMs, qint64(1000));

	auto f = Feed(U(kTabLatest));
	LG_CHECK(f.restore(saved->page, saved->savedAtMs));
	LG_EQUAL(Ids(f), U("p1 p2"));
	LG_EQUAL(f.nextCursor, U("c"));
	LG_CHECK(f.footer() == Feed::Footer::LoadingMore);
	LG_CHECK(!f.isStale(1000 + Feed::kStaleMs - 1));
	LG_CHECK(f.isStale(1000 + Feed::kStaleMs));
	LG_CHECK(!f.restore(saved->page, saved->savedAtMs));

	LG_CHECK(f.beginRefresh());
	LG_EQUAL(Ids(f), U("p1 p2"));
	LG_EQUAL(f.applyRefresh(PageOf(nullptr, { "p3", "p2" }), 5000), 1);

	f.beginRefresh();
	f.cancelLoading();
	LG_CHECK(f.beginRefresh());

	LG_CHECK(!DecodeSaved(QString(), 0).has_value());
	LG_CHECK(!DecodeSaved(U("not json"), 2000).has_value());
	LG_CHECK(!DecodeSaved(EncodeSaved(J(R"({"items":[]})"), 1000), 2000).has_value());
	LG_CHECK(!DecodeSaved(stored, 1000 + kSavedMaxAgeMs + 1).has_value());
	LG_CHECK(!DecodeSaved(stored, 999).has_value());
}
