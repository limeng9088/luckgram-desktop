/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QString>

// D11 (mobile 03): a video file sent as a round video message. When the
// entry is offered, the 60 s range, rotation, the circle viewfinder (crop
// square, drag, zoom) and the texts. Ported from Android RoundVideoGeometry
// / RoundVideoFeature, unit tested with the same cases.
namespace Luckgram::RoundVideo {

inline constexpr auto kFeatureKey = "round_video_from_gallery";
inline constexpr auto kMaxDurationMs = qint64(60) * 1000;
inline constexpr auto kMaxZoom = 4.;
inline constexpr auto kMinOutputSide = 240;
// Upstream round video messages (RoundVideoRecorder) are 400 x 400.
inline constexpr auto kDesktopOutputSide = 400;

[[nodiscard]] bool CanOffer(
	bool enabled,
	bool editing,
	int selectedCount,
	bool isVideo,
	bool fileExists,
	qint64 starsPrice,
	bool scheduleMode,
	bool canSendRoundVideo);

struct Size {
	int width = 0;
	int height = 0;
};
[[nodiscard]] Size DisplaySize(int width, int height, int rotation);
[[nodiscard]] int OutputSide(int configuredSize);

[[nodiscard]] double InitialRightProgress(qint64 durationMs);
[[nodiscard]] bool NeedsTimeline(qint64 durationMs);
struct Range {
	qint64 startMs = 0;
	qint64 endMs = 0;
};
[[nodiscard]] Range SelectedRange(
	qint64 durationMs,
	double leftProgress,
	double rightProgress);

[[nodiscard]] double BaseScale(
	double circleDiameter,
	int displayWidth,
	int displayHeight);
[[nodiscard]] double ClampZoom(double zoom);
struct Offset {
	double x = 0.;
	double y = 0.;
};
[[nodiscard]] Offset ClampOffset(
	int displayWidth,
	int displayHeight,
	double baseScale,
	double zoom,
	double circleDiameter,
	double tx,
	double ty);
struct Square {
	double x = 0.;
	double y = 0.;
	double side = 0.;
};
[[nodiscard]] Square CropSquare(
	int displayWidth,
	int displayHeight,
	double baseScale,
	double zoom,
	double circleDiameter,
	double tx,
	double ty);

class Texts final {
public:
	explicit Texts(bool zh) : _zh(zh) {
	}

	[[nodiscard]] QString sendAsRound() const;
	[[nodiscard]] QString title() const;
	[[nodiscard]] QString cancel() const;
	[[nodiscard]] QString send() const;
	[[nodiscard]] QString hint() const;
	[[nodiscard]] QString converting(int percent) const;
	[[nodiscard]] QString failed() const;
	[[nodiscard]] QString selected(const QString &range) const;
	[[nodiscard]] QString trimHint() const;

private:
	[[nodiscard]] QString t(const char *zh, const char *en) const;

	bool _zh = false;

};

} // namespace Luckgram::RoundVideo
