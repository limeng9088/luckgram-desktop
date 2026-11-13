/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"
#include "luckgram/ai/ai_compose_logic.h"

// Same cases as Android AiReplyTextTest, AiWritingEntitiesTest and
// AiPostTextTest.
namespace {

using Ranges = std::vector<std::pair<int, int>>;

} // namespace

// ---- 20 AI reply

LUCKGRAM_TEST(AiReplyThreePlainLines) {
	using namespace Luckgram::AI::Reply;
	LG_EQUAL(Candidates(QString::fromUtf8(
		"周五可以吗？\n不好意思明天去不了，周五方便吗？\n明天不行哈，周五咋样\n")),
		QStringList({
			QString::fromUtf8("周五可以吗？"),
			QString::fromUtf8("不好意思明天去不了，周五方便吗？"),
			QString::fromUtf8("明天不行哈，周五咋样"),
		}));
}

LUCKGRAM_TEST(AiReplyNumberingQuotesAndExtraLinesAreRemoved) {
	using namespace Luckgram::AI::Reply;
	LG_EQUAL(Candidates(QString::fromUtf8(
		"1. \"Sure\"\n\n2) “Thank you, see you then”\n- cool\n4. extra")),
		QStringList({
			QStringLiteral("Sure"),
			QStringLiteral("Thank you, see you then"),
			QStringLiteral("cool"),
		}));
}

LUCKGRAM_TEST(AiReplyPartialStreamShowsWhatArrived) {
	using namespace Luckgram::AI::Reply;
	LG_EQUAL(
		Candidates(QStringLiteral("Sure, fri")),
		QStringList({ QStringLiteral("Sure, fri") }));
	LG_CHECK(Candidates(QString()).isEmpty());
}

LUCKGRAM_TEST(AiReplyPromptWithAndWithoutDraft) {
	using namespace Luckgram::AI::Reply;
	const auto withDraft = Prompt(
		QString::fromUtf8("Alice: 明天的午饭还约吗？"),
		QString::fromUtf8("明天去不了 周五行不"),
		QString::fromUtf8("我"));
	LG_CHECK(withDraft.contains(QString::fromUtf8("Alice: 明天的午饭还约吗？")));
	LG_CHECK(withDraft.endsWith(QString::fromUtf8("My draft: 明天去不了 周五行不")));
	const auto noDraft = Prompt(
		QString(),
		QStringLiteral("  "),
		QStringLiteral("Me"));
	LG_CHECK(noDraft.contains(QStringLiteral("(none)")));
	LG_CHECK(noDraft.endsWith(
		QStringLiteral("I have no draft; suggest what I could reply.")));
}

LUCKGRAM_TEST(AiReplyTranscriptLine) {
	using namespace Luckgram::AI::Reply;
	LG_EQUAL(
		TranscriptLine(QStringLiteral("Bob"), QStringLiteral("a\nb")),
		QStringLiteral("Bob: a b"));
	const auto cut = TranscriptLine(
		QStringLiteral("A"),
		QString(kMaxMessageChars + 50, QChar('z')));
	LG_EQUAL(int(cut.size()), 3 + kMaxMessageChars + 1);
	LG_CHECK(cut.endsWith(QString::fromUtf8("…")));
}

// ---- 21 AI writing

LUCKGRAM_TEST(AiWritingEffectsInMenuOrder) {
	using namespace Luckgram::AI::Writing;
	const auto &effects = Effects();
	LG_EQUAL(int(effects.size()), 5);
	auto ids = QStringList();
	for (const auto &effect : effects) {
		ids.push_back(QString::fromLatin1(effect.id));
	}
	LG_EQUAL(ids, QStringList({
		QStringLiteral("polish"),
		QStringLiteral("formal"),
		QStringLiteral("friendly"),
		QStringLiteral("shorter"),
		QStringLiteral("grammar"),
	}));
	LG_CHECK(Luckgram::AI::Reply::kMenuOrder < effects.front().order);
	LG_CHECK(Luckgram::AI::Post::kMenuOrder < effects.front().order);
}

LUCKGRAM_TEST(AiWritingMentionKeptWhenNameUnchanged) {
	using namespace Luckgram::AI::Writing;
	LG_EQUAL(Relocate(
		QStringLiteral("tell Anna we are late"),
		Ranges{ { 5, 9 } },
		QStringLiteral("Please tell Anna that we're running late.")),
		std::vector<int>{ 12 });
}

LUCKGRAM_TEST(AiWritingRangeDroppedWhenTextChanged) {
	using namespace Luckgram::AI::Writing;
	LG_EQUAL(Relocate(
		QStringLiteral("this is VERY important"),
		Ranges{ { 8, 12 } },
		QStringLiteral("This is extremely important.")),
		std::vector<int>{ -1 });
}

