/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <vector>

// D7: pure logic of the ✦ features in the message field (mobile 20 AI
// reply, 21 AI writing, 24 AI post). QtCore only (luckgram_tests); same
// rules and request texts as Android AiReplyText / AiWritingEntities /
// AiPostText.
namespace Luckgram::AI {

// The "task" field of POST /v1/ai/chat.
inline constexpr auto kTaskChat = "chat";
inline constexpr auto kTaskSummary = "summary";
inline constexpr auto kTaskWriting = "writing";
inline constexpr auto kTaskReply = "reply";
inline constexpr auto kTaskPost = "post";
inline constexpr auto kTaskAgent = "agent";

// The default of the remote value ai.max_input_chars.
inline constexpr auto kDefaultMaxInputChars = 60000;

} // namespace Luckgram::AI

namespace Luckgram::AI::Writing {

// 21 需求 4.
inline constexpr auto kMaxChars = 4000;

// 21 需求 1: the five effects, in menu order. "effect" is sent as is
// (server prompts/writing-<effect>.md).
struct Effect {
	const char *id = "";
	const char *titleZh = "";
	const char *titleEn = "";
	int order = 0; // Menu position; other items (20, 24) sort in by it.
	const char *doneZh = ""; // "已润色 · 撤销"
	const char *doneEn = "";
};
[[nodiscard]] const std::vector<Effect> &Effects();

// For each formatted range {start, end} of original (mentions, links,
// custom emoji, bold...), its new start in result (length unchanged), or -1
// when the AI changed that text. Ranges are looked up by their exact text
// in the original order; the same text at the same place is claimed once.
[[nodiscard]] std::vector<int> Relocate(
	const QString &original,
	const std::vector<std::pair<int, int>> &ranges,
	const QString &result);

} // namespace Luckgram::AI::Writing

namespace Luckgram::AI::Reply {

// 20 需求 2.
inline constexpr auto kContextMessages = 20;
inline constexpr auto kMaxMessageChars = 1000;
inline constexpr auto kCandidates = 3;

// Menu position: first (21 需求 6: "排在第一位").
inline constexpr auto kMenuOrder = 0;

// "Alice: text" (newlines flattened, cut to kMaxMessageChars).
[[nodiscard]] QString TranscriptLine(const QString &name, QString text);

// The request: transcript (oldest first, "name: text" lines) + draft.
[[nodiscard]] QString Prompt(
	const QString &transcript,
	const QString &draft,
	const QString &me);

// The candidates in streamed text: complete lines plus the one being
// written, without numbering / bullets / surrounding quotes, at most 3.
[[nodiscard]] QStringList Candidates(const QString &text);

} // namespace Luckgram::AI::Reply

namespace Luckgram::AI::Post {

// 24 需求 2.
inline constexpr auto kExamples = 20;
inline constexpr auto kMaxExampleChars = 1500;

inline constexpr auto kMenuOrder = 10;

inline constexpr auto kShorter = "Make it shorter, keeping the key "
	"information.";
inline constexpr auto kEmoji = "Add more fitting emoji, keeping the text.";
inline constexpr auto kRewrite = "Rewrite it with the same information in "
	"a fresh way.";

// 24 需求 3 "翻译（选语言）": the name sent to the model, and the label.
struct Language {
	const char *name = "";
	const char *label = "";
};
[[nodiscard]] const std::vector<Language> &Languages();

[[nodiscard]] QString Translate(const QString &language);

// Style examples cut so they stay within maxChars (at most kExamples).
[[nodiscard]] QString Examples(const QStringList &posts, int maxChars);

// A new post about topic.
[[nodiscard]] QString Generate(const QString &examples, const QString &topic);

// An edit of the current draft (24 需求 3: always based on the draft).
[[nodiscard]] QString Edit(
	const QString &examples,
	const QString &draft,
	const QString &instruction);

} // namespace Luckgram::AI::Post
