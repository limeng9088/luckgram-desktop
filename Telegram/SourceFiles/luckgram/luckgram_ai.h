/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/luckgram_api.h"

#include <QtCore/QJsonObject>

namespace Ui {
class Show;
} // namespace Ui

// D2 §8-9 (mobile 00 LuckgramAI): the only way Luckgram features talk to the
// AI service. Errors carry the server code; Api::Error::text() is the
// message for the user. Every call that sends the user's content goes
// through WithConsent() first (the UI layer, D7, does that; the debug page
// here too).
namespace Luckgram::AI {

struct Message {
	QString role; // system, user, assistant
	QString content;
};

struct ChatArgs {
	QString task = u"chat"_q;
	std::vector<Message> messages;
	bool stream = true;
	// D7: extra top-level request fields, e.g. "effect" (writing, mobile
	// 21) or "agent_id" (market agent, mobile 30). Same rule as Android
	// LuckgramAI.chat(params): they never replace task, messages or stream.
	QJsonObject extra;
};

// POST /v1/ai/chat. onDelta(piece, textSoFar) while streaming (SSE),
// done(fullText) at the end.
Api::RequestPtr Chat(
	ChatArgs &&args,
	Fn<void(QString, QString)> onDelta,
	Fn<void(QString)> done,
	Fn<void(Api::Error)> fail);

struct Image {
	QByteArray data;
	QString mime;
};

// POST /v1/ai/image {"prompt"} -> the image (downloaded if a URL came).
Api::RequestPtr GenerateImage(
	const QString &prompt,
	Fn<void(Image)> done,
	Fn<void(Api::Error)> fail);

// POST /v1/ai/transcribe, the audio as the body (audio/ogg, audio/mpeg,
// audio/mp4, audio/wav). language: ISO-639-1 or empty (auto).
Api::RequestPtr Transcribe(
	QByteArray audio,
	const QString &mime,
	const QString &language,
	Fn<void(QString)> done,
	Fn<void(Api::Error)> fail);

// GET /v1/me (quota, Pro).
Api::RequestPtr Me(Fn<void(QJsonObject)> done, Fn<void(Api::Error)> fail);

// --- Consent (D2 §9): asked once, revocable in Luckgram Features → AI.
[[nodiscard]] bool HasConsent();
void SetConsent(bool given);
[[nodiscard]] rpl::producer<bool> ConsentValue();
// Runs granted() at once if consent was given, else asks first. Declining
// runs denied() and nothing is sent.
void WithConsent(
	std::shared_ptr<Ui::Show> show,
	Fn<void()> granted,
	Fn<void()> denied = nullptr);
// The revoke confirmation from the settings row.
void ShowRevokeConsent(std::shared_ptr<Ui::Show> show);

} // namespace Luckgram::AI