LUCKGRAM_TEST(AiWritingRepeatedEmojiLandOnDifferentOccurrences) {
	using namespace Luckgram::AI::Writing;
	const auto original = QString::fromUtf8("ok 😀 and 😀");
	const auto emoji = QString::fromUtf8("😀");
	const auto a = int(original.indexOf(emoji));
	const auto b = int(original.lastIndexOf(emoji));
	const auto result = QString::fromUtf8("Okay 😀, and 😀!");
	LG_EQUAL(
		Relocate(original, Ranges{ { a, a + 2 }, { b, b + 2 } }, result),
		(std::vector<int>{
			int(result.indexOf(emoji)),
			int(result.lastIndexOf(emoji)),
		}));
}

LUCKGRAM_TEST(AiWritingNestedRangesBothFound) {
	using namespace Luckgram::AI::Writing;
	// Bold over "you, Anna Lee" and a mention on "Anna Lee".
	LG_EQUAL(Relocate(
		QStringLiteral("see you, Anna Lee"),
		Ranges{ { 4, 17 }, { 9, 17 } },
		QStringLiteral("See you, Anna Lee!")),
		(std::vector<int>{ 4, 9 }));
}

LUCKGRAM_TEST(AiWritingReorderedTextStillFindsRange) {
	using namespace Luckgram::AI::Writing;
	const auto result = QStringLiteral(
		"Thanks, Bob! Here is the link: https://t.me/x");
	LG_EQUAL(Relocate(
		QStringLiteral("link: https://t.me/x , thanks Bob"),
		Ranges{ { 6, 20 }, { 30, 33 } },
		result),
		(std::vector<int>{
			int(result.indexOf(QStringLiteral("https"))),
			int(result.indexOf(QStringLiteral("Bob"))),
		}));
}

LUCKGRAM_TEST(AiWritingBlankAndInvalidRangesIgnored) {
	using namespace Luckgram::AI::Writing;
	LG_EQUAL(Relocate(
		QStringLiteral("a  b"),
		Ranges{ { 1, 3 }, { 2, 9 } },
		QStringLiteral("a  b")),
		(std::vector<int>{ -1, -1 }));
}

// ---- 24 AI post

LUCKGRAM_TEST(AiPostExamplesAreLimitedToTwentyAndBudget) {
	using namespace Luckgram::AI::Post;
	auto posts = QStringList();
	for (auto i = 0; i < 30; ++i) {
		posts.push_back(QStringLiteral("post ") + QString::number(i));
	}
	const auto ex = Examples(posts, 100000);
	LG_CHECK(ex.contains(QStringLiteral("post 19")));
	LG_CHECK(!ex.contains(QStringLiteral("post 20")));
	LG_CHECK(Examples(posts, 40).size() <= 40);
}

LUCKGRAM_TEST(AiPostLongExamplesAreCutAndEmptyOnesSkipped) {
	using namespace Luckgram::AI::Post;
	const auto longPost = QString(3000, QChar('a'));
	LG_EQUAL(
		Examples({ QString(), QStringLiteral("  "), longPost }, 100000),
		QStringLiteral("---\n")
			+ longPost.left(kMaxExampleChars)
			+ QString::fromUtf8("…\n"));
}

LUCKGRAM_TEST(AiPostGenerateAndEditRequests) {
	using namespace Luckgram::AI::Post;
	const auto ex = Examples({ QString::fromUtf8("🌹 新品上架") }, 1000);
	const auto generated = Generate(ex, QString::fromUtf8(" 周末促销，玫瑰八折 "));
	LG_CHECK(generated.startsWith(
		QStringLiteral("Example posts from this channel")));
	LG_CHECK(generated.endsWith(QString::fromUtf8(
		"Topic: 周末促销，玫瑰八折\nWrite a new post for the channel.")));
	LG_EQUAL(
		Generate(QString(), QStringLiteral("x")),
		QStringLiteral("Topic: x\nWrite a new post for the channel."));
	LG_EQUAL(
		Edit(QString(), QString::fromUtf8("草稿\n"), Translate(QStringLiteral("English"))),
		QString::fromUtf8("Current draft:\n草稿\n\nInstruction: Translate it "
			"into English. Keep the formatting and emoji."));
}

LUCKGRAM_TEST(AiPostLanguages) {
	using namespace Luckgram::AI::Post;
	LG_EQUAL(int(Languages().size()), 8);
	LG_EQUAL(
		QString::fromLatin1(Languages().front().name),
		QStringLiteral("English"));
}
