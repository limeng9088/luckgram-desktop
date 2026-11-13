/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <vector>

// D10 (mobile 27): the Discover page rows and the block list shared by
// People Nearby and Square (S10 / S11), ported from Android DiscoverEntries
// and BlockLogic (unit tested with the same cases).
namespace Luckgram::Discover {

enum class Entry {
	Square,
	Nearby,
};

// Rows in display order (Square, then People Nearby), each only while its
// switch is on.
[[nodiscard]] std::vector<Entry> VisibleEntries(bool squareOn, bool nearbyOn);

class Texts final {
public:
	explicit Texts(bool zh) : _zh(zh) {
	}

	[[nodiscard]] QString pageTitle() const;
	[[nodiscard]] QString emptyText() const;
	[[nodiscard]] QString title(Entry entry) const;
	[[nodiscard]] QString subtitle(Entry entry) const;

private:
	[[nodiscard]] QString t(const char *zh, const char *en) const;

	bool _zh = false;

};

} // namespace Luckgram::Discover

namespace Luckgram::Blocks {

inline constexpr auto kPath = "/v1/blocks";

// GET /v1/blocks?cursor=, the cursor is opaque (empty for the first page).
[[nodiscard]] QString ListPath(const QString &cursor);
[[nodiscard]] QString DeletePath(const QString &userId);
[[nodiscard]] QJsonObject BlockBody(const QString &userId);
// {items:[{user}]}; user is an id (an {id} object is tolerated), deduped.
[[nodiscard]] QStringList ParseIds(const QJsonObject &json);
// Empty on the last page.
[[nodiscard]] QString NextCursor(const QJsonObject &json);

// Local name cache (JSON object id -> name): the server only returns ids.
[[nodiscard]] QString PutName(
	const QString &cache,
	const QString &id,
	const QString &name);
[[nodiscard]] QString RemoveName(const QString &cache, const QString &id);
// Empty when unknown.
[[nodiscard]] QString Name(const QString &cache, const QString &id);

class Texts final {
public:
	explicit Texts(bool zh) : _zh(zh) {
	}

	[[nodiscard]] QString title() const;
	[[nodiscard]] QString info() const;
	[[nodiscard]] QString empty() const;
	[[nodiscard]] QString unknownUser() const;
	[[nodiscard]] QString unblock() const;
	[[nodiscard]] QString unblockConfirm(const QString &name) const;
	[[nodiscard]] QString unblocked() const;
	[[nodiscard]] QString loading() const;
	[[nodiscard]] QString loadMore() const;
	[[nodiscard]] QString retry() const;
	[[nodiscard]] QString cancel() const;
	[[nodiscard]] QString connectFailed() const;

private:
	[[nodiscard]] QString t(const char *zh, const char *en) const;

	bool _zh = false;

};

} // namespace Luckgram::Blocks
