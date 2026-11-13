/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/calls/luckgram_call_recording_logic.h"

#include <limits>

// Cases ported from mobile CallRecMixerTest / CallRecTextTest (spec 04).

namespace {

using namespace Luckgram::CallRecording;

[[nodiscard]] std::vector<int16_t> Constant(int count, int16_t value) {
	return std::vector<int16_t>(count, value);
}

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

} // namespace

LUCKGRAM_TEST(CallRecMixesBothSidesFrameByFrame) {
	auto m = Mixer();
	const auto near = Constant(kFrame, 100);
	m.pushNear(near.data(), kFrame, kRate, 1, false);
	auto out = std::vector<int16_t>();
	m.drain(out);
	LG_CHECK(out.empty()); // Waits for the far end.
	const auto far = Constant(kFrame, 23);
	m.pushFar(far.data(), kFrame, kRate, 1);
	m.drain(out);
	LG_EQUAL(int(out.size()), kFrame);
	LG_EQUAL(int(out.front()), 123);
	LG_EQUAL(int(out.back()), 123);
}

LUCKGRAM_TEST(CallRecClampsInsteadOfWrapping) {
	auto m = Mixer();
	auto out = std::vector<int16_t>();
	const auto high = Constant(kFrame, 30000);
	m.pushNear(high.data(), kFrame, kRate, 1, false);
	m.pushFar(high.data(), kFrame, kRate, 1);
	m.drain(out);
	LG_EQUAL(int(out.front()), int(std::numeric_limits<int16_t>::max()));
	out.clear();
	const auto low = Constant(kFrame, -30000);
	m.pushNear(low.data(), kFrame, kRate, 1, false);
	m.pushFar(low.data(), kFrame, kRate, 1);
	m.drain(out);
	LG_EQUAL(int(out.front()), int(std::numeric_limits<int16_t>::min()));
}

LUCKGRAM_TEST(CallRecMutedMicrophoneIsSilence) {
	auto m = Mixer();
	const auto loud = Constant(kFrame, 5000);
	const auto far = Constant(kFrame, 7);
	m.pushNear(loud.data(), kFrame, kRate, 1, true);
	m.pushFar(far.data(), kFrame, kRate, 1);
	auto out = std::vector<int16_t>();
	m.drain(out);
	LG_EQUAL(int(out.front()), 7);
}

LUCKGRAM_TEST(CallRecSilentSideDoesNotHoldBackTheOther) {
	auto m = Mixer();
	auto out = std::vector<int16_t>();
	const auto first = Constant(kMaxLagSamples - kFrame, 1);
	m.pushNear(first.data(), int(first.size()), kRate, 1, false);
	m.drain(out);
	LG_CHECK(out.empty());
	const auto frame = Constant(kFrame, 1);
	m.pushNear(frame.data(), kFrame, kRate, 1, false);
	m.drain(out);
	LG_EQUAL(int(out.size()), kFrame);
	LG_EQUAL(int(out.front()), 1);
	out.clear();
	m.pushNear(frame.data(), kFrame, kRate, 1, false);
	m.drain(out);
	LG_EQUAL(int(out.size()), kFrame);
}

LUCKGRAM_TEST(CallRecFlushKeepsEverythingRecorded) {
	auto m = Mixer();
	const auto total = kFrame * 2 + 100;
	const auto all = Constant(total, 2);
	m.pushNear(all.data(), total, kRate, 1, false);
	auto out = std::vector<int16_t>();
	m.flush(out);
	LG_EQUAL(int(out.size()), total);
	LG_EQUAL(int(out.back()), 2);
	out.clear();
	m.flush(out);
	LG_CHECK(out.empty());
}

LUCKGRAM_TEST(CallRecDownmixesStereo) {
	auto m = Mixer();
	auto stereo = std::vector<int16_t>(kFrame * 2);
	for (auto i = 0; i != kFrame; ++i) {
		stereo[i * 2] = 100;
		stereo[i * 2 + 1] = 300;
	}
	m.pushFar(stereo.data(), kFrame, kRate, 2);
	const auto silence = Constant(kFrame, 0);
	m.pushNear(silence.data(), kFrame, kRate, 1, false);
	auto out = std::vector<int16_t>();
	m.drain(out);
	LG_EQUAL(int(out.front()), 200);
}

