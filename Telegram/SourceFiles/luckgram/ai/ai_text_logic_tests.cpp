/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"
#include "luckgram/ai/ai_compose_logic.h"
#include "luckgram/ai/ai_conversation_logic.h"
#include "luckgram/ai/ai_summary_logic.h"
#include "luckgram/ai/ai_text_logic.h"

// w-1139 review of #15 (P3): limits must not split a surrogate pair.
using namespace Luckgram::AI::Text;

LUCKGRAM_TEST(AiTextSafeLeftKeepsPairs) {
	const auto s = QStringLiteral("ab") + QString::fromUtf8("😀") + QStringLiteral("c");
	LG_EQUAL(SafeLeft(s, 2), QStringLiteral("ab"));
	LG_EQUAL(SafeLeft(s, 3), QStringLiteral("ab")); // Would cut the emoji.
	LG_EQUAL(SafeLeft(s, 4), QStringLiteral("ab") + QString::fromUtf8("😀"));
	LG_EQUAL(SafeLeft(s, 99), s);
	LG_EQUAL(SafeLeft(s, 0), QString());
	LG_EQUAL(SafeLeft(s, -1), QString());
}

LUCKGRAM_TEST(AiTextSafeRightKeepsPairs) {
	const auto s = QStringLiteral("a") + QString::fromUtf8("😀") + QStringLiteral("bc");
	LG_EQUAL(SafeRight(s, 2), QStringLiteral("bc"));
	LG_EQUAL(SafeRight(s, 3), QStringLiteral("bc")); // Would cut the emoji.
	LG_EQUAL(SafeRight(s, 4), QString::fromUtf8("😀") + QStringLiteral("bc"));
	LG_EQUAL(SafeRight(s, 99), s);
	LG_EQUAL(SafeRight(s, 0), QString());
}

LUCKGRAM_TEST(AiTextLimitsInTheLogicUseIt) {
	// 39 letters + an emoji: cutting at 40 must not leave half of it.
	const auto emoji = QString::fromUtf8("😀");
	const auto title = Luckgram::AI::Conversations::AutoTitle(
		QString(39, QChar('a')) + emoji + QStringLiteral("tail"));
	LG_EQUAL(title, QString(39, QChar('a')) + QString::fromUtf8("…"));

	const auto line = Luckgram::AI::Reply::TranscriptLine(
		QStringLiteral("A"),
		QString(Luckgram::AI::Reply::kMaxMessageChars - 1, QChar('z'))
			+ emoji
			+ QStringLiteral("more"));
	LG_CHECK(!line.contains(QChar(emoji[0])));

	auto c = Luckgram::AI::Conversations::Conversation();
	auto m = Luckgram::AI::Conversations::Message();
	m.text = QStringLiteral("x") + emoji + QString(9, QChar('y'));
	c.messages.push_back(m);
	const auto request = Luckgram::AI::Conversations::RequestMessages(c, 10);
	LG_EQUAL(request.size(), size_t(1));
	LG_EQUAL(request[0].content, QString(9, QChar('y')));
}
