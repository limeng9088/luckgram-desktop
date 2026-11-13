/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <cstdint>
#include <memory>

// D11 (mobile 04 CallRecOpusEncoder): mono 48 kHz PCM16 to Ogg/Opus at
// 32 kbps, in memory only (FFmpeg "opus" muxer, the same setup upstream
// uses for voice messages). Nothing is written to disk.
namespace Luckgram::CallRecording {

class OggOpusEncoder final {
public:
	// Tags read back as the audio title / performer when sent.
	explicit OggOpusEncoder(
		const QString &title = QString(),
		const QString &artist = QString());
	~OggOpusEncoder();

	[[nodiscard]] bool valid() const;
	// Mono, kRate. False once encoding failed (then stays invalid).
	bool push(const int16_t *samples, int count);
	// The finished file, or empty if anything failed.
	[[nodiscard]] QByteArray finish();

	[[nodiscard]] qint64 bytes() const;
	[[nodiscard]] qint64 durationMs() const;

private:
	struct Private;
	const std::unique_ptr<Private> _private;

};

} // namespace Luckgram::CallRecording
