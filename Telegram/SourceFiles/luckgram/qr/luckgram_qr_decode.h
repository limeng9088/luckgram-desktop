/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QString>

#include <optional>

class QImage;

// D3 / D10: reads a QR code from an image (a picture file, a screenshot in
// the clipboard, a camera frame). Vendored quirc 1.2 (luckgram/qr/quirc,
// ISC), shared by the proxy import (D3) and 扫一扫 (D10, w-1147).
namespace Luckgram::Qr {

// The text of the first readable code, or nullopt. Works on 8-bit
// grayscale; a big picture that gives nothing is tried again with its long
// side scaled to about 1000 px.
[[nodiscard]] std::optional<QString> DecodeQr(const QImage &image);

} // namespace Luckgram::Qr