LUCKGRAM_TEST(CallRecResamplesToFortyEightKilohertz) {
	const auto in = Constant(160, 42); // 10 ms at 16 kHz.
	const auto out = Resample(in, 16000);
	LG_EQUAL(int(out.size()), 480);
	LG_EQUAL(int(out.front()), 42);
	LG_EQUAL(int(out.back()), 42);
	auto m = Mixer();
	for (auto i = 0; i != 2; ++i) {
		m.pushNear(in.data(), 160, 16000, 1, false);
		m.pushFar(in.data(), 160, 16000, 1);
	}
	auto mixed = std::vector<int16_t>();
	m.drain(mixed);
	LG_EQUAL(int(mixed.size()), kFrame);
	LG_EQUAL(int(mixed[500]), 84);
}

LUCKGRAM_TEST(CallRecShortTextIsOneMessage) {
	const auto parts = Split(U("  要点总结\n- a\n\n文字稿\nhello  "), 4000, U("续"));
	LG_EQUAL(parts.size(), 1);
	LG_EQUAL(parts[0], U("要点总结\n- a\n\n文字稿\nhello"));
}

LUCKGRAM_TEST(CallRecLongTextSplitsAtLineEnds) {
	auto text = QString();
	for (auto i = 0; i != 300; ++i) {
		text += QStringLiteral("line %1 some spoken words here\n").arg(i);
	}
	const auto prefix = U("文字稿（续）");
	const auto parts = Split(text, 4000, prefix);
	LG_CHECK(parts.size() >= 2);
	auto joined = QString();
	for (auto i = 0; i != parts.size(); ++i) {
		auto p = parts[i];
		LG_CHECK(p.size() <= 4096);
		if (i > 0) {
			LG_CHECK(p.startsWith(prefix + QChar('\n')));
			p = p.mid(prefix.size() + 1);
			LG_CHECK(p.startsWith(U("line ")));
			joined += QChar('\n');
		}
		joined += p;
	}
	LG_EQUAL(joined, text.trimmed());
}

LUCKGRAM_TEST(CallRecTextWithoutBreaksIsCutHard) {
	const auto text = QString(9000, QChar(0x5B57));
	const auto parts = Split(text, 4000, U("c"));
	LG_EQUAL(parts.size(), 3);
	LG_EQUAL(parts[0].size(), 4000);
	LG_EQUAL(parts[0].size() + parts[1].size() - 2 + parts[2].size() - 2, 9000);
}

LUCKGRAM_TEST(CallRecLimit) {
	LG_EQUAL(Limit(U("abc"), 10), U("abc"));
	LG_EQUAL(Limit(U("abcdef"), 0), U("abcdef"));
	auto text = QString();
	for (auto i = 0; i != 100; ++i) {
		text += QStringLiteral("sentence number %1\n").arg(i);
	}
	const auto cut = Limit(text, 500);
	LG_CHECK(cut.size() <= 500);
	LG_CHECK(cut.size() >= 400);
	LG_CHECK(text.startsWith(cut + QChar('\n')));
	LG_EQUAL(Limit(QString(1000, QChar('x')), 300).size(), 300);
}

LUCKGRAM_TEST(CallRecNamesAndTexts) {
	LG_EQUAL(Duration(754), U("12:34"));
	LG_EQUAL(Duration(3723), U("1:02:03"));
	LG_EQUAL(FileDateTime(QDateTime(QDate(2026, 10, 26), QTime(15, 4))), U("2026-10-26 15.04"));
	LG_EQUAL(Texts(true).fileTitle(U("张三"), U("2026-10-26 15.04")), U("通话 张三 2026-10-26 15.04"));
	LG_EQUAL(Texts(false).caption(U("Ann"), U("1:02")), U("Call with Ann · 1:02"));
	LG_EQUAL(Texts(true).recording(U("0:21")), U("录音 0:21"));
	LG_CHECK(Texts(true).legalText().contains(U("取得同意")));
	LG_CHECK(!Texts(false).autoRecordInfo().contains(U("Tap")));
}
