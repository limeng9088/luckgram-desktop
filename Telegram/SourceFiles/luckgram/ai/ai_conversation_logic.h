/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QString>

#include <functional>
#include <optional>
#include <vector>

// D7 (mobile 18): one local AI conversation. It lives only on this device,
// never as a Telegram chat. QtCore only (luckgram_tests); same JSON and
// rules as Android LuckgramAiConversation, so stored data reads the same.
//
// Extension point "对话属性" (18 需求 8): task and system are sent with every
// request of the conversation; key lets a feature find its own conversation
// again (22: one per agent, "agent:<id>"; 30: "market:<id>"); params adds
// top-level request fields (30: agent_id).
namespace Luckgram::AI::Conversations {

// 18 需求 6: messages sent as context with each request.
inline constexpr auto kContextMessages = 20;

inline constexpr auto kRoleUser = "user";
inline constexpr auto kRoleAssistant = "assistant";
inline constexpr auto kRoleSystem = "system";
// Plain text (Markdown for replies). Other types come from the registry
// (18 需求 8 "消息类型", e.g. 23's image).
inline constexpr auto kTypeText = "text";

struct Message {
	QString id;
	QString role = QString::fromLatin1(kRoleUser);
	QString type = QString::fromLatin1(kTypeText);
	std::optional<QString> text;
	QJsonObject data; // Payload of registered types (e.g. an image file).
	std::optional<QString> errorCode; // Set when the request failed.
	qint64 time = 0; // Unix ms.
	bool pending = false; // Streaming in; not saved.

	[[nodiscard]] bool isUser() const;
	[[nodiscard]] QJsonObject toJson() const;
	[[nodiscard]] static Message FromJson(const QJsonObject &object);
};

struct Conversation {
	QString id;
	std::optional<QString> key;
	std::optional<QString> title;
	QString task = QStringLiteral("chat");
	std::optional<QString> system;
	QJsonObject params;
	qint64 createdAt = 0;
	qint64 updatedAt = 0;
	std::vector<Message> messages;

	[[nodiscard]] QJsonObject toJson() const;
	[[nodiscard]] static Conversation FromJson(const QJsonObject &object);
};

struct RequestMessage {
	QString role;
	QString content;

	friend inline bool operator==(
			const RequestMessage &a,
			const RequestMessage &b) {
		return (a.role == b.role) && (a.content == b.content);
	}
};

// What a message tells the model ("" = nothing, skipped): the text for
// text messages; registered types provide their own (23: the prompt).
using ContextText = std::function<QString(const Message&)>;
[[nodiscard]] QString DefaultContextText(const Message &message);

// The request messages: the system instruction, then the last
// kContextMessages messages that have text for the model (failed and
// pending ones skipped), oldest dropped first if the total would go over
// maxChars (ai.max_input_chars). If the newest message alone is too long,
// its end is sent.
[[nodiscard]] std::vector<RequestMessage> RequestMessages(
	const Conversation &conversation,
	int maxChars,
	const ContextText &contextText = DefaultContextText);

// The title a conversation gets from its first user message: one line, at
// most 40 characters + "…" (the history list shows "新对话" while empty).
[[nodiscard]] QString AutoTitle(const QString &firstUserText);

// The history row's second line: the last message with text, one line.
[[nodiscard]] QString PreviewText(const Conversation &conversation);

// History list order: most recently updated first.
void SortForHistory(std::vector<Conversation> &list);

} // namespace Luckgram::AI::Conversations
