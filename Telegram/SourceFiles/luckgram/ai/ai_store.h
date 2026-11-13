/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/ai/ai_conversation_logic.h"

namespace Main {
class Session;
} // namespace Main

// D7 (mobile 18 需求 6): the AI conversations of one Telegram account, only on
// this device (tdata/luckgram_ai/<user id>.json, same JSON as Android
// LuckgramAiStore). Nothing here talks to Telegram.
namespace Luckgram::AI {

class Store final {
public:
	using Conversation = Conversations::Conversation;
	using Message = Conversations::Message;

	[[nodiscard]] static Store &For(not_null<Main::Session*> session);
	// Logged out of every account (D2 OnLoggedOutCompletely).
	static void ClearAll();

	// Most recently updated first.
	[[nodiscard]] std::vector<Conversation> list() const;
	[[nodiscard]] std::optional<Conversation> find(const QString &id) const;
	[[nodiscard]] std::optional<Conversation> findByKey(
		const QString &key) const;

	// A new empty conversation (task "chat" unless given), saved.
	Conversation create(
		const QString &task = QString(),
		const std::optional<QString> &system = std::nullopt,
		const std::optional<QString> &title = std::nullopt,
		const std::optional<QString> &key = std::nullopt,
		const QJsonObject &params = QJsonObject());
	// 18 需求 8 "对话属性": the conversation of a feature (e.g. one per agent,
	// key "agent:<id>"), created on first use. Task, system and params are
	// refreshed from the arguments every time.
	Conversation findOrCreate(
		const QString &key,
		const QString &task,
		const std::optional<QString> &system,
		const QString &title,
		const QJsonObject &params = QJsonObject());

	// Replaces (or adds) the conversation and writes the file. Messages
	// still pending are kept in memory only.
	void save(const Conversation &conversation);
	void remove(const QString &id);

	// The id of the conversation that changed ("" = several / all).
	[[nodiscard]] rpl::producer<QString> changes() const;

	explicit Store(uint64 userId);

private:
	void load();
	void write() const;
	[[nodiscard]] QString path() const;

	uint64 _userId = 0;
	std::vector<Conversation> _list;
	rpl::event_stream<QString> _changes;

};

[[nodiscard]] qint64 NowMs();
[[nodiscard]] QString NewId();

} // namespace Luckgram::AI
