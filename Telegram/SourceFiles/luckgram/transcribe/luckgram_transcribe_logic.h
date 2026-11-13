/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QString>

#include <cstdint>
#include <optional>

// D6 (spec 17, 01 B): free voice / round video transcription. Pure logic,
// QtCore only, covered by luckgram_transcribe_tests.cpp.
namespace Luckgram::Transcribe {

inline constexpr auto kFeature = "free_transcribe";
inline constexpr auto kDefaultMaxMb = 25;

// 17.1: non-Premium accounts use Luckgram (Premium keeps Telegram's).
[[nodiscard]] bool UseLuckgram(bool featureOn, bool premium);

// The gateway's size limit ("ai.transcribe_max_mb", default 25 MB).
[[nodiscard]] bool TooLarge(int64_t bytes, double maxMb);

// What the gateway is told the body is: round videos are mp4 with audio.
[[nodiscard]] QString UploadMime(bool roundVideo, const QString &mime);

// Cache key of one message's transcript.
[[nodiscard]] QString CacheKey(int64_t peerId, int64_t msgId);

// Links in the transcript (internal: URLs, core/local_url_handlers.cpp):
// 「重试」 after a failure, 「翻译」 / 「撤销翻译」 under the transcript.
enum class LinkAction {
	Retry,
	Translate,
};
inline constexpr auto kLinkPrefix = "luckgram_transcribe:";
[[nodiscard]] QString Link(LinkAction action, int64_t peerId, int64_t msgId);
struct LinkTarget {
	LinkAction action = LinkAction::Retry;
	int64_t peerId = 0;
	int64_t msgId = 0;
};
// Takes the part after "internal:".
[[nodiscard]] std::optional<LinkTarget> ParseLink(const QString &link);

// 17.4: 「转写失败 · 」 before the 「重试」 link.
[[nodiscard]] QString FailedText(bool chinese);
[[nodiscard]] QString RetryLabel(bool chinese);

} // namespace Luckgram::Transcribe
