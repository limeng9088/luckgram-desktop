/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>

// CI screenshots (#137): the Square, People Nearby, block list and account
// link requests of the CI session are answered here with made up local data
// and never reach the Luckgram API, so no real user content (names, photos,
// posts) ends up in the public CI artifacts. Text only: no image, video or
// avatar URLs, so nothing is downloaded either.
namespace Luckgram::Discover::Offline {

struct Reply {
	int status = 200;
	QByteArray body; // JSON.
};

// Whether `path` ("/v1/square/feed?tab=new") belongs to Discover (Square,
// Nearby, blocks, account link): those never go online in the CI session.
[[nodiscard]] bool Handles(const QString &path);

// The local answer for a Discover request: made up data for the reads,
// 503 upstream_unavailable for everything that would change something.
// std::nullopt when `path` is not a Discover path.
[[nodiscard]] std::optional<Reply> Answer(
	const QByteArray &method,
	const QString &path,
	qint64 nowMs);

} // namespace Luckgram::Discover::Offline
