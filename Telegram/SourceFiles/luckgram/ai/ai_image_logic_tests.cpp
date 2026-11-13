/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"
#include "luckgram/ai/ai_image_logic.h"

// D7 / mobile 23: the prefixes behave like Android LuckgramAiRegistry
// (case-insensitive, the rest trimmed, an empty rest is not taken).
using namespace Luckgram::AI::Draw;

namespace {

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

} // namespace

LUCKGRAM_TEST(AiImageInterceptsPrefixes) {
	const auto zh = Intercept(U("画 夜晚的霓虹城市上空，一架纸飞机在飞"));
	LG_CHECK(zh.has_value());
	LG_EQUAL(zh->prefix, U("画"));
	LG_EQUAL(zh->prompt, U("夜晚的霓虹城市上空，一架纸飞机在飞"));

	const auto en = Intercept(QStringLiteral("  /IMAGE   a paper plane  "));
	LG_CHECK(en.has_value());
	LG_EQUAL(en->prefix, QStringLiteral("/image"));
	LG_EQUAL(en->prompt, QStringLiteral("a paper plane"));

	// No space needed after 画, as on Android.
	const auto glued = Intercept(U("画一只猫"));
	LG_CHECK(glued.has_value());
	LG_EQUAL(glued->prompt, U("一只猫"));
}

LUCKGRAM_TEST(AiImageLeavesOtherTextAlone) {
	LG_CHECK(!Intercept(U("画")).has_value());
	LG_CHECK(!Intercept(QStringLiteral("/image   ")).has_value());
	LG_CHECK(!Intercept(QString()).has_value());
	LG_CHECK(!Intercept(QStringLiteral("draw a cat")).has_value());
	LG_CHECK(!Intercept(U("帮我画一只猫")).has_value());
	LG_CHECK(!Intercept(QStringLiteral("image of a cat")).has_value());
}

LUCKGRAM_TEST(AiImageQuickPrefix) {
	LG_EQUAL(QuickPrefix(true), U("画 "));
	LG_EQUAL(QuickPrefix(false), QStringLiteral("/image "));
	// What the button puts in is taken once a description follows.
	LG_CHECK(Intercept(QuickPrefix(true) + U("海边日落")).has_value());
	LG_CHECK(Intercept(QuickPrefix(false) + QStringLiteral("sunset")).has_value());
}

LUCKGRAM_TEST(AiImageDataRoundTrip) {
	const auto done = Data{
		.state = State::Done,
		.file = QStringLiteral("ai_image_1"),
		.mime = QStringLiteral("image/jpeg"),
	};
	LG_CHECK(Data::FromJson(done.toJson()) == done);
	const auto error = Data{
		.state = State::Error,
		.error = QStringLiteral("CONTENT_REJECTED"),
	};
	LG_CHECK(Data::FromJson(error.toJson()) == error);
	LG_CHECK(Data::FromJson(QJsonObject()) == Data());
	// Done without a picture can only offer 重画.
	auto broken = QJsonObject();
	broken.insert(QStringLiteral("state"), QStringLiteral("done"));
	LG_EQUAL(Data::FromJson(broken).state, State::Error);
}

LUCKGRAM_TEST(AiImageStorageNames) {
	LG_EQUAL(
		StorageName(QStringLiteral("550E8400-e29b-41d4-a716-446655440000")),
		QStringLiteral("ai_image_550e8400-e29b-41d4-a716-446655440000"));
	LG_EQUAL(StorageName(QString()), QString());
	LG_EQUAL(StorageName(QStringLiteral("{braces}")), QString());
	LG_EQUAL(StorageName(QStringLiteral("a/b")), QString());
	LG_EQUAL(StorageName(QString(60, QChar('a'))), QString()); // > 64.
}

LUCKGRAM_TEST(AiImageExtensions) {
	LG_EQUAL(Extension(QStringLiteral("image/jpeg")), QStringLiteral("jpg"));
	LG_EQUAL(Extension(QStringLiteral("image/webp")), QStringLiteral("webp"));
	LG_EQUAL(Extension(QStringLiteral("image/png")), QStringLiteral("png"));
	LG_EQUAL(Extension(QString()), QStringLiteral("png"));
}

LUCKGRAM_TEST(AiImageIndexAndOrphans) {
	const auto a = Written{ QStringLiteral("c1"), QStringLiteral("ai_image_a") };
	const auto b = Written{ QStringLiteral("c1"), QStringLiteral("ai_image_b") };
	const auto c = Written{ QStringLiteral("c2"), QStringLiteral("ai_image_c") };
	const auto bad = Written{ QStringLiteral("c2"), QStringLiteral("notes") };
	const auto list = std::vector<Written>{ a, b, c, a, bad };
	const auto back = DeserializeIndex(SerializeIndex(list));
	LG_CHECK(back == (std::vector<Written>{ a, b, c }));
	LG_CHECK(DeserializeIndex(QByteArray("not json")).empty());

	const auto written = std::vector<Written>{ a, b, c };
	// Conversation gone: all its pictures, none of the others.
	LG_EQUAL(
		Orphans(written, QStringLiteral("c1"), std::nullopt),
		(QStringList{ QStringLiteral("ai_image_a"), QStringLiteral("ai_image_b") }));
	// Still there: only pictures no message refers to.
	LG_EQUAL(
		Orphans(
			written,
			QStringLiteral("c1"),
			QStringList{ QStringLiteral("ai_image_b") }),
		QStringList{ QStringLiteral("ai_image_a") });
	LG_EQUAL(
		Orphans(written, QStringLiteral("c3"), std::nullopt),
		QStringList());
}
