/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/media/luckgram_round_video_logic.h"

#include <cmath>

// Cases ported from mobile RoundVideoGeometryTest (spec 03). The live photo
// case doesn't exist on the desktop.

namespace {

using namespace Luckgram::RoundVideo;

[[nodiscard]] bool Near(double a, double b, double eps = 0.5) {
	return std::abs(a - b) <= eps;
}

} // namespace

LUCKGRAM_TEST(RoundVideoOfferedOnlyForSingleVideo) {
	LG_CHECK(CanOffer(true, false, 1, true, true, 0, false, true));
	LG_CHECK(!CanOffer(true, false, 2, true, true, 0, false, true));
	LG_CHECK(!CanOffer(true, false, 0, true, true, 0, false, true));
	LG_CHECK(!CanOffer(true, false, 1, false, true, 0, false, true));
	LG_CHECK(!CanOffer(true, false, 1, true, false, 0, false, true));
}

LUCKGRAM_TEST(RoundVideoHiddenWhenSwitchOffOrNotAllowed) {
	LG_CHECK(!CanOffer(false, false, 1, true, true, 0, false, true));
	LG_CHECK(!CanOffer(true, true, 1, true, true, 0, false, true));
	LG_CHECK(!CanOffer(true, false, 1, true, true, 5, false, true));
	LG_CHECK(!CanOffer(true, false, 1, true, true, 0, true, true));
	LG_CHECK(!CanOffer(true, false, 1, true, true, 0, false, false));
}

LUCKGRAM_TEST(RoundVideoShortVideoUsesWholeLength) {
	LG_CHECK(!NeedsTimeline(10'000));
	LG_CHECK(!NeedsTimeline(60'000));
	LG_CHECK(Near(InitialRightProgress(10'000), 1., 0.01));
	const auto r = SelectedRange(10'000, 0.3, 0.5);
	LG_EQUAL(r.startMs, qint64(0));
	LG_EQUAL(r.endMs, qint64(10'000));
}

LUCKGRAM_TEST(RoundVideoTwoMinutesSelectsAtMostSixtySeconds) {
	const auto d = qint64(120'000);
	LG_CHECK(NeedsTimeline(d));
	const auto right = InitialRightProgress(d);
	LG_CHECK(Near(right, 0.5, 0.01));
	auto r = SelectedRange(d, 0., right);
	LG_EQUAL(r.startMs, qint64(0));
	LG_EQUAL(r.endMs, qint64(60'000));
	r = SelectedRange(d, 0.5, 1.);
	LG_EQUAL(r.startMs, qint64(60'000));
	LG_EQUAL(r.endMs, qint64(120'000));
	r = SelectedRange(d, 0.1, 1.);
	LG_EQUAL(r.endMs - r.startMs, qint64(60'000));
	r = SelectedRange(d, 0.25, 0.5);
	LG_EQUAL(r.endMs - r.startMs, qint64(30'000));
	r = SelectedRange(d, 1.2, -1.);
	LG_CHECK(r.startMs >= 0 && r.endMs <= d && r.endMs >= r.startMs);
}

LUCKGRAM_TEST(RoundVideoRotationSwapsDisplaySize) {
	LG_EQUAL(DisplaySize(1920, 1080, 90).width, 1080);
	LG_EQUAL(DisplaySize(1920, 1080, 270).height, 1920);
	LG_EQUAL(DisplaySize(1920, 1080, 180).width, 1920);
	LG_EQUAL(DisplaySize(1920, 1080, 0).height, 1080);
	LG_EQUAL(DisplaySize(1920, 1080, -90).width, 1080);
}

LUCKGRAM_TEST(RoundVideoDefaultCropIsCentredSquare) {
	auto base = BaseScale(900, 1080, 1920);
	auto c = CropSquare(1080, 1920, base, 1., 900, 0, 0);
	LG_CHECK(Near(c.x, 0));
	LG_CHECK(Near(c.y, (1920 - 1080) / 2.));
	LG_CHECK(Near(c.side, 1080));
	base = BaseScale(900, 1920, 1080);
	c = CropSquare(1920, 1080, base, 1., 900, 0, 0);
	LG_CHECK(Near(c.x, (1920 - 1080) / 2.));
	LG_CHECK(Near(c.y, 0));
	LG_CHECK(Near(c.side, 1080));
}

LUCKGRAM_TEST(RoundVideoDragIsClampedInsideVideo) {
	const auto w = 1920;
	const auto h = 1080;
	const auto circle = 900.;
	const auto base = BaseScale(circle, w, h);
	auto o = ClampOffset(w, h, base, 1., circle, 10'000, 10'000);
	LG_CHECK(Near(o.x, (w * base - circle) / 2., 0.01));
	LG_CHECK(Near(o.y, 0., 0.01));
	auto c = CropSquare(w, h, base, 1., circle, o.x, o.y);
	LG_CHECK(Near(c.x, 0));
	o = ClampOffset(w, h, base, 1., circle, -10'000, 0);
	c = CropSquare(w, h, base, 1., circle, o.x, o.y);
	LG_CHECK(Near(c.x, w - c.side));
}

LUCKGRAM_TEST(RoundVideoZoomShrinksCropAndIsClamped) {
	LG_CHECK(Near(ClampZoom(0.2), 1., 0.01));
	LG_CHECK(Near(ClampZoom(50.), kMaxZoom, 0.01));
	const auto w = 1080;
	const auto h = 1920;
	const auto circle = 900.;
	const auto base = BaseScale(circle, w, h);
	auto c = CropSquare(w, h, base, 2., circle, 0, 0);
	LG_CHECK(Near(c.side, 540));
	LG_CHECK(Near(c.x, (w - 540) / 2.));
	LG_CHECK(Near(c.y, (h - 540) / 2.));
	const auto o = ClampOffset(w, h, base, 2., circle, -1e6, -1e6);
	c = CropSquare(w, h, base, 2., circle, o.x, o.y);
	LG_CHECK(Near(c.x, w - c.side));
	LG_CHECK(Near(c.y, h - c.side));
}

LUCKGRAM_TEST(RoundVideoOutputSideAndTexts) {
	LG_EQUAL(OutputSide(384), 384);
	LG_EQUAL(OutputSide(0), kMinOutputSide);
	LG_EQUAL(OutputSide(kDesktopOutputSide), 400);
	LG_EQUAL(Texts(true).sendAsRound(), QString::fromUtf8("作为圆形视频发送"));
	LG_EQUAL(Texts(false).converting(42), QString::fromUtf8("Converting… 42%"));
	LG_CHECK(!Texts(false).hint().contains(QStringLiteral("pinch")));
	LG_CHECK(Texts(true).hint().contains(QString::fromUtf8("滚轮")));
}
