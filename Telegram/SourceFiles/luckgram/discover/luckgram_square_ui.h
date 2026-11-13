/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/discover/luckgram_square_logic.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Main {
class Account;
} // namespace Main

namespace Luckgram::Api {
struct Error;
} // namespace Luckgram::Api

// D10 (mobile 26, SquareUi): the Square pieces shared by the feed page, the
// post box and the author box: the silent link and profile before writes,
// like / report / block / delete, post and comment widgets, images.
// Browsing needs nothing; writes go through RequireLink().
namespace Luckgram::SquareUi {

[[nodiscard]] Square::Texts Tx();
[[nodiscard]] bool FeatureEnabled();
[[nodiscard]] qint64 MaxPostsPerDay();
[[nodiscard]] qint64 MaxVideoSeconds();

[[nodiscard]] QString ErrorText(const Api::Error &error);
void ShowError(
	not_null<Window::SessionController*> controller,
	const Api::Error &error);

// Open pages follow these.
[[nodiscard]] rpl::producer<Square::Post> PostUpdated();
[[nodiscard]] rpl::producer<QString> PostRemoved();
[[nodiscard]] rpl::producer<QString> AuthorBlocked();
void NotifyPostUpdated(const Square::Post &post);
void NotifyPostRemoved(const QString &postId);
// A post was just published (compose): shown at the top of 「最新」.
[[nodiscard]] rpl::producer<Square::Post> PostCreated();
void NotifyPostCreated(const Square::Post &post);

// This user's Square author id once known (delete entries), else empty.
[[nodiscard]] QString MyAuthorId();
// Learns it quietly when the device is already linked.
void PrefetchMe(not_null<Window::SessionController*> controller);

// Linked to this Telegram account (silently), then `then`.
void RequireLink(
	not_null<Window::SessionController*> controller,
	Fn<void()> then);
// Linked and has a Square profile, filling it in first if needed.
void RequireProfile(
	not_null<Window::SessionController*> controller,
	Fn<void()> then);
void EditProfile(
	not_null<Window::SessionController*> controller,
	Fn<void()> saved = nullptr);

// target: "post", "comment" or "user".
void Report(
	not_null<Window::SessionController*> controller,
	const QString &target,
	const QString &id);
void Block(
	not_null<Window::SessionController*> controller,
	const Square::Author &author);
void ToggleLike(
	not_null<Window::SessionController*> controller,
	Square::Post post);
void DeletePost(
	not_null<Window::SessionController*> controller,
	const Square::Post &post);
// "删除我的广场数据" (26 需求 8).
void DeleteMyData(not_null<Window::SessionController*> controller);

// Names of blocked people, cached for the block list (account storage).
[[nodiscard]] QString BlockedNames(not_null<Main::Account*> account);
void PutBlockedName(
	not_null<Main::Account*> account,
	const QString &id,
	const QString &name);
void RemoveBlockedName(not_null<Main::Account*> account, const QString &id);

// Cached https image (Api::Download); `done` only while `guard` lives.
void LoadImage(
	const QString &url,
	not_null<QObject*> guard,
	Fn<void(QImage)> done);

// One post: author, time, hidden notice, text, photos or video, like and
// comment counts. `full` is the post box (no "open" on the text).
[[nodiscard]] object_ptr<Ui::RpWidget> CreatePostWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	const Square::Post &post,
	bool full);

// The post with its comments, the comment field and reply.
void ShowPost(
	not_null<Window::SessionController*> controller,
	const Square::Post &post);
// Author card: photo, name, bio, "发消息" with a public username, posts.
void ShowAuthor(
	not_null<Window::SessionController*> controller,
	const Square::Author &author);

} // namespace Luckgram::SquareUi
