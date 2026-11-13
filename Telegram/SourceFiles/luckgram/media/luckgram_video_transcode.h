/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <atomic>
#include <memory>

// D10 / D11: re-encodes a local video to H.264 / AAC MP4 with the bundled
// FFmpeg (libopenh264 + aac, the encoders round video messages use). The
// long edge is scaled down to `maxEdge` (even sides, rotation applied), the
// video is cut at `maxDurationMs`, and nothing of the source container is
// copied (no location or other metadata). Blocking: call it on a worker
// thread (crl::async) and hand the result back to the main thread.
namespace Luckgram::Media {

struct VideoInfo {
	int width = 0; // After rotation.
	int height = 0;
	qint64 durationMs = 0;
	bool ok = false;
};

struct TranscodeArgs {
	QString path;
	int maxEdge = 1280;
	qint64 maxDurationMs = 60 * 1000;
	// 0 = keep the source bitrate rules (Square::VideoBitrate).
	int videoBitrate = 0;
	bool withAudio = true;
	// Set from the main thread to stop early (the box was closed).
	std::shared_ptr<std::atomic<bool>> cancelled;
};

struct TranscodeResult {
	QByteArray mp4;
	QByteArray coverJpeg; // First frame, the same size as the video.
	int width = 0;
	int height = 0;
	qint64 durationMs = 0;
	QString error; // Empty on success.
};

[[nodiscard]] VideoInfo Probe(const QString &path);

// `progress` gets 0..100, called on the worker thread.
[[nodiscard]] TranscodeResult Transcode(
	const TranscodeArgs &args,
	Fn<void(int)> progress = nullptr);

} // namespace Luckgram::Media
