/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QtGlobal>

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <vector>

// D9 / mobile 06 (keyword folders): matching rules and the folder model,
// no UI or session types, so they run in luckgram_tests. Same rules as
// Android KeywordMatcher / KeywordFolder.
namespace Luckgram::Keywords {

inline constexpr auto kMaxFolders = 10;
inline constexpr auto kMaxKeywords = 10;
// Cached messages per chat checked besides the last one.
inline constexpr auto kRecentMessagesLimit = 50;
// Messages longer than this are only scanned up to this length.
inline constexpr auto kMaxScanLength = 8192;

// Case-insensitive. Keywords containing CJK characters match as substrings,
// others match whole words only ("invoice" matches "Invoice #12", not
// "invoices"). Returns the UTF-16 index of the first match at or after
// `from`, or -1.
[[nodiscard]] int IndexOf(const QString &text, const QString &keyword, int from = 0);
[[nodiscard]] bool Matches(const QString &text, const QString &keyword);
// First keyword (in folder order) found in `text`, or empty.
[[nodiscard]] QString FirstMatch(const QString &text, const QStringList &keywords);
[[nodiscard]] bool MatchesAny(const QString &text, const QStringList &keywords);

struct Range {
	int from = 0;
	int length = 0;
};
// Every keyword hit, sorted by position, overlaps merged: the highlight.
[[nodiscard]] std::vector<Range> Highlights(
	const QString &text,
	const QStringList &keywords);

enum Scope : int {
	kScopePrivate = 1,
	kScopeGroups = 2,
	kScopeChannels = 4,
	kScopeAll = kScopePrivate | kScopeGroups | kScopeChannels,
};

enum class ChatKind {
	Private, // Users, bots, secret chats.
	Group, // Legacy groups and supergroups.
	Channel,
};

[[nodiscard]] bool InScope(int scope, ChatKind kind);

struct Folder {
	int id = 0; // Local id, 1..; 0 = not saved yet.
	QString name;
	QStringList keywords;
	int scope = kScopeAll;
	int order = 0; // Position among all folders of the tab bar.

	friend inline bool operator==(const Folder &, const Folder &) = default;
};

// Keywords as typed: trimmed, empty and duplicate (case-insensitive) ones
// dropped, at most kMaxKeywords.
[[nodiscard]] QStringList CleanKeywords(const QStringList &keywords);

enum class Invalid {
	None,
	Name,
	Keywords,
	Scope,
};
[[nodiscard]] Invalid Validate(const Folder &folder);

// Storage format: same JSON fields as mobile ({id, name, keywords, scope,
// order} per folder). Bad entries are dropped, at most kMaxFolders kept.
[[nodiscard]] QByteArray Serialize(const std::vector<Folder> &folders);
[[nodiscard]] std::vector<Folder> Deserialize(const QByteArray &json);

// Id for a new folder: one more than the largest.
[[nodiscard]] int NextId(const std::vector<Folder> &folders);

} // namespace Luckgram::Keywords
