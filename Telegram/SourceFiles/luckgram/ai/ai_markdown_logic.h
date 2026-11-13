/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QString>

#include <vector>

// D7 (mobile 18 需求 4): minimal Markdown of AI replies. Code blocks (```),
// headings (#), bullet lists, **bold**, *italic* / _italic_, `code`,
// ~~strike~~ and [links](url). Parse() returns the display text plus style
// ranges (UTF-16 offsets, like QString); the bubble turns them into text
// entities. Unclosed markers stay as they are, so half-streamed text renders
// sensibly. QtCore only (luckgram_tests); same rules as Android
// LuckgramAiMarkdown.
namespace Luckgram::AI::Markdown {

enum class Type {
	Bold = 1,
	Italic = 2,
	Code = 3,
	CodeBlock = 4,
	Link = 5,
	Heading = 6,
	Strike = 7,
};

struct Span {
	Type type = Type::Bold;
	int start = 0;
	int end = 0;
	// Link target for Link, language for CodeBlock (may be empty), heading
	// level for Heading.
	QString value;
};

struct Result {
	QString text;
	std::vector<Span> spans;
};

[[nodiscard]] Result Parse(const QString &markdown);

} // namespace Luckgram::AI::Markdown
