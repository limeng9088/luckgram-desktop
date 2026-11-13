/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <cstdint>
#include <deque>
#include <vector>

// D11 (mobile 04): pure parts of call recording, ported from Android
// CallRecMixer / CallRecText / CallRecStrings and unit tested with the same
// rules: the two sides mixed into one mono 48 kHz stream, the transcript
// reply split into message-sized parts, file name, caption and texts.
namespace Luckgram::CallRecording {

inline constexpr auto kFeatureKey = "call_recording";
inline constexpr auto kAutoKey = "call_recording_auto";
inline constexpr auto kAiTranscriptKey = "call_recording_ai_transcript";
inline constexpr auto kTargetKey = "luckgram_call_recording_target";

inline constexpr auto kRate = 48'000;
inline constexpr auto kFrame = 960; // 20 ms at 48 kHz (an Opus frame).
// A side that stopped delivering doesn't hold the other back longer.
inline constexpr auto kMaxLagSamples = kRate * 3 / 10;
inline constexpr auto kBitrate = 32'000;
// Shorter recordings are dropped (mobile MIN_DURATION_MS).
inline constexpr auto kMinDurationMs = 1'000;
// Telegram's text limit is 4096; room for the "continued" header.
inline constexpr auto kMaxTextChunk = 4'000;
inline constexpr auto kDefaultTranscribeMaxMb = 4; // ai.transcribe_max_mb
inline constexpr auto kDefaultMaxInputChars = 60'000; // ai.max_input_chars

// Mono, 48 kHz, 16 bit; both sides pushed from their audio threads (the
// caller locks), frames taken out by Drain() / Flush().
class Mixer final {
public:
	// PCM16 interleaved, any rate / channel count. Muted near end = silence.
	void pushNear(
		const int16_t *pcm,
		int frames,
		int sampleRate,
		int channels,
		bool muted);
	void pushFar(const int16_t *pcm, int frames, int sampleRate, int channels);

	// Every complete frame that can be mixed now, appended to `out`.
	void drain(std::vector<int16_t> &out);
	// Everything left (the last frame may be partial).
	void flush(std::vector<int16_t> &out);

	[[nodiscard]] int nearSize() const;
	[[nodiscard]] int farSize() const;

private:
	void mix(bool useNear, bool useFar, int count, std::vector<int16_t> &out);

	std::deque<int16_t> _near;
	std::deque<int16_t> _far;

};

[[nodiscard]] std::vector<int16_t> ToMono(
	const int16_t *pcm,
	int frames,
	int channels);
// Linear, one block to kRate.
[[nodiscard]] std::vector<int16_t> Resample(
	const std::vector<int16_t> &in,
	int fromRate);

// At most maxChars, cut at a line end or space in the last fifth.
[[nodiscard]] QString Limit(const QString &text, int maxChars);
// Parts of at most maxChunk characters (line end, then space in the second
// half); later parts start with `continuedPrefix` and a line break.
[[nodiscard]] QStringList Split(
	const QString &text,
	int maxChunk,
	const QString &continuedPrefix);

// "12:34" / "1:02:03".
[[nodiscard]] QString Duration(qint64 seconds);
// "2026-10-26 15.04.09" (no ':' in file names).
[[nodiscard]] QString FileDateTime(const QDateTime &when);
// \ / : * ? " < > | and line breaks / tabs become '_', then trimmed.
[[nodiscard]] QString SanitizeFileName(const QString &name);
// Size of one Ogg segment sent to /v1/ai/transcribe: a tenth below
// ai.transcribe_max_mb, so the page that crosses it still fits.
[[nodiscard]] qint64 SegmentLimitBytes(qint64 transcribeMaxMb);
// Seconds shown in the caption, at least 1 (rounded).
[[nodiscard]] qint64 DurationSeconds(qint64 durationMs);

class Texts final {
public:
	explicit Texts(bool zh) : _zh(zh) {
	}

	[[nodiscard]] QString featureName() const;
	[[nodiscard]] QString featureDescription() const;
	[[nodiscard]] QString enable() const;
	[[nodiscard]] QString enableInfo() const;
	[[nodiscard]] QString legalTitle() const;
	[[nodiscard]] QString legalText() const;
	[[nodiscard]] QString gotIt() const;
	[[nodiscard]] QString cancel() const;
	[[nodiscard]] QString autoRecord() const;
	[[nodiscard]] QString autoRecordInfo() const;
	[[nodiscard]] QString aiTranscript() const;
	[[nodiscard]] QString aiTranscriptInfo() const;
	[[nodiscard]] QString saveTo() const;
	[[nodiscard]] QString savedMessages() const;
	[[nodiscard]] QString chooseChat() const;
	[[nodiscard]] QString resetToSaved() const;
	[[nodiscard]] QString saveToInfo() const;
	[[nodiscard]] QString record() const;
	[[nodiscard]] QString recording(const QString &elapsed) const;
	[[nodiscard]] QString stopRecordingDescription() const;
	[[nodiscard]] QString startFailed() const;
	[[nodiscard]] QString fileTitle(
		const QString &peerName,
		const QString &dateTime) const;
	[[nodiscard]] QString caption(
		const QString &peerName,
		const QString &duration) const;
	[[nodiscard]] QString summaryHeader() const;
	[[nodiscard]] QString transcriptHeader() const;
	[[nodiscard]] QString transcriptContinued() const;
	[[nodiscard]] QString transcriptEmpty() const;
	[[nodiscard]] QString transcriptPartial(const QString &reason) const;
	[[nodiscard]] QString transcriptFailed(const QString &reason) const;
	[[nodiscard]] QString savedToast(const QString &target) const;

	// Reply under the audio: summary (if any), transcript, partial note.
	[[nodiscard]] QString reply(
		const QString &summary,
		const QString &transcript,
		const QString &partialReason) const;

private:
	[[nodiscard]] QString t(const char *zh, const char *en) const;

	bool _zh = false;

};

} // namespace Luckgram::CallRecording
