/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/media/luckgram_round_video_logic.h"

#include <algorithm>

namespace Luckgram::RoundVideo {
namespace {

[[nodiscard]] double Clamp01(double value) {
	return std::max(0., std::min(1., value));
}

} // namespace

bool CanOffer(
		bool enabled,
		bool editing,
		int selectedCount,
		bool isVideo,
		bool fileExists,
		qint64 starsPrice,
		bool scheduleMode,
		bool canSendRoundVideo) {
	return enabled
		&& !editing
		&& (selectedCount == 1)
		&& isVideo
		&& fileExists
		&& (starsPrice <= 0)
		&& !scheduleMode
		&& canSendRoundVideo;
}

Size DisplaySize(int width, int height, int rotation) {
	const auto r = ((rotation % 360) + 360) % 360;
	return (r == 90 || r == 270)
		? Size{ height, width }
		: Size{ width, height };
}

int OutputSide(int configuredSize) {
	return std::max(kMinOutputSide, configuredSize);
}

double InitialRightProgress(qint64 durationMs) {
	return (durationMs > kMaxDurationMs)
		? (double(kMaxDurationMs) / durationMs)
		: 1.;
}

bool NeedsTimeline(qint64 durationMs) {
	return durationMs > kMaxDurationMs;
}

Range SelectedRange(qint64 durationMs, double leftProgress, double rightProgress) {
	if (!NeedsTimeline(durationMs)) {
		return { 0, std::max(qint64(0), durationMs) };
	}
	const auto l = Clamp01(leftProgress);
	const auto r = std::max(l, Clamp01(rightProgress));
	const auto start = qint64(l * durationMs);
	auto end = std::min(qint64(r * durationMs), start + kMaxDurationMs);
	end = std::min(end, durationMs);
	return { start, end };
}

double BaseScale(double circleDiameter, int displayWidth, int displayHeight) {
	return circleDiameter / std::min(displayWidth, displayHeight);
}

double ClampZoom(double zoom) {
	return std::max(1., std::min(kMaxZoom, zoom));
}

Offset ClampOffset(
		int displayWidth,
		int displayHeight,
		double baseScale,
		double zoom,
		double circleDiameter,
		double tx,
		double ty) {
	const auto w = displayWidth * baseScale * zoom;
	const auto h = displayHeight * baseScale * zoom;
	const auto maxX = std::max(0., (w - circleDiameter) / 2.);
	const auto maxY = std::max(0., (h - circleDiameter) / 2.);
	return {
		std::max(-maxX, std::min(maxX, tx)),
		std::max(-maxY, std::min(maxY, ty)),
	};
}

Square CropSquare(
		int displayWidth,
		int displayHeight,
		double baseScale,
		double zoom,
		double circleDiameter,
		double tx,
		double ty) {
	const auto s = baseScale * zoom;
	const auto side = std::min(
		double(std::min(displayWidth, displayHeight)),
		circleDiameter / s);
	auto x = (displayWidth * s / 2. - tx - circleDiameter / 2.) / s;
	auto y = (displayHeight * s / 2. - ty - circleDiameter / 2.) / s;
	x = std::max(0., std::min(displayWidth - side, x));
	y = std::max(0., std::min(displayHeight - side, y));
	return { x, y, side };
}

QString Texts::t(const char *zh, const char *en) const {
	return QString::fromUtf8(_zh ? zh : en);
}

QString Texts::sendAsRound() const { return t("作为圆形视频发送", "Send as Round Video"); }
QString Texts::title() const { return t("圆形视频", "Round Video"); }
QString Texts::cancel() const { return t("取消", "Cancel"); }
QString Texts::send() const { return t("发送", "Send"); }
QString Texts::hint() const { return t("拖动调整位置，滚轮缩放", "Drag to move, scroll to zoom"); }

QString Texts::converting(int percent) const {
	return t("正在转换… %1%", "Converting… %1%").arg(percent);
}

QString Texts::failed() const { return t("无法转换这个视频。", "Couldn't convert this video."); }

QString Texts::selected(const QString &range) const {
	return t("已选 %1", "Selected %1").arg(range);
}

QString Texts::trimHint() const { return t("视频超过 60 秒，请选取不超过 60 秒的一段", "Videos longer than 60 seconds: choose up to 60 seconds"); }

} // namespace Luckgram::RoundVideo
