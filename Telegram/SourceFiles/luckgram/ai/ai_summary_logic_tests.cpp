/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"
#include "luckgram/ai/ai_summary_logic.h"

// Same cases as Android ChatSummaryLogicTest, plus the requests and
// placeholders the Android controller builds inline.
using namespace Luckgram::AI::Summary;

LUCKGRAM_TEST(AiSummaryTranscriptLineFormat) {
	const auto parts = Split({
		{ 101, QStringLiteral("Alice"), QStringLiteral("10-31 14:05"),
			QString::fromUtf8("明天 10 点开会\n地点待定") },
		{ 102, QStringLiteral("Bob"), QStringLiteral("10-31 14:06"),
			QString::fromUtf8("[图片] 菜单") },
	}, 60000);
	LG_EQUAL(int(parts.size()), 1);
	LG_CHECK(parts[0].startsWith(Header(false)));
	LG_CHECK(parts[0].endsWith(QString::fromUtf8(
		"[#101] Alice (10-31 14:05): 明天 10 点开会 地点待定\n"
		"[#102] Bob (10-31 14:06): [图片] 菜单")));
}

LUCKGRAM_TEST(AiSummaryLongRangesAreSplitAndEveryLineKept) {
	auto lines = std::vector<Line>();
	for (auto i = 1; i <= 500; ++i) {
		lines.push_back({
			i,
			QStringLiteral("User") + QString::number(i % 7),
			QStringLiteral("10-31 12:00"),
			QStringLiteral("message number ")
				+ QString::number(i)
				+ QStringLiteral(" with some words to make it longer"),
		});
	}
	const auto parts = Split(lines, 8000);
	LG_CHECK(parts.size() > 1);
	auto found = 0;
	for (const auto &part : parts) {
		LG_CHECK(part.size() <= 8000);
		for (auto i = 1; i <= 500; ++i) {
			if (part.contains(QStringLiteral("\n[#")
					+ QString::number(i)
					+ QStringLiteral("] "))) {
				++found;
			}
		}
	}
	LG_EQUAL(found, 500);
}

LUCKGRAM_TEST(AiSummaryVeryLongMessageIsCut) {
	const auto parts = Split({
		{ 1, QStringLiteral("A"), QStringLiteral("t"), QString(5000, QChar('x')) },
	}, 60000);
	LG_CHECK(parts[0].size() < Header(false).size() + kMaxLineChars + 10);
}

LUCKGRAM_TEST(AiSummaryCitationsAreParsedAndValidated) {
	const auto s = Parse(
		QStringLiteral("Meeting moved to 10am [#101, #102]; "
			"budget agreed [#999] (#205)."),
		{ 101, 102, 205 });
	LG_EQUAL(Describe(s), QStringLiteral(
		"[Meeting moved to 10am, {101}, {102}, ; budget agreed, {205}, .]"));
}

LUCKGRAM_TEST(AiSummaryChineseBracketsAndCommas) {
	const auto s = Parse(
		QString::fromUtf8("客户答应周五付款（#7，#8）"),
		{ 7, 8 });
	LG_EQUAL(Describe(s), QString::fromUtf8("[客户答应周五付款, {7}, {8}]"));
}

LUCKGRAM_TEST(AiSummaryOrdinaryParenthesesAndBracketsStayText) {
	const auto s = Parse(
		QStringLiteral("Revenue (2024) grew, see [1]"),
		{ 5 });
	LG_EQUAL(Describe(s), QStringLiteral("[Revenue (2024) grew, see [1]]"));
}

LUCKGRAM_TEST(AiSummaryBulletsAreExtracted) {
	const auto b = Bullets(QString::fromUtf8(
		"Here you go:\n- 第一点 [#1]\n  继续说明\n* second\n2) third\n\n• fourth"));
	LG_EQUAL(b, QStringList({
		QStringLiteral("Here you go:"),
		QString::fromUtf8("第一点 [#1] 继续说明"),
		QStringLiteral("second"),
		QStringLiteral("third"),
		QStringLiteral("fourth"),
	}));
}

LUCKGRAM_TEST(AiSummaryMergeRequest) {
	const auto text = MergeRequest(
		{ QStringLiteral("- a [#1]"), QStringLiteral("- b [#2]") },
		60000);
	LG_CHECK(text.startsWith(Header(true)));
	LG_CHECK(text.endsWith(QStringLiteral(
		"Part 1:\n- a [#1]\n\nPart 2:\n- b [#2]\n\n")));
	LG_EQUAL(int(MergeRequest({ QString(100, QChar('y')) }, 50).size()), 50);
}

LUCKGRAM_TEST(AiSummaryAskTakesTheNewestLinesThatFit) {
	auto lines = std::vector<Line>();
	for (auto i = 1; i <= 300; ++i) {
		lines.push_back({
			i,
			QStringLiteral("U"),
			QStringLiteral("t"),
			QString(40, QChar('m')),
		});
	}
	const auto text = AskRequest(
		lines,
		QString::fromUtf8("客户最后答应了什么"),
		3000);
	LG_CHECK(text.startsWith(QStringLiteral("Chat messages (oldest first):\n")));
	LG_CHECK(text.contains(QStringLiteral("[#300] U (t): ")));
	LG_CHECK(!text.contains(QStringLiteral("[#1] U (t): ")));
	LG_CHECK(text.contains(QString::fromUtf8("Question: 客户最后答应了什么\n")));
	LG_CHECK(text.endsWith(QString::fromLatin1(kCiteRule)));
}

LUCKGRAM_TEST(AiSummaryMediaPlaceholders) {
	LG_EQUAL(
		LineText(Media::Photo, QString(), QString::fromUtf8(" 菜单 "), true),
		QString::fromUtf8("[图片] 菜单"));
	LG_EQUAL(
		LineText(Media::Voice, QString(), QString(), false),
		QStringLiteral("[Voice message]"));
	LG_EQUAL(
		LineText(Media::File, QStringLiteral("a.pdf"), QString(), true),
		QString::fromUtf8("[文件 a.pdf]"));
	LG_EQUAL(
		LineText(Media::File, QString(), QString(), false),
		QStringLiteral("[File]"));
	LG_EQUAL(
		LineText(Media::Poll, QStringLiteral("Lunch?"), QString(), false),
		QStringLiteral("[Poll] Lunch?"));
	LG_EQUAL(
		LineText(Media::None, QString(), QStringLiteral("hi"), true),
		QStringLiteral("hi"));
}
