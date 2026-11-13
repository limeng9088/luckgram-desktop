/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// D7 / mobile 23 (AI 画图), entirely through the assistant's extension
// points (ai_registry.h): the 「画图」 quick button, an input interceptor for
// 「画」 and "/image", and the "image" message type. The picture comes from
// /v1/ai/image (luckgram_ai.h GenerateImage) and is kept per conversation in
// the encrypted account storage (luckgram_storage.h, docs/desktop.md),
// removed with its conversation and on log out. Under the picture:
// 「发送给…」 (Telegram's chat picker, sent as a normal photo), 「保存」 (a
// file on disk the user picks) and 「重画」 (the same description again).
namespace Luckgram::AI::Draw {

// The registration line in RegisterAll() (ai_registry.cpp).
void Register();

} // namespace Luckgram::AI::Draw
