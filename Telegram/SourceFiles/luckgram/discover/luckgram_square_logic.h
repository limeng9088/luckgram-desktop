/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <optional>
#include <vector>

// D10 (mobile 26, server S11): pure logic of Square, ported from Android
// SquareLogic: response parsing, limits, image and video sizing, comment
// threading, permissions, the silent S6 link and all texts. QtCore only,
// unit tested in luckgram_tests (cases from SquareLogicTest).
namespace Luckgram::Square {

inline constexpr auto kFeatureKey = "square";

inline constexpr auto kTextMax = 1000;
inline constexpr auto kCommentMax = 300;
inline constexpr auto kImagesMax = 9;
inline constexpr auto kPageSize = 20;
// Long edge after client-side compression (26 item 3).
inline constexpr auto kImageMaxEdge = 2048;
inline constexpr auto kProfileBioMax = 70;
// Built-in defaults; the remote config values win.
inline constexpr auto kDefaultMaxPostsPerDay = 10;
inline constexpr auto kDefaultMaxVideoSeconds = 60;
inline constexpr auto kVideoMaxEdge = 1280;
inline constexpr auto kDefaultMaxVideoMb = 50;
inline constexpr auto kImageContentType = "image/jpeg";
inline constexpr auto kVideoContentType = "video/mp4";

inline constexpr auto kTabLatest = "latest";
inline constexpr auto kTabHot = "hot";
inline constexpr auto kTabRecommend = "recommend"; // specs#115.

inline constexpr auto kStatusPublished = "published";
inline constexpr auto kStatusHidden = "hidden";

// Remote config values.
inline constexpr auto kConfigMaxPostsPerDay = "square.max_posts_per_day";
inline constexpr auto kConfigMaxVideoSeconds = "square.max_video_seconds";
inline constexpr auto kConfigMaxVideoMb = "square.max_video_mb";

struct Author {
	QString id;
	QString displayName;
	QString avatarUrl; // https only, else empty.
	QString username; // Empty when not public.
	QString bio;
};

struct Image {
	QString url;
	int width = 0;
	int height = 0;
};

struct Video {
	QString url;
	QString coverUrl;
	int width = 0;
	int height = 0;
	int durationSeconds = 0;
};

struct Post {
	QString id;
	Author author;
	QString text;
	std::vector<Image> images;
	std::optional<Video> video;
	int likeCount = 0;
	int commentCount = 0;
	bool liked = false;
	qint64 createdAtMs = 0;
	QString status = QString::fromLatin1(kStatusPublished);
	QString hiddenReason; // Only sent to the author.

	[[nodiscard]] bool isHidden() const;
};

struct Comment {
	QString id;
	QString postId;
	QString parentId; // Empty for a top level comment.
	Author author;
	QString text;
	qint64 createdAtMs = 0;
	QString status = QString::fromLatin1(kStatusPublished);
	QString hiddenReason;
	int depth = 0; // Set by Thread(): 0 = comment, 1 = reply.
};

template <typename T>
struct Page {
	std::vector<T> items;
	QString nextCursor; // Empty on the last page.
};

// One presigned upload slot from POST /v1/square/uploads.
struct Upload {
	QString key;
	QString url;
};

[[nodiscard]] Author ParseAuthor(const QJsonObject &o);
[[nodiscard]] std::optional<Video> ParseVideo(const QJsonObject &o);
[[nodiscard]] Post ParsePost(const QJsonObject &o);
[[nodiscard]] Comment ParseComment(const QJsonObject &o);
[[nodiscard]] Page<Post> ParsePosts(const QJsonObject &j);
[[nodiscard]] Page<Comment> ParseComments(const QJsonObject &j);
// {"uploads":[{"key","url"}]}; entries without an https url are dropped.
[[nodiscard]] std::vector<Upload> ParseUploads(const QJsonObject &j);
// Anything but "hidden" is shown as published.
[[nodiscard]] QString NormalizeStatus(const QString &status);
// RFC 3339 / ISO 8601 with or without milliseconds; 0 when unreadable.
[[nodiscard]] qint64 ParseTime(const QString &value);

// Appends a page without duplicates (cursor pages may overlap).
void AppendUnique(std::vector<Post> &into, const std::vector<Post> &page);

// ---- Saved first page (shown at once when the page opens).

inline constexpr auto kSavedMaxAgeMs = qint64(7) * 24 * 3600 * 1000;

struct Saved {
	Page<Post> page;
	qint64 savedAtMs = 0;
};

[[nodiscard]] QString EncodeSaved(const QJsonObject &firstPage, qint64 now);
// Nothing when unreadable, empty, too old or from the future.
[[nodiscard]] std::optional<Saved> DecodeSaved(const QString &stored, qint64 now);

// ---- Feed state of one tab, kept while the page is open. A refresh never
// empties the list: the posts stay until the new first page has arrived;
// a failed refresh leaves them untouched.

class Feed final {
public:
	static constexpr auto kStaleMs = qint64(5) * 60 * 1000;
	static constexpr auto kPrefetch = 4;
	static constexpr auto kRequestTimeoutMs = qint64(15) * 1000;

