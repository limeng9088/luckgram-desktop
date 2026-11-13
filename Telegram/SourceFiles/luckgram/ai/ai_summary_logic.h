/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <set>
#include <vector>

// D7 (mobile 19): the transcript sent with task=summary, splitting long
// ranges into parts that fit ai.max_input_chars, and parsing the [#123]
// message references the model puts after each point. QtCore only
// (luckgram_tests); same rules as Android ChatSummaryLogic.
namespace Luckgram::AI::Summary {

// 需求 1: the top bar shows from this many unread messages; the "…" menu
// offers these ranges besides "unread".
inline constexpr auto kMinUnreadForBar = 30;
inline constexpr int kRanges[] = { 100, 300, 500 };

// How references must look; added to every request (the server's prompt
// doesn't ask for them).
inline constexpr auto kCiteRule = "After each point, cite the message IDs "
	"it is based on, like [#123] or [#123, #456]. Use only IDs that appear "
	"in the messages.";

// Lines longer than this are cut, so one long message can't take a part.
inline constexpr auto kMaxLineChars = 2000;

// One chat message as the model sees it.
struct Line {
	qint64 id = 0;
	QString sender;
	QString time; // "10-31 14:05"
	QString text; // Text, or a media placeholder like "[图片] caption".
};

[[nodiscard]] QString FormatLine(const Line &line);
[[nodiscard]] QString Header(bool merge);

// Splits the lines (oldest first) into transcripts of at most maxChars
// characters each, header included. At least one part when there are lines.
[[nodiscard]] QStringList Split(const std::vector<Line> &lines, int maxChars);

// The request text merging the summaries of several parts (需求 5), cut to
// maxChars.
[[nodiscard]] QString MergeRequest(
	const QStringList &partSummaries,
	int maxChars);

// 需求 4: a question about the loaded range (task=chat). Takes the most
// recent lines that fit maxChars.
[[nodiscard]] QString AskRequest(
	const std::vector<Line> &lines,
	const QString &question,
	int maxChars);

// 需求 2: media placeholders, same words as mobile.
enum class Media {
	None,
	Photo,
	Voice,
	VideoMessage,
	Sticker,
	Gif,
	Video,
	Music,
	File, // detail: file name (may be empty)
	Location,
	Contact,
	Poll, // detail: the question
};
[[nodiscard]] QString MediaPlaceholder(
	Media media,
	const QString &detail,
	bool chinese);
// "[图片] caption", or just the text / the placeholder.
[[nodiscard]] QString LineText(
	Media media,
	const QString &detail,
	const QString &text,
	bool chinese);

// A piece of an answer: plain text, or a reference to messageId.
struct Segment {
	QString text;
	qint64 messageId = 0; // 0 for plain text.

	friend inline bool operator==(const Segment &a, const Segment &b) {
		return (a.text == b.text) && (a.messageId == b.messageId);
	}
};

// Splits text into plain text and references. References to IDs not in
// validIds are dropped (需求 3: the client checks the ID is in the range).
[[nodiscard]] std::vector<Segment> Parse(
	const QString &text,
	const std::set<qint64> &validIds);

// "[a, {101}, {102}, b]", for tests and logs.
[[nodiscard]] QString Describe(const std::vector<Segment> &segments);

// The bullet points of a summary, without "- " / "• " / "1. " markers.
[[nodiscard]] QStringList Bullets(const QString &text);

} // namespace Luckgram::AI::Summary
