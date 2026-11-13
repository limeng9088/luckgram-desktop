/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

class HistoryItem;

namespace Window {
class SessionController;
} // namespace Window

// D6 (spec 17 transcription, 01 B its translation): Telegram's voice and
// round video transcription needs Premium. For non-Premium accounts with
// "free_transcribe" on, Api::Transcribes asks Luckgram instead: the audio
// goes to POST /v1/ai/transcribe (after the AI consent), the text shows in
// the bubble like Telegram's, cached per message in the account's
// encrypted storage. Under the text: 「翻译」 (01 B, same rules as 01 A).
namespace Luckgram::Transcribe {

// Api::Transcribes::freeFor(): the → A button without a lock.
[[nodiscard]] bool FreeFor(not_null<const HistoryItem*> item);

// Api::Transcribes::load() for FreeFor() items: the cached text at once,
// else download and transcribe. Never called back after the session ends.
void Request(
	not_null<HistoryItem*> item,
	Fn<void(QString text)> done,
	Fn<void(bool tooLong)> fail);

// The bubble texts (HistoryView::Document) for FreeFor() items: the
// transcript with 「翻译」 or its translation, 「转写失败 · 重试」.
[[nodiscard]] TextWithEntities ResultText(
	not_null<HistoryItem*> item,
	const QString &result);
[[nodiscard]] TextWithEntities FailedText(not_null<HistoryItem*> item);

// internal:luckgram_transcribe:... (core/local_url_handlers.cpp).
bool HandleLink(
	not_null<Window::SessionController*> controller,
	const QString &link);

} // namespace Luckgram::Transcribe