	enum class Footer {
		Skeleton,
		FirstError,
		Empty,
		LoadingMore,
		MoreError,
		End,
	};

	explicit Feed(QString tab);

	QString tab;
	std::vector<Post> posts;
	QString nextCursor;
	bool loadedOnce = false;
	qint64 loadedAtMs = 0;
	qint64 seenAtMs = 0;
	bool refreshing = false;
	bool refreshFailed = false;
	bool loadingMore = false;
	bool moreFailed = false;
	int generation = 0;
	int scrollTop = 0;

	// False when a refresh is already running.
	bool beginRefresh();
	// Replaces the list; returns how many posts were not there before (0 on
	// the very first load).
	int applyRefresh(const Page<Post> &page, qint64 now);
	// A saved first page, shown before anything is requested.
	bool restore(const Page<Post> &page, qint64 savedAtMs);
	void cancelLoading();
	void failRefresh();
	[[nodiscard]] bool canLoadMore() const;
	[[nodiscard]] bool shouldLoadMore(int lastVisiblePost) const;
	// Returns the generation to pass to applyMore() / failMore().
	int beginMore();
	// False when the page belongs to a list replaced meanwhile.
	bool applyMore(const Page<Post> &page, int forGeneration);
	void failMore(int forGeneration);
	void markSeen(qint64 now);
	[[nodiscard]] bool isStale(qint64 now) const;
	// New counts in place; nothing added, removed or moved. Returns how many
	// posts changed.
	int mergeSilently(const std::vector<Post> &fresh);
	bool removePost(const QString &postId);
	int removeAuthor(const QString &authorId);
	[[nodiscard]] Footer footer() const;

};

// One level of comments plus one level of replies, in server order;
// replies to a reply go under the top comment, orphans become comments.
[[nodiscard]] std::vector<Comment> Thread(std::vector<Comment> comments);
// Top level comment id to send as parent_id when replying to `c`.
[[nodiscard]] QString ReplyParentId(
	const std::vector<Comment> &all,
	const Comment &c);

[[nodiscard]] bool CanDeletePost(const Post &p, const QString &myId);
// The comment's author, or the author of the post it is under.
[[nodiscard]] bool CanDeleteComment(
	const Comment &c,
	const Post *post,
	const QString &myId);

// Unicode code points.
[[nodiscard]] int Length(const QString &s);
// Text <= 1000; either up to 9 images or one video; something must be there.
[[nodiscard]] bool IsValidPost(const QString &text, int images, bool hasVideo);
[[nodiscard]] bool IsValidComment(const QString &text);

struct Size {
	int width = 0;
	int height = 0;

