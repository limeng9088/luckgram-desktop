/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/transcribe/luckgram_transcribe_logic.h"

// D6: free transcription (mobile 17) and its links.

namespace {

using namespace Luckgram::Transcribe;

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

} // namespace

LUCKGRAM_TEST(TranscribePremiumKeepsTelegram) {
	LG_CHECK(UseLuckgram(true, false));
	LG_CHECK(!UseLuckgram(true, true));
	LG_CHECK(!UseLuckgram(false, false));
}

LUCKGRAM_TEST(TranscribeSizeLimit) {
	const auto mb = int64_t(1024) * 1024;
	LG_CHECK(!TooLarge(25 * mb, 25));
	LG_CHECK(TooLarge(25 * mb + 1, 25));
	LG_CHECK(!TooLarge(25 * mb, 0)); // No remote value: 25 MB.
	LG_CHECK(TooLarge(11 * mb, 10));
}

LUCKGRAM_TEST(TranscribeUploadMime) {
	LG_EQUAL(UploadMime(true, U("video/mp4")), U("audio/mp4"));
	LG_EQUAL(UploadMime(false, U("audio/ogg")), U("audio/ogg"));
	LG_EQUAL(UploadMime(false, U(" Audio/MPEG ")), U("audio/mpeg"));
	LG_EQUAL(UploadMime(false, QString()), U("audio/ogg"));
}

LUCKGRAM_TEST(TranscribeLinksRoundTrip) {
	const auto retry = Link(LinkAction::Retry, 777000, 42);
	LG_EQUAL(retry, U("internal:luckgram_transcribe:retry:777000:42"));
	const auto parsed = ParseLink(retry.mid(9));
	LG_CHECK(parsed.has_value());
	LG_CHECK(parsed->action == LinkAction::Retry);
	LG_EQUAL(parsed->peerId, int64_t(777000));
	LG_EQUAL(parsed->msgId, int64_t(42));
	const auto translate = ParseLink(
		Link(LinkAction::Translate, 5, 6).mid(9));
	LG_CHECK(translate && translate->action == LinkAction::Translate);
}

LUCKGRAM_TEST(TranscribeLinksRejectGarbage) {
	LG_CHECK(!ParseLink(U("luckgram_transcribe:open:1:2")));
	LG_CHECK(!ParseLink(U("luckgram_transcribe:retry:1")));
	LG_CHECK(!ParseLink(U("luckgram_transcribe:retry:0:2")));
	LG_CHECK(!ParseLink(U("luckgram_transcribe:retry:1:0")));
	LG_CHECK(!ParseLink(U("luckgram_transcribe:retry:x:2")));
	LG_CHECK(!ParseLink(U("other:retry:1:2")));
}

LUCKGRAM_TEST(TranscribeTexts) {
	LG_EQUAL(FailedText(true) + RetryLabel(true), U("转写失败 · 重试"));
	LG_EQUAL(
		FailedText(false) + RetryLabel(false),
		U("Couldn't transcribe · Retry"));
	LG_EQUAL(CacheKey(1, 2), U("1:2"));
}

// 17.5-17.6 dictation.

LUCKGRAM_TEST(DictationShortRecordingDropped) {
	LG_CHECK(TooShort(0));
	LG_CHECK(TooShort(99));
	LG_CHECK(!TooShort(100));
}

LUCKGRAM_TEST(DictationLevel) {
	LG_EQUAL(LevelRatio(0), 0.);
	LG_EQUAL(LevelRatio(-5), 0.);
	LG_EQUAL(LevelRatio(900), 0.5);
	LG_EQUAL(LevelRatio(5000), 1.);
}

LUCKGRAM_TEST(DictationTidyKeepsRawWhenEmpty) {
	LG_EQUAL(TidyResult(U("嗯 那个 明天见"), U("明天见。")), U("明天见。"));
	LG_EQUAL(TidyResult(U("明天见"), U("  ")), U("明天见"));
}
