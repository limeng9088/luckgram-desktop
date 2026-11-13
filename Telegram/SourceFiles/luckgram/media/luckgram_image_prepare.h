/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

// D10 (mobile 26 需求 3): a photo for upload. EXIF orientation applied,
// long edge <= maxEdge, transparency on white, re-encoded as a fresh JPEG so
// nothing of the source file (EXIF, GPS, camera) is kept. QtGui only, unit
// tested in luckgram_tests. Blocking: call it on a worker thread.
namespace Luckgram::Media {

struct PreparedImage {
	QByteArray jpeg; // Empty when the file can't be read.
	int width = 0;
	int height = 0;
};

[[nodiscard]] PreparedImage PrepareImage(
	const QString &path,
	int maxEdge = 2048,
	int quality = 87);

} // namespace Luckgram::Media
