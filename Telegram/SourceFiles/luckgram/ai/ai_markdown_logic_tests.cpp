/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"
#include "luckgram/ai/ai_markdown_logic.h"

// Same cases as Android LuckgramAiMarkdownTest.
using namespace Luckgram::AI::Markdown;

namespace {

[[nodiscard]] const Span &Only(const Result &r, Type type) {
	const Span *found = nullptr;
	for (const auto &span : r.spans) {
		if (span.type == type) {
			LG_CHECK(found == nullptr);
			found = &span;
		}
	}
	LG_CHECK(found != nullptr);
	return *found;
}

[[nodiscard]] QString Cut(const Result &r, const Span &span) {
	return r.text.mid(span.start, span.end - span.start);
}

} // namespace

LUCKGRAM_TEST(AiMarkdownPlainTextIsUnchanged) {
	const auto text = QString::fromUtf8("太阳能板把光变成电。\n第二行 2*3=6");
	const auto r = Parse(text);
	LG_EQUAL(r.text, text);
	LG_CHECK(r.spans.empty());
}

LUCKGRAM_TEST(AiMarkdownBoldItalicCodeStrike) {
	const auto r = Parse(
		QStringLiteral("a **bold** b *it* c `x = 1` d ~~no~~"));
	LG_EQUAL(r.text, QStringLiteral("a bold b it c x = 1 d no"));
	LG_EQUAL(Cut(r, Only(r, Type::Bold)), QStringLiteral("bold"));
	LG_EQUAL(Cut(r, Only(r, Type::Italic)), QStringLiteral("it"));
	LG_EQUAL(Cut(r, Only(r, Type::Code)), QStringLiteral("x = 1"));
	LG_EQUAL(Cut(r, Only(r, Type::Strike)), QStringLiteral("no"));
}

LUCKGRAM_TEST(AiMarkdownSnakeCaseIsNotItalic) {
	const auto r = Parse(QStringLiteral("use snake_case_name here"));
	LG_EQUAL(r.text, QStringLiteral("use snake_case_name here"));
	LG_CHECK(r.spans.empty());
}

LUCKGRAM_TEST(AiMarkdownCodeBlockKeepsContentVerbatim) {
	const auto r = Parse(QStringLiteral(
		"Code:\n```python\nprint(\"**hi**\")\nx = 1\n```\nDone"));
	LG_EQUAL(r.text, QStringLiteral("Code:\nprint(\"**hi**\")\nx = 1\nDone"));
	const auto &block = Only(r, Type::CodeBlock);
	LG_EQUAL(Cut(r, block), QStringLiteral("print(\"**hi**\")\nx = 1"));
	LG_EQUAL(block.value, QStringLiteral("python"));
}

LUCKGRAM_TEST(AiMarkdownUnclosedCodeBlockWhileStreaming) {
	const auto r = Parse(QStringLiteral("```\nint a;"));
	LG_EQUAL(r.text, QStringLiteral("int a;"));
	(void)Only(r, Type::CodeBlock);
}

LUCKGRAM_TEST(AiMarkdownUnclosedMarkersStayLiteral) {
	const auto r = Parse(QStringLiteral("**half and `tick"));
	LG_EQUAL(r.text, QStringLiteral("**half and `tick"));
	LG_CHECK(r.spans.empty());
}

LUCKGRAM_TEST(AiMarkdownHeadingsListsAndLinks) {
	const auto r = Parse(QString::fromUtf8(
		"## 标题\n- one\n- **two**\n"
		"[site](https://example.com) and [bad](javascript:x)"));
	LG_EQUAL(r.text, QString::fromUtf8(
		"标题\n• one\n• two\nsite and [bad](javascript:x)"));
	const auto &heading = Only(r, Type::Heading);
	LG_EQUAL(Cut(r, heading), QString::fromUtf8("标题"));
	LG_EQUAL(heading.value, QStringLiteral("2"));
	const auto &link = Only(r, Type::Link);
	LG_EQUAL(Cut(r, link), QStringLiteral("site"));
	LG_EQUAL(link.value, QStringLiteral("https://example.com"));
	LG_EQUAL(Cut(r, Only(r, Type::Bold)), QStringLiteral("two"));
}

LUCKGRAM_TEST(AiMarkdownEscapes) {
	const auto r = Parse(QStringLiteral("\\*not italic\\*"));
	LG_EQUAL(r.text, QStringLiteral("*not italic*"));
	LG_CHECK(r.spans.empty());
}

LUCKGRAM_TEST(AiMarkdownWindowsLineEndsAndEmpty) {
	LG_EQUAL(Parse(QStringLiteral("a\r\nb")).text, QStringLiteral("a\nb"));
	LG_CHECK(Parse(QString()).text.isEmpty());
}