	friend inline bool operator==(const Size &a, const Size &b) {
		return (a.width == b.width) && (a.height == b.height);
	}
};

[[nodiscard]] Size ScaledSize(int width, int height, int maxEdge = kImageMaxEdge);
// Long edge <= 1280, both sides even (H.264).
[[nodiscard]] Size VideoSize(int width, int height);
[[nodiscard]] int SampleSize(int width, int height);
// ~2.5 Mbps at 1280x720, scaled with the area, never above the source.
[[nodiscard]] int VideoBitrate(int width, int height, int sourceBitrate);
// Rounded down: 75.02 s is "1:15".
[[nodiscard]] int WholeSeconds(qint64 millis);
// "0:07", "1:00", "12:34".
[[nodiscard]] QString FormatDuration(int seconds);

[[nodiscard]] QJsonObject PostBody(const QString &text, const QStringList &imageKeys);
[[nodiscard]] QJsonObject PostBodyWithVideo(
	const QString &text,
	const QString &videoKey,
	const QString &coverKey);
[[nodiscard]] QJsonObject UploadsBody(
	const QString &contentType,
	const std::vector<qint64> &sizes);
[[nodiscard]] QJsonObject CommentBody(const QString &text, const QString &parentId);
[[nodiscard]] QString FeedPath(const QString &tab, const QString &cursor);
[[nodiscard]] QString WithCursor(const QString &path, const QString &cursor);
[[nodiscard]] QString UrlEncode(const QString &value);
[[nodiscard]] QString PathId(const QString &id);

[[nodiscard]] QStringList ReportReasons();
[[nodiscard]] QJsonObject ReportBody(
	const QString &target,
	const QString &id,
	const QString &reason);

// Silent S6 link: the bot from a t.me/<bot>?start=<nonce> link.
[[nodiscard]] QString BotUsername(const QString &link);
[[nodiscard]] QString StartCommand(const QString &nonce);

class Texts final {
public:
	explicit Texts(bool zh) : _zh(zh) {
	}

	[[nodiscard]] QString title() const;
	[[nodiscard]] QString latest() const;
	[[nodiscard]] QString hot() const;
	[[nodiscard]] QString recommend() const;
	[[nodiscard]] QString notInterested() const;
	[[nodiscard]] QString notInterestedDone() const;
	[[nodiscard]] QString compose() const;
	[[nodiscard]] QString loadMore() const;
	[[nodiscard]] QString loading() const;
	[[nodiscard]] QString empty() const;
	[[nodiscard]] QString noMore() const;
	[[nodiscard]] QString refresh() const;
	[[nodiscard]] QString refreshing() const;
	[[nodiscard]] QString refreshed(int n) const;
	[[nodiscard]] QString loadFailed() const;
	[[nodiscard]] QString retry() const;
	[[nodiscard]] QString moreFailed() const;

	[[nodiscard]] QString hidden() const;
	[[nodiscard]] QString hiddenInfo(const QString &reason) const;
	[[nodiscard]] QString likes(int n) const;
	[[nodiscard]] QString comments(int n) const;
	[[nodiscard]] QString like() const;
	[[nodiscard]] QString unlike() const;
	[[nodiscard]] QString comment() const;
	[[nodiscard]] QString reply() const;
	[[nodiscard]] QString replyTo(const QString &name) const;
	[[nodiscard]] QString commentHint() const;
	[[nodiscard]] QString send() const;
	[[nodiscard]] QString deleteText() const;
	[[nodiscard]] QString deletePostConfirm() const;
	[[nodiscard]] QString deleteCommentConfirm() const;
	[[nodiscard]] QString deleted() const;
	[[nodiscard]] QString noComments() const;
	[[nodiscard]] QString commentsHeader() const;
	[[nodiscard]] QString commentsLoading() const;
	[[nodiscard]] QString replyingTo(const QString &name) const;
	[[nodiscard]] QString cancelReply() const;
	[[nodiscard]] QString commentSent() const;
	[[nodiscard]] QString postGone() const;
	[[nodiscard]] QString noPublicUsername() const;
	[[nodiscard]] QString authorGone() const;

