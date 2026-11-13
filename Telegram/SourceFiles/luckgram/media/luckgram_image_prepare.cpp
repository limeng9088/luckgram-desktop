/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/media/luckgram_image_prepare.h"

#include <QtCore/QBuffer>
#include <QtGui/QImage>
#include <QtGui/QImageReader>
#include <QtGui/QPainter>

#include <algorithm>
#include <cmath>

namespace Luckgram::Media {

PreparedImage PrepareImage(const QString &path, int maxEdge, int quality) {
	auto reader = QImageReader(path);
	reader.setAutoTransform(true); // EXIF orientation.
	auto image = reader.read();
	if (image.isNull()) {
		return {};
	}
	const auto edge = std::max(image.width(), image.height());
	if (edge > maxEdge) {
		const auto k = double(maxEdge) / edge;
		image = image.scaled(
			std::max(1, int(std::lround(image.width() * k))),
			std::max(1, int(std::lround(image.height() * k))),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
	}
	auto flat = QImage(image.size(), QImage::Format_RGB32);
	flat.fill(Qt::white);
	{
		auto p = QPainter(&flat);
		p.drawImage(0, 0, image);
	}
	auto result = PreparedImage{
		.width = flat.width(),
		.height = flat.height(),
	};
	auto buffer = QBuffer(&result.jpeg);
	buffer.open(QIODevice::WriteOnly);
	if (!flat.save(&buffer, "JPEG", quality)) {
		return {};
	}
	return result;
}

} // namespace Luckgram::Media
