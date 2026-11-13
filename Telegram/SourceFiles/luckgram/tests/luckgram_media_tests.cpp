/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/media/luckgram_image_prepare.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtGui/QImage>

// D10: photos for Square uploads (mobile 26 需求 3).

LUCKGRAM_TEST(MediaPrepareImageScalesFlattensAndDropsMetadata) {
	auto dir = QTemporaryDir();
	LG_CHECK(dir.isValid());
	auto source = QImage(4096, 3072, QImage::Format_ARGB32);
	source.fill(Qt::transparent);
	source.setText(QStringLiteral("Comment"), QStringLiteral("GPS 31.23 121.47"));
	const auto path = dir.filePath(QStringLiteral("big.png"));
	LG_CHECK(source.save(path, "PNG"));

	const auto prepared = Luckgram::Media::PrepareImage(path);
	LG_CHECK(!prepared.jpeg.isEmpty());
	LG_EQUAL(prepared.width, 2048);
	LG_EQUAL(prepared.height, 1536);
	LG_CHECK(prepared.jpeg.startsWith("\xFF\xD8"));
	LG_CHECK(!prepared.jpeg.contains("Exif"));
	LG_CHECK(!prepared.jpeg.contains("GPS"));

	const auto decoded = QImage::fromData(prepared.jpeg, "JPEG");
	LG_EQUAL(decoded.width(), 2048);
	LG_CHECK(decoded.pixelColor(10, 10).red() > 240); // Transparent -> white.
}

LUCKGRAM_TEST(MediaPrepareImageKeepsSmallImagesAndFailsOnGarbage) {
	auto dir = QTemporaryDir();
	auto small = QImage(800, 600, QImage::Format_RGB32);
	small.fill(Qt::red);
	const auto path = dir.filePath(QStringLiteral("small.jpg"));
	LG_CHECK(small.save(path, "JPEG"));
	const auto prepared = Luckgram::Media::PrepareImage(path);
	LG_EQUAL(prepared.width, 800);
	LG_EQUAL(prepared.height, 600);

	const auto bad = dir.filePath(QStringLiteral("bad.jpg"));
	auto file = QFile(bad);
	LG_CHECK(file.open(QIODevice::WriteOnly));
	file.write("not an image");
	file.close();
	LG_CHECK(Luckgram::Media::PrepareImage(bad).jpeg.isEmpty());
}
