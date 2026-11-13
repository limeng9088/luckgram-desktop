/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/quickreplies/quick_replies_logic.h"

// D6: quick replies (mobile 02), cases ported from mobile
// QuickRepliesLogicTest plus the desktop store and 「/」 query.

namespace {

using namespace Luckgram::QuickReplies;

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] QString Repeat(const QString &text, int times) {
	auto result = QString();
	for (auto i = 0; i != times; ++i) {
		result += text;
	}
	return result;
}

const auto kGrin = U("\xF0\x9F\x98\x80");
const auto kFamily = U(
	"\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7");
const auto kThumbSkin = U("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD");
const auto kFlagCn = U("\xF0\x9F\x87\xA8\xF0\x9F\x87\xB3");
const auto kKeycap1 = U("1\xEF\xB8\x8F\xE2\x83\xA3");
const auto kEAcute = U("e\xCC\x81");
const auto kThanksZh = U("谢谢，收到。");

// By value: LG_EQUAL keeps a reference, not to a temporary's inside.
[[nodiscard]] QString N(const QString &text) {
	return Normalize(text).value_or(U("<none>"));
}
[[nodiscard]] QString Q(const QString &text) {
	return SlashQuery(text).value_or(U("<none>"));
}

} // namespace

LUCKGRAM_TEST(QuickRepliesNormalizeTrims) {
	LG_EQUAL(N(U("  On my way!\n")), U("On my way!"));
	LG_CHECK(!Normalize(QString()));
	LG_CHECK(!Normalize(U(" \n\t ")));
	LG_EQUAL(N(U("line 1\nline 2")), U("line 1\nline 2"));
}

LUCKGRAM_TEST(QuickRepliesGraphemes) {
	LG_EQUAL(GraphemeCount(kGrin), 1);
	LG_EQUAL(GraphemeCount(kFamily), 1);
	LG_EQUAL(GraphemeCount(kThumbSkin), 1);
	LG_EQUAL(GraphemeCount(kFlagCn), 1);
	LG_EQUAL(GraphemeCount(kKeycap1), 1);
	LG_EQUAL(GraphemeCount(kEAcute), 1);
	LG_EQUAL(GraphemeCount(kFlagCn + kFlagCn), 2);
	LG_EQUAL(GraphemeCount(U("\r\n")), 1);
	LG_EQUAL(GraphemeCount(U("ab") + kGrin + U("\n") + kFamily), 5);
	LG_EQUAL(GraphemeCount(kThanksZh), 6);
	LG_EQUAL(GraphemeCount(QString()), 0);
}

LUCKGRAM_TEST(QuickRepliesLengthLimitKeepsEmoji) {
	const auto thousand = Repeat(kGrin, kMaxLength);
	LG_EQUAL(N(thousand), thousand);
	LG_EQUAL(N(Repeat(kGrin, kMaxLength + 1)), thousand);
	const auto base = Repeat(U("a"), kMaxLength - 1);
	LG_EQUAL(N(base + kFamily + kFamily), base + kFamily);
	LG_EQUAL(
		N(Repeat(U("a"), kMaxLength) + kFamily),
		Repeat(U("a"), kMaxLength));
	LG_EQUAL(N(base + kFlagCn + kFlagCn), base + kFlagCn);
	LG_EQUAL(N(base + kThumbSkin + U("b")), base + kThumbSkin);
	LG_EQUAL(N(base + kEAcute + U("x")), base + kEAcute);
}

LUCKGRAM_TEST(QuickRepliesCountLimit) {
	LG_EQUAL(kMaxCount, 100);
	LG_CHECK(CanAdd(0));
	LG_CHECK(CanAdd(99));
	LG_CHECK(!CanAdd(100)); // The 101st can't be added.
	LG_CHECK(!CanAdd(150));
}

LUCKGRAM_TEST(QuickRepliesMatching) {
	const auto items = QStringList{
		U("Thanks, got it."),
		U("thank you"),
		U("On my way!"),
		kThanksZh,
		U("在路上了！"),
	};
	LG_EQUAL(
		Matching(items, U("THANK")),
		(QStringList{ U("Thanks, got it."), U("thank you") }));
	LG_EQUAL(Matching(items, U("谢")), QStringList{ kThanksZh });
	LG_CHECK(Matching(items, U("got")).isEmpty()); // Not a prefix.
	LG_EQUAL(Matching(items, QString()), items); // Bare 「/」: all.
}

LUCKGRAM_TEST(QuickRepliesSlashQuery) {
	LG_EQUAL(Q(U("/谢")), U("谢"));
	LG_EQUAL(Q(U("/")), QString());
	LG_CHECK(!SlashQuery(U("谢")));
	LG_CHECK(!SlashQuery(U("/a\nb")));
}

LUCKGRAM_TEST(QuickRepliesMove) {
	const auto items = QStringList{ U("a"), U("b"), U("c"), U("d") };
	LG_EQUAL(Move(items, 2, 0), (QStringList{ U("c"), U("a"), U("b"), U("d") }));
	LG_EQUAL(Move(items, 0, 2), (QStringList{ U("b"), U("c"), U("a"), U("d") }));
	LG_EQUAL(Move(items, -1, 2), items);
	LG_EQUAL(Move(items, 1, 4), items);
}

LUCKGRAM_TEST(QuickRepliesSeededOnce) {
	const auto first = Seeded(Parse(QByteArray()), true);
	LG_EQUAL(first.items.size(), 4);
	LG_EQUAL(first.items[2], U("谢谢，收到。"));
	LG_CHECK(first.seeded);
	// Deleted examples don't come back.
	auto kept = first;
	kept.items = QStringList{ U("mine") };
	const auto again = Seeded(Parse(Serialize(kept)), true);
	LG_EQUAL(again.items, QStringList{ U("mine") });
	const auto english = Presets(false);
	LG_EQUAL(english[0], U("On my way!"));
}

LUCKGRAM_TEST(QuickRepliesParseDropsBad) {
	const auto data = Parse(QByteArray(
		R"({"items":["  a ","",3,"b"],"seeded":true})"));
	LG_EQUAL(data.items, (QStringList{ U("a"), U("b") }));
	LG_CHECK(data.seeded);
	LG_CHECK(!Parse(QByteArray("[1]")).seeded);
}
