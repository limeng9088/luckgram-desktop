/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/qr/luckgram_qr_decode.h"

#include "luckgram/qr/quirc/quirc.h"

#include <QtGui/QImage>
#include <QtGui/QPainter>

#include <algorithm>
#include <cstring>
#include <memory>

namespace Luckgram::Qr {
namespace {

constexpr auto kRetrySide = 1000;

struct QuircDeleter {
	void operator()(quirc *q) const {
		quirc_destroy(q);
	}
};

[[nodiscard]] std::optional<QString> DecodeGray(const QImage &gray) {
	const auto width = gray.width();
	const auto height = gray.height();
	if (width <= 0 || height <= 0) {
		return std::nullopt;
	}
	const auto q = std::unique_ptr<quirc, QuircDeleter>(quirc_new());
	if (!q || quirc_resize(q.get(), width, height) < 0) {
		return std::nullopt;
	}
	auto w = 0;
	auto h = 0;
	const auto buffer = quirc_begin(q.get(), &w, &h);
	if (!buffer || w != width || h != height) {
		quirc_end(q.get());
		return std::nullopt;
	}
	for (auto y = 0; y != height; ++y) {
		std::memcpy(buffer + y * width, gray.constScanLine(y), width);
	}
	quirc_end(q.get());

	const auto count = quirc_count(q.get());
	for (auto i = 0; i != count; ++i) {
		auto code = quirc_code();
		auto data = quirc_data();
		quirc_extract(q.get(), i, &code);
		auto error = quirc_decode(&code, &data);
		if (error == QUIRC_ERROR_DATA_ECC) {
			// A mirrored code (e.g. a selfie camera frame).
			quirc_flip(&code);
			error = quirc_decode(&code, &data);
		}
		if (error == QUIRC_SUCCESS && data.payload_len > 0) {
			return QString::fromUtf8(
				reinterpret_cast<const char*>(data.payload),
				data.payload_len);
		}
	}
	return std::nullopt;
}

} // namespace

std::optional<QString> DecodeQr(const QImage &image) {
	if (image.isNull()) {
		return std::nullopt;
	}
	// Transparent pixels would turn black: put the picture on white first.
	auto opaque = image;
	if (image.hasAlphaChannel()) {
		opaque = QImage(image.size(), QImage::Format_RGB32);
		opaque.fill(Qt::white);
		auto painter = QPainter(&opaque);
		painter.drawImage(0, 0, image);
	}
	const auto gray = opaque.convertToFormat(QImage::Format_Grayscale8);
	if (auto result = DecodeGray(gray)) {
		return result;
	}
	const auto side = std::max(gray.width(), gray.height());
	if (side <= kRetrySide) {
		return std::nullopt;
	}
	return DecodeGray(gray.scaled(
		kRetrySide,
		kRetrySide,
		Qt::KeepAspectRatio,
		Qt::SmoothTransformation));
}

} // namespace Luckgram::Qr
