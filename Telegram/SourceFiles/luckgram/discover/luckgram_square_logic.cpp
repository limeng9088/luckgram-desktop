/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/discover/luckgram_square_logic.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QUrl>

#include <algorithm>
#include <cmath>

namespace Luckgram::Square {
namespace {

[[nodiscard]] QString L(const char *text) {
	return QString::fromLatin1(text);
}

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

// The string value, empty for null / missing / non-string.
[[nodiscard]] QString Str(const QJsonObject &o, const char *key) {
	const auto value = o.value(L(key));
	return value.isString() ? value.toString() : QString();
}

[[nodiscard]] QString HttpsOrEmpty(const QString &url) {
	return url.startsWith(L("https://")) ? url : QString();
}

[[nodiscard]] int Int(const QJsonObject &o, const char *key) {
	return o.value(L(key)).toInt(0);
}

[[nodiscard]] QString HiddenReason(const QJsonObject &o) {
	const auto reason = Str(o, "hidden_reason");
	return reason.isEmpty() ? Str(o, "reason") : reason;
}

[[nodiscard]] const Comment *FindById(
		const std::vector<Comment> &list,
		const QString &id) {
	for (const auto &c : list) {
		if (c.id == id) {
			return &c;
		}
	}
	return nullptr;
}

[[nodiscard]] const Comment *RootOf(
		const std::vector<Comment> &list,
		const Comment &c) {
	auto current = &c;
	for (auto guard = 0; guard < 10 && !current->parentId.isEmpty(); ++guard) {
		const auto parent = FindById(list, current->parentId);
		if (!parent) {
			break;
		}
		current = parent;
	}
	return current;
}

} // namespace

bool Post::isHidden() const {
	return status == L(kStatusHidden);
}

Author ParseAuthor(const QJsonObject &o) {
	auto a = Author();
	a.id = Str(o, "id");
	a.displayName = Str(o, "display_name");
	a.avatarUrl = HttpsOrEmpty(Str(o, "avatar_url"));
	a.username = Str(o, "username");
	a.bio = Str(o, "bio");
	return a;
}

std::optional<Video> ParseVideo(const QJsonObject &o) {
	const auto url = HttpsOrEmpty(Str(o, "url"));
	if (url.isEmpty()) {
		return std::nullopt;
	}
	auto v = Video();
	v.url = url;
	v.coverUrl = HttpsOrEmpty(Str(o, "cover_url"));
	v.width = std::max(0, Int(o, "width"));
	v.height = std::max(0, Int(o, "height"));
	v.durationSeconds = std::max(0, Int(o, "duration_seconds"));
	return v;
}

Post ParsePost(const QJsonObject &o) {
	auto p = Post();
	p.id = Str(o, "id");
	p.author = ParseAuthor(o.value(L("author")).toObject());
	p.text = Str(o, "text");
	const auto images = o.value(L("images")).toArray();
	for (const auto &value : images) {
		if (int(p.images.size()) >= kImagesMax) {
			break;
		}
		const auto io = value.toObject();
		const auto url = HttpsOrEmpty(Str(io, "url"));
		if (url.isEmpty()) {
			continue;
		}
		p.images.push_back({ url, Int(io, "width"), Int(io, "height") });
	}
	p.video = ParseVideo(o.value(L("video")).toObject());
	if (p.video) {
		p.images.clear(); // Attachments are exclusive.
	}
	p.likeCount = std::max(0, Int(o, "like_count"));
	p.commentCount = std::max(0, Int(o, "comment_count"));
	p.liked = o.value(L("liked")).toBool(false);
	p.createdAtMs = ParseTime(Str(o, "created_at"));
	p.status = NormalizeStatus(Str(o, "status"));
	p.hiddenReason = HiddenReason(o);
	return p;
}

Comment ParseComment(const QJsonObject &o) {
	auto c = Comment();
	c.id = Str(o, "id");
	c.postId = Str(o, "post_id");
	c.parentId = Str(o, "parent_id");
	c.author = ParseAuthor(o.value(L("author")).toObject());
	c.text = Str(o, "text");
	c.createdAtMs = ParseTime(Str(o, "created_at"));
	c.status = NormalizeStatus(Str(o, "status"));
	c.hiddenReason = HiddenReason(o);
	return c;
}

Page<Post> ParsePosts(const QJsonObject &j) {
	auto page = Page<Post>();
	for (const auto &value : j.value(L("items")).toArray()) {
		if (!value.isObject()) {
			continue;
		}
		auto p = ParsePost(value.toObject());
		if (!p.id.isEmpty()) {
			page.items.push_back(std::move(p));
		}
	}
	page.nextCursor = Str(j, "next_cursor");
	return page;
}

Page<Comment> ParseComments(const QJsonObject &j) {
	auto page = Page<Comment>();
	for (const auto &value : j.value(L("items")).toArray()) {
		if (!value.isObject()) {
			continue;
		}
		auto c = ParseComment(value.toObject());
		if (!c.id.isEmpty()) {
			page.items.push_back(std::move(c));
		}
	}
	page.nextCursor = Str(j, "next_cursor");
	return page;
}

std::vector<Upload> ParseUploads(const QJsonObject &j) {
	auto result = std::vector<Upload>();
	for (const auto &value : j.value(L("uploads")).toArray()) {
		const auto o = value.toObject();
		const auto key = Str(o, "key");
		const auto url = HttpsOrEmpty(Str(o, "url"));
		if (!key.isEmpty() && !url.isEmpty()) {
			result.push_back({ key, url });
		}
	}
	return result;
}

QString NormalizeStatus(const QString &status) {
	return (status == L(kStatusHidden)) ? L(kStatusHidden) : L(kStatusPublished);
}

qint64 ParseTime(const QString &value) {
	if (value.isEmpty()) {
		return 0;
	}
	const auto parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
	return parsed.isValid() ? parsed.toMSecsSinceEpoch() : 0;
}

void AppendUnique(std::vector<Post> &into, const std::vector<Post> &page) {
	for (const auto &p : page) {
		const auto dup = std::any_of(into.begin(), into.end(), [&](
				const Post &q) {
			return q.id == p.id;
		});
		if (!dup) {
			into.push_back(p);
		}
	}
}

QString EncodeSaved(const QJsonObject &firstPage, qint64 now) {
	auto o = QJsonObject();
	o.insert(L("saved_at"), double(now));
	o.insert(L("page"), firstPage);
	return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

std::optional<Saved> DecodeSaved(const QString &stored, qint64 now) {
	if (stored.isEmpty()) {
		return std::nullopt;
	}
	const auto document = QJsonDocument::fromJson(stored.toUtf8());
	if (!document.isObject()) {
		return std::nullopt;
	}
	const auto o = document.object();
	const auto savedAt = qint64(o.value(L("saved_at")).toDouble(0.));
	const auto page = o.value(L("page"));
	if (!page.isObject()
		|| savedAt <= 0
		|| savedAt > now
		|| now - savedAt > kSavedMaxAgeMs) {
		return std::nullopt;
	}
	auto parsed = ParsePosts(page.toObject());
	if (parsed.items.empty()) {
		return std::nullopt;
	}
	return Saved{ std::move(parsed), savedAt };
}

Feed::Feed(QString tab) : tab(std::move(tab)) {
}

bool Feed::beginRefresh() {
	if (refreshing) {
		return false;
	}
	refreshing = true;
	refreshFailed = false;
	return true;
}

int Feed::applyRefresh(const Page<Post> &page, qint64 now) {
	auto fresh = 0;
	if (loadedOnce) {
		auto old = QSet<QString>();
		for (const auto &p : posts) {
			old.insert(p.id);
		}
		for (const auto &p : page.items) {
			if (!old.contains(p.id)) {
				++fresh;
			}
		}
	}
	posts.clear();
	AppendUnique(posts, page.items);
	nextCursor = page.nextCursor;
	loadedOnce = true;
	loadedAtMs = now;
	refreshing = false;
	refreshFailed = false;
	loadingMore = false;
	moreFailed = false;
	++generation;
	scrollTop = 0;
	return fresh;
}

bool Feed::restore(const Page<Post> &page, qint64 savedAtMs) {
	if (loadedOnce || page.items.empty()) {
		return false;
	}
	AppendUnique(posts, page.items);
	nextCursor = page.nextCursor;
	loadedOnce = true;
	loadedAtMs = savedAtMs;
	return true;
}

void Feed::cancelLoading() {
	refreshing = false;
	loadingMore = false;
}

void Feed::failRefresh() {
	refreshing = false;
	refreshFailed = true;
}

bool Feed::canLoadMore() const {
	return loadedOnce && !refreshing && !loadingMore && !nextCursor.isEmpty();
}

bool Feed::shouldLoadMore(int lastVisiblePost) const {
	return canLoadMore()
		&& !moreFailed
		&& (lastVisiblePost >= int(posts.size()) - 1 - kPrefetch);
}

int Feed::beginMore() {
	loadingMore = true;
	moreFailed = false;
	return generation;
}

bool Feed::applyMore(const Page<Post> &page, int forGeneration) {
	if (forGeneration != generation) {
		return false;
	}
	AppendUnique(posts, page.items);
	nextCursor = page.nextCursor;
	loadingMore = false;
	return true;
}

void Feed::failMore(int forGeneration) {
	if (forGeneration != generation) {
		return;
	}
	loadingMore = false;
	moreFailed = true;
}

void Feed::markSeen(qint64 now) {
	seenAtMs = now;
}

bool Feed::isStale(qint64 now) const {
	return !loadedOnce || (now - std::max(loadedAtMs, seenAtMs) >= kStaleMs);
}

int Feed::mergeSilently(const std::vector<Post> &fresh) {
	auto changed = 0;
	for (const auto &f : fresh) {
		for (auto &p : posts) {
			if (p.id != f.id) {
				continue;
			}
			if (p.likeCount != f.likeCount
				|| p.commentCount != f.commentCount
				|| p.liked != f.liked
				|| p.status != f.status
				|| p.text != f.text) {
				++changed;
				p.likeCount = f.likeCount;
				p.commentCount = f.commentCount;
				p.liked = f.liked;
				p.status = f.status;
				p.hiddenReason = f.hiddenReason;
				p.text = f.text;
			}
			break;
		}
	}
	return changed;
}

bool Feed::removePost(const QString &postId) {
	const auto i = std::find_if(posts.begin(), posts.end(), [&](
			const Post &p) {
		return p.id == postId;
	});
	if (i == posts.end()) {
		return false;
	}
	posts.erase(i);
	return true;
}

int Feed::removeAuthor(const QString &authorId) {
	const auto before = int(posts.size());
	posts.erase(std::remove_if(posts.begin(), posts.end(), [&](
			const Post &p) {
		return p.author.id == authorId;
	}), posts.end());
	return before - int(posts.size());
}

Feed::Footer Feed::footer() const {
	if (posts.empty()) {
		if (!loadedOnce) {
			return refreshFailed ? Footer::FirstError : Footer::Skeleton;
		} else if (refreshing) {
			return Footer::Skeleton;
		} else if (nextCursor.isEmpty()) {
			return Footer::Empty;
		}
	}
	if (moreFailed) {
		return Footer::MoreError;
	}
	return nextCursor.isEmpty() ? Footer::End : Footer::LoadingMore;
}

std::vector<Comment> Thread(std::vector<Comment> comments) {
	auto top = std::vector<int>();
	for (auto i = 0; i != int(comments.size()); ++i) {
		const auto &c = comments[i];
		if (c.parentId.isEmpty() || !FindById(comments, c.parentId)) {
			top.push_back(i);
		}
	}
	const auto isTop = [&](int index) {
		return std::find(top.begin(), top.end(), index) != top.end();
	};
	auto result = std::vector<Comment>();
	for (const auto t : top) {
		auto topComment = comments[t];
		topComment.depth = 0;
		result.push_back(topComment);
		for (auto i = 0; i != int(comments.size()); ++i) {
			const auto &c = comments[i];
			if (i == t || c.parentId.isEmpty() || isTop(i)) {
				continue;
			}
			if (RootOf(comments, c) == &comments[t]) {
				auto reply = c;
				reply.depth = 1;
				result.push_back(reply);
			}
		}
	}
	return result;
}

QString ReplyParentId(const std::vector<Comment> &all, const Comment &c) {
	const auto self = FindById(all, c.id);
	return RootOf(all, self ? *self : c)->id;
}

bool CanDeletePost(const Post &p, const QString &myId) {
	return !myId.isEmpty() && (myId == p.author.id);
}

bool CanDeleteComment(const Comment &c, const Post *post, const QString &myId) {
	if (myId.isEmpty()) {
		return false;
	}
	return (myId == c.author.id) || (post && myId == post->author.id);
}

int Length(const QString &s) {
	return int(s.toUcs4().size());
}

bool IsValidPost(const QString &text, int images, bool hasVideo) {
	const auto n = Length(text.trimmed());
	if (images < 0 || images > kImagesMax || n > kTextMax) {
		return false;
	} else if (hasVideo && images > 0) {
		return false;
	}
	return (n > 0) || (images > 0) || hasVideo;
}

bool IsValidComment(const QString &text) {
	const auto n = Length(text.trimmed());
	return (n > 0) && (n <= kCommentMax);
}

Size ScaledSize(int width, int height, int maxEdge) {
	if (width <= 0 || height <= 0) {
		return {};
	}
	const auto edge = std::max(width, height);
	if (edge <= maxEdge) {
		return { width, height };
	}
	const auto k = double(maxEdge) / edge;
	return {
		std::max(1, int(std::lround(width * k))),
		std::max(1, int(std::lround(height * k))),
	};
}

Size VideoSize(int width, int height) {
	const auto s = ScaledSize(width, height, kVideoMaxEdge);
	return { std::max(2, s.width / 2 * 2), std::max(2, s.height / 2 * 2) };
}

int SampleSize(int width, int height) {
	const auto edge = std::max(width, height);
	auto s = 1;
	while (edge / (s * 2) >= kImageMaxEdge) {
		s *= 2;
	}
	return s;
}

int VideoBitrate(int width, int height, int sourceBitrate) {
	const auto area = qint64(std::max(1, width)) * std::max(1, height);
	const auto target = int(std::min(
		qint64(4'000'000),
		std::max(qint64(600'000), area * 2'500'000 / (qint64(1280) * 720))));
	return (sourceBitrate > 0) ? std::min(target, sourceBitrate) : target;
}

int WholeSeconds(qint64 millis) {
	return int(std::max(qint64(0), millis / 1000));
}

QString FormatDuration(int seconds) {
	const auto s = std::max(0, seconds);
	return QString::number(s / 60)
		+ QChar(':')
		+ QStringLiteral("%1").arg(s % 60, 2, 10, QChar('0'));
}

QJsonObject PostBody(const QString &text, const QStringList &imageKeys) {
	auto keys = QJsonArray();
	for (const auto &key : imageKeys) {
		keys.append(key);
	}
	auto result = QJsonObject();
	result.insert(L("text"), text.trimmed());
	result.insert(L("image_keys"), keys);
	return result;
}

QJsonObject PostBodyWithVideo(
		const QString &text,
		const QString &videoKey,
		const QString &coverKey) {
	auto result = QJsonObject();
	result.insert(L("text"), text.trimmed());
	result.insert(L("video_key"), videoKey);
	result.insert(L("cover_key"), coverKey);
	return result;
}

QJsonObject UploadsBody(
		const QString &contentType,
		const std::vector<qint64> &sizes) {
	auto list = QJsonArray();
	for (const auto size : sizes) {
		list.append(double(size));
	}
	auto result = QJsonObject();
	result.insert(L("count"), int(sizes.size()));
	result.insert(L("content_type"), contentType);
	result.insert(L("sizes"), list);
	return result;
}

QJsonObject CommentBody(const QString &text, const QString &parentId) {
	auto result = QJsonObject();
	result.insert(L("text"), text.trimmed());
	if (!parentId.isEmpty()) {
		result.insert(L("parent_id"), parentId);
	}
	return result;
}

QString FeedPath(const QString &tab, const QString &cursor) {
	const auto t = (tab == L(kTabHot) || tab == L(kTabRecommend))
		? tab
		: L(kTabLatest);
	return L("/v1/square/feed?tab=") + t + (cursor.isEmpty()
		? QString()
		: (L("&cursor=") + UrlEncode(cursor)));
}

QString WithCursor(const QString &path, const QString &cursor) {
	if (cursor.isEmpty()) {
		return path;
	}
	return path
		+ (path.contains(QChar('?')) ? QChar('&') : QChar('?'))
		+ L("cursor=")
		+ UrlEncode(cursor);
}

QString UrlEncode(const QString &value) {
	return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

QString PathId(const QString &id) {
	return UrlEncode(id);
}

QStringList ReportReasons() {
	return {
		L("spam"),
		L("scam"),
		L("porn"),
		L("violence"),
		L("hate"),
		L("impersonation"),
		L("other"),
	};
}

QJsonObject ReportBody(
		const QString &target,
		const QString &id,
		const QString &reason) {
	auto result = QJsonObject();
	result.insert(L("target"), target);
	result.insert(L("id"), id);
	result.insert(L("reason"), reason);
	return result;
}

QString BotUsername(const QString &link) {
	auto s = link.trimmed();
	const auto i = s.indexOf(L("t.me/"));
	if (i < 0) {
		return QString();
	}
	s = s.mid(i + 5);
	auto end = int(s.size());
	for (auto k = 0; k != int(s.size()); ++k) {
		const auto c = s[k];
		if (c == QChar('?') || c == QChar('/') || c == QChar('#')) {
			end = k;
			break;
		}
	}
	s = s.left(end);
	static const auto valid = QRegularExpression(
		L("^[A-Za-z0-9_]{4,32}$"));
	return valid.match(s).hasMatch() ? s : QString();
}

QString StartCommand(const QString &nonce) {
	return L("/start ") + nonce;
}

QString Texts::t(const char *zh, const char *en) const {
	return U(_zh ? zh : en);
}

QString Texts::title() const { return t("广场", "Square"); }
QString Texts::latest() const { return t("最新", "Latest"); }
QString Texts::hot() const { return t("热门", "Hot"); }
QString Texts::recommend() const { return t("推荐", "For you"); }
QString Texts::notInterested() const { return t("不感兴趣", "Not interested"); }
QString Texts::notInterestedDone() const { return t("将减少推荐类似内容", "You'll see fewer posts like this"); }
QString Texts::compose() const { return t("发帖", "Post"); }
QString Texts::loadMore() const { return t("加载更多", "Load more"); }
QString Texts::loading() const { return t("正在加载…", "Loading…"); }
QString Texts::empty() const { return t("还没有动态，来发第一条吧", "No posts yet. Be the first to share something."); }
QString Texts::noMore() const { return t("已经到底了", "You're all caught up"); }
QString Texts::refresh() const { return t("刷新", "Refresh"); }
QString Texts::refreshing() const { return t("正在刷新", "Refreshing"); }

QString Texts::refreshed(int n) const {
	if (n <= 0) {
		return t("已是最新内容", "You're up to date");
	} else if (_zh) {
		return U("更新了 %1 条新内容").arg(n);
	}
	return (n == 1) ? L("1 new post") : L("%1 new posts").arg(n);
}

QString Texts::loadFailed() const { return t("加载失败，请检查网络后重试", "Couldn't load. Check your connection and try again."); }
QString Texts::retry() const { return t("重试", "Try again"); }
QString Texts::moreFailed() const { return t("加载失败，点击重试", "Couldn't load more. Click to retry."); }

QString Texts::hidden() const { return t("已隐藏", "Hidden"); }

QString Texts::hiddenInfo(const QString &reason) const {
	const auto base = t(
		"这条动态已被隐藏，只有你自己能看到。",
		"This post has been hidden and is visible only to you.");
	if (reason.isEmpty()) {
		return base;
	}
	const auto shown = ReportReasons().contains(reason)
		? reportReason(reason)
		: reason;
	return base + QChar(' ') + t("原因：", "Reason: ") + shown;
}

QString Texts::likes(int n) const {
	return _zh
		? U("赞 %1").arg(n)
		: ((n == 1) ? L("1 like") : L("%1 likes").arg(n));
}

QString Texts::comments(int n) const {
	return _zh
		? U("评论 %1").arg(n)
		: ((n == 1) ? L("1 comment") : L("%1 comments").arg(n));
}

QString Texts::like() const { return t("赞", "Like"); }
QString Texts::unlike() const { return t("取消赞", "Unlike"); }
QString Texts::comment() const { return t("评论", "Comment"); }
QString Texts::reply() const { return t("回复", "Reply"); }
QString Texts::replyTo(const QString &name) const { return t("回复 ", "Reply to ") + name; }
QString Texts::commentHint() const { return t("写下你的评论（最多 300 字）", "Write a comment (up to 300 characters)"); }
QString Texts::send() const { return t("发送", "Send"); }
QString Texts::deleteText() const { return t("删除", "Delete"); }
QString Texts::deletePostConfirm() const { return t("删除这条动态？删除后无法恢复。", "Delete this post? This can't be undone."); }
QString Texts::deleteCommentConfirm() const { return t("删除这条评论？", "Delete this comment?"); }
QString Texts::deleted() const { return t("已删除", "Deleted"); }
QString Texts::noComments() const { return t("还没有评论，来说两句吧", "No comments yet. Start the conversation."); }
QString Texts::commentsHeader() const { return t("评论", "Comments"); }
QString Texts::commentsLoading() const { return t("正在加载评论…", "Loading comments…"); }
QString Texts::replyingTo(const QString &name) const { return t("正在回复 ", "Replying to ") + name; }
QString Texts::cancelReply() const { return t("取消回复", "Cancel"); }
QString Texts::commentSent() const { return t("评论已发布", "Comment posted"); }
QString Texts::postGone() const { return t("这条动态已不存在", "This post is no longer available"); }
QString Texts::noPublicUsername() const { return t("对方没有公开用户名，暂时无法发消息", "This person hasn't set a public username, so you can't message them yet"); }
QString Texts::authorGone() const { return t("找不到这位用户", "User not found"); }

QString Texts::composeTitle() const { return t("发动态", "New post"); }
QString Texts::textHint() const { return t("分享新鲜事…（最多 1000 字）", "Share what's new… (up to 1000 characters)"); }
QString Texts::addAttachment() const { return t("添加图片或视频", "Add photos or video"); }

QString Texts::addImages(int n) const {
	return _zh
		? U("添加图片（%1/9）").arg(n)
		: L("Add photos (%1/9)").arg(n);
}

QString Texts::chooseImages() const { return t("从相册选图片", "Choose photos"); }
QString Texts::chooseVideo() const { return t("从相册选视频", "Choose a video"); }
QString Texts::removeImages() const { return t("移除全部图片", "Remove all photos"); }
QString Texts::removeVideo() const { return t("移除视频", "Remove video"); }

QString Texts::videoLabel(int seconds) const {
	return t("视频 · ", "Video · ") + FormatDuration(seconds);
}

QString Texts::publish() const { return t("发布", "Post"); }
QString Texts::publishing() const { return t("正在发布…", "Posting…"); }

QString Texts::uploadingVideo(int percent) const {
	const auto base = t("正在上传视频…", "Uploading video…");
	return (percent < 0) ? base : (base + QChar(' ') + L("%1%").arg(percent));
}

QString Texts::videoTitle() const { return t("视频", "Video"); }
QString Texts::play() const { return t("播放", "Play"); }
QString Texts::pause() const { return t("暂停", "Pause"); }
QString Texts::replay() const { return t("重播", "Replay"); }
QString Texts::published() const { return t("已发布", "Posted"); }

QString Texts::attachmentsInfo(qint64 maxSeconds) const {
	return _zh
		? U("最多 9 张图片，或 1 段不超过 %1 秒的视频。图片会压缩到长边 2048 像素，视频会转码为 1280p 以内的 MP4；位置等元数据都会去掉后再上传。").arg(maxSeconds)
		: L("Up to 9 photos, or one video of up to %1 seconds. Photos are resized to 2048 px on the long edge and videos are transcoded to MP4 at up to 1280p; location and other metadata are removed before upload.").arg(maxSeconds);
}

QString Texts::tooLong() const { return t("内容超过了 1000 字", "The text is over 1000 characters"); }
QString Texts::needContent() const { return t("写点什么，或添加图片 / 视频", "Add some text, photos or a video"); }
QString Texts::imageFailed() const { return t("有图片无法处理，请换一张试试", "One of the photos couldn't be processed. Please try another."); }
QString Texts::preparingImages() const { return t("正在处理图片…", "Preparing photos…"); }
QString Texts::preparingVideo() const { return t("正在处理视频…", "Preparing video…"); }
QString Texts::videoFailed() const { return t("视频无法处理，请换一段试试", "The video couldn't be processed. Please try another."); }

QString Texts::videoTooBig(qint64 maxMb) const {
	return _zh
		? U("转码后仍超过 %1 MB，请选一段更短的视频").arg(maxMb)
		: L("The video is still over %1 MB after compression. Please pick a shorter one.").arg(maxMb);
}

QString Texts::videoRejected() const { return t("视频不符合要求（时长、大小或格式），请换一段试试", "The video doesn't meet the requirements (length, size or format). Please try another."); }
QString Texts::squareDisabled() const { return t("广场暂时不可用，请稍后再试", "Square is temporarily unavailable. Please try again later."); }
QString Texts::videoTooLongTitle() const { return t("视频超过时长限制", "Video is too long"); }

QString Texts::videoTooLong(qint64 maxSeconds, int actualSeconds) const {
	const auto actual = FormatDuration(actualSeconds);
	return _zh
		? U("广场视频最长 %1 秒，这段视频有 %2。可以只保留开头 %1 秒继续发布。").arg(maxSeconds).arg(actual)
		: L("Videos can be up to %1 seconds; this one is %2. You can keep the first %1 seconds and continue.").arg(maxSeconds).arg(actual);
}

QString Texts::trimAndContinue(qint64 maxSeconds) const {
	return _zh
		? U("保留前 %1 秒").arg(maxSeconds)
		: L("Keep first %1 s").arg(maxSeconds);
}

QString Texts::onlyOneAttachmentType() const { return t("一条动态只能带图片或视频中的一种", "A post can have either photos or a video, not both"); }

QString Texts::dailyLimitInfo(qint64 perDay) const {
	return _zh
		? U("每天最多发布 %1 条动态。").arg(perDay)
		: L("You can post up to %1 times a day.").arg(perDay);
}

QString Texts::profileTitle() const { return t("广场资料", "Square profile"); }
QString Texts::profileNeeded() const { return t("先设置一下在广场显示的名字和简介，其他人会在你的动态上看到。", "Set the name and bio shown with your posts in Square."); }
QString Texts::displayName() const { return t("显示名", "Name"); }
QString Texts::bio() const { return t("简介", "Bio"); }
QString Texts::publicUsername() const { return t("公开用户名", "Public username"); }

QString Texts::useMyUsername(const QString &username) const {
	return _zh
		? U("在广场显示 @%1，其他人可以通过它给你发消息").arg(username)
		: L("Show @%1 in Square so others can message you").arg(username);
}

QString Texts::showUsername() const { return t("显示", "Show"); }
QString Texts::useTelegramAvatar() const { return t("使用我的 Telegram 头像", "Use my Telegram photo"); }
QString Texts::hideUsername() const { return t("不显示", "Don't show"); }
QString Texts::save() const { return t("保存", "Save"); }
QString Texts::saved() const { return t("已保存", "Saved"); }
QString Texts::editProfile() const { return t("编辑广场资料", "Edit Square profile"); }

QString Texts::authorPosts() const { return t("动态", "Posts"); }
QString Texts::sendMessage() const { return t("发消息", "Message"); }

QString Texts::connecting() const { return t("正在连接广场…", "Connecting to Square…"); }
QString Texts::connectFailed() const { return t("暂时无法连接广场，请稍后再试", "Square isn't reachable right now. Please try again later."); }
QString Texts::cancel() const { return t("取消", "Cancel"); }

QString Texts::report() const { return t("举报", "Report"); }
QString Texts::reportPost() const { return t("举报这条动态", "Report post"); }
QString Texts::reportComment() const { return t("举报这条评论", "Report comment"); }
QString Texts::reportAuthor() const { return t("举报该用户", "Report user"); }
QString Texts::reportTitle() const { return t("举报原因", "Why are you reporting this?"); }

QString Texts::reportReason(const QString &reason) const {
	if (reason == L("spam")) {
		return t("垃圾广告", "Spam");
	} else if (reason == L("scam")) {
		return t("诈骗", "Scam or fraud");
	} else if (reason == L("porn")) {
		return t("色情内容", "Sexual content");
	} else if (reason == L("violence")) {
		return t("暴力内容", "Violence");
	} else if (reason == L("hate")) {
		return t("仇恨言论", "Hate speech");
	} else if (reason == L("impersonation")) {
		return t("冒充他人", "Impersonation");
	}
	return t("其他", "Something else");
}

QString Texts::reported() const { return t("已收到你的举报，感谢反馈", "Thanks, your report has been received"); }
QString Texts::block() const { return t("拉黑该用户", "Block user"); }

QString Texts::blockConfirm(const QString &name) const {
	return _zh
		? U("拉黑后，你和 %1 在广场和「附近的人」里都不会再看到对方。").arg(name)
		: L("You and %1 will no longer see each other in Square or People Nearby.").arg(name);
}

QString Texts::blocked() const { return t("已拉黑", "Blocked"); }
QString Texts::more() const { return t("更多", "More"); }

QString Texts::deleteMyData() const { return t("删除我的广场数据", "Delete my Square data"); }
QString Texts::deleteMyDataConfirm() const { return t("将删除你在广场发布的全部动态、评论、点赞和媒体文件，无法恢复。", "This permanently deletes all your Square posts, comments, likes and media."); }
QString Texts::deletedMyData() const { return t("你的广场数据已删除", "Your Square data has been deleted"); }

QString Texts::waitSeconds(qint64 seconds) const {
	if (seconds <= 0) {
		return t("操作太频繁，请稍后再试", "You're doing that too often. Please try again later.");
	} else if (seconds < 60) {
		return _zh
			? U("操作太频繁，请 %1 秒后再试").arg(seconds)
			: L("You're doing that too often. Try again in %1 s.").arg(seconds);
	}
	const auto minutes = (seconds + 59) / 60;
	if (minutes < 60) {
		return _zh
			? U("操作太频繁，请 %1 分钟后再试").arg(minutes)
			: L("You're doing that too often. Try again in %1 min.").arg(minutes);
	}
	const auto hours = (minutes + 59) / 60;
	return _zh
		? U("今天的发布次数已用完，请 %1 小时后再试").arg(hours)
		: L("You've reached today's posting limit. Try again in %1 h.").arg(hours);
}

QString Texts::banned() const { return t("你的账号暂时无法在广场发布内容", "Your account can't post in Square at the moment"); }

QString Texts::timeAgo(qint64 createdAtMs, qint64 nowMs) const {
	if (createdAtMs <= 0) {
		return QString();
	}
	const auto s = std::max(qint64(0), (nowMs - createdAtMs) / 1000);
	if (s < 60) {
		return t("刚刚", "Just now");
	} else if (s < 3600) {
		return _zh ? U("%1 分钟前").arg(s / 60) : L("%1 min ago").arg(s / 60);
	} else if (s < 86400) {
		return _zh ? U("%1 小时前").arg(s / 3600) : L("%1 h ago").arg(s / 3600);
	}
	return _zh ? U("%1 天前").arg(s / 86400) : L("%1 d ago").arg(s / 86400);
}

} // namespace Luckgram::Square