	[[nodiscard]] QString composeTitle() const;
	[[nodiscard]] QString textHint() const;
	[[nodiscard]] QString addAttachment() const;
	[[nodiscard]] QString addImages(int n) const;
	[[nodiscard]] QString chooseImages() const;
	[[nodiscard]] QString chooseVideo() const;
	[[nodiscard]] QString removeImages() const;
	[[nodiscard]] QString removeVideo() const;
	[[nodiscard]] QString videoLabel(int seconds) const;
	[[nodiscard]] QString publish() const;
	[[nodiscard]] QString publishing() const;
	[[nodiscard]] QString uploadingVideo(int percent = -1) const;
	[[nodiscard]] QString videoTitle() const;
	[[nodiscard]] QString play() const;
	[[nodiscard]] QString pause() const;
	[[nodiscard]] QString replay() const;
	[[nodiscard]] QString published() const;
	[[nodiscard]] QString attachmentsInfo(qint64 maxSeconds) const;
	[[nodiscard]] QString tooLong() const;
	[[nodiscard]] QString needContent() const;
	[[nodiscard]] QString imageFailed() const;
	[[nodiscard]] QString preparingImages() const;
	[[nodiscard]] QString preparingVideo() const;
	[[nodiscard]] QString videoFailed() const;
	[[nodiscard]] QString videoTooBig(qint64 maxMb) const;
	[[nodiscard]] QString videoRejected() const;
	[[nodiscard]] QString squareDisabled() const;
	[[nodiscard]] QString videoTooLongTitle() const;
	[[nodiscard]] QString videoTooLong(qint64 maxSeconds, int actualSeconds) const;
	[[nodiscard]] QString trimAndContinue(qint64 maxSeconds) const;
	[[nodiscard]] QString onlyOneAttachmentType() const;
	[[nodiscard]] QString dailyLimitInfo(qint64 perDay) const;

	[[nodiscard]] QString profileTitle() const;
	[[nodiscard]] QString profileNeeded() const;
	[[nodiscard]] QString displayName() const;
	[[nodiscard]] QString bio() const;
	[[nodiscard]] QString publicUsername() const;
	[[nodiscard]] QString useMyUsername(const QString &username) const;
	[[nodiscard]] QString showUsername() const;
	[[nodiscard]] QString useTelegramAvatar() const;
	[[nodiscard]] QString hideUsername() const;
	[[nodiscard]] QString save() const;
	[[nodiscard]] QString saved() const;
	[[nodiscard]] QString editProfile() const;

	[[nodiscard]] QString authorPosts() const;
	[[nodiscard]] QString sendMessage() const;

	[[nodiscard]] QString connecting() const;
	[[nodiscard]] QString connectFailed() const;
	[[nodiscard]] QString cancel() const;

	[[nodiscard]] QString report() const;
	[[nodiscard]] QString reportPost() const;
	[[nodiscard]] QString reportComment() const;
	[[nodiscard]] QString reportAuthor() const;
	[[nodiscard]] QString reportTitle() const;
	[[nodiscard]] QString reportReason(const QString &reason) const;
	[[nodiscard]] QString reported() const;
	[[nodiscard]] QString block() const;
	[[nodiscard]] QString blockConfirm(const QString &name) const;
	[[nodiscard]] QString blocked() const;
	[[nodiscard]] QString more() const;

	[[nodiscard]] QString deleteMyData() const;
	[[nodiscard]] QString deleteMyDataConfirm() const;
	[[nodiscard]] QString deletedMyData() const;

	[[nodiscard]] QString waitSeconds(qint64 seconds) const;
	[[nodiscard]] QString banned() const;
	[[nodiscard]] QString timeAgo(qint64 createdAtMs, qint64 nowMs) const;

private:
	[[nodiscard]] QString t(const char *zh, const char *en) const;

	bool _zh = false;

};

} // namespace Luckgram::Square
