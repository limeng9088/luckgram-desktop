/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_conversation_logic.h"
#include "luckgram/ai/ai_text_logic.h"

#include <QtCore/QJsonArray>
#include <QtCore/QUuid>

#include <algorithm>

namespace Luckgram::AI::Conversations {
namespace {

[[nodiscard]] QString NewId() {
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

[[nodiscard]] std::optional<QString> OptionalString(
		const QJsonObject &object,
		const QString &key) {
	const auto value = object.value(key);
	if (value.isUndefined() || value.isNull()) {
		return std::nullopt;
	}
	return value.toVariant().toString();
}

[[nodiscard]] QString StringOr(
		const QJsonObject &object,
		const QString &key,
		const QString &def) {
	return OptionalString(object, key).value_or(def);
}

[[nodiscard]] qint64 LongOr(
		const QJsonObject &object,
		const QString &key,
		qint64 def) {
	const auto value = object.value(key);
	return value.isDouble() ? qint64(value.toDouble()) : def;
}

} // namespace

bool Message::isUser() const {
	return role == QLatin1String(kRoleUser);
}

QJsonObject Message::toJson() const {
	auto result = QJsonObject();
	result.insert(QStringLiteral("id"), id);
	result.insert(QStringLiteral("role"), role);
	result.insert(QStringLiteral("type"), type);
	result.insert(QStringLiteral("time"), double(time));
	if (text) {
		result.insert(QStringLiteral("text"), *text);
	}
	if (!data.isEmpty()) {
		result.insert(QStringLiteral("data"), data);
	}
	if (errorCode) {
		result.insert(QStringLiteral("error"), *errorCode);
	}
	return result;
}

Message Message::FromJson(const QJsonObject &o) {
	auto result = Message();
	result.id = StringOr(o, QStringLiteral("id"), NewId());
	result.role = StringOr(
		o,
		QStringLiteral("role"),
		QString::fromLatin1(kRoleUser));
	result.type = StringOr(
		o,
		QStringLiteral("type"),
		QString::fromLatin1(kTypeText));
	result.text = OptionalString(o, QStringLiteral("text"));
	result.data = o.value(QStringLiteral("data")).toObject();
	result.errorCode = OptionalString(o, QStringLiteral("error"));
	result.time = LongOr(o, QStringLiteral("time"), 0);
	return result;
}

QJsonObject Conversation::toJson() const {
	auto result = QJsonObject();
	result.insert(QStringLiteral("id"), id);
	result.insert(QStringLiteral("task"), task);
	result.insert(QStringLiteral("created"), double(createdAt));
	result.insert(QStringLiteral("updated"), double(updatedAt));
	if (key) {
		result.insert(QStringLiteral("key"), *key);
	}
	if (title) {
		result.insert(QStringLiteral("title"), *title);
	}
	if (system) {
		result.insert(QStringLiteral("system"), *system);
	}
	if (!params.isEmpty()) {
		result.insert(QStringLiteral("params"), params);
	}
	auto list = QJsonArray();
	for (const auto &message : messages) {
		if (!message.pending) {
			list.push_back(message.toJson());
		}
	}
	result.insert(QStringLiteral("messages"), list);
	return result;
}

Conversation Conversation::FromJson(const QJsonObject &o) {
	auto result = Conversation();
	result.id = StringOr(o, QStringLiteral("id"), NewId());
	result.key = OptionalString(o, QStringLiteral("key"));
	result.title = OptionalString(o, QStringLiteral("title"));
	result.task = StringOr(o, QStringLiteral("task"), QStringLiteral("chat"));
	result.system = OptionalString(o, QStringLiteral("system"));
	result.params = o.value(QStringLiteral("params")).toObject();
	result.createdAt = LongOr(o, QStringLiteral("created"), 0);
	result.updatedAt = LongOr(o, QStringLiteral("updated"), result.createdAt);
	for (const auto &value : o.value(QStringLiteral("messages")).toArray()) {
		if (value.isObject()) {
			result.messages.push_back(Message::FromJson(value.toObject()));
		}
	}
	return result;
}

QString DefaultContextText(const Message &message) {
	return (message.type == QLatin1String(kTypeText) && message.text)
		? *message.text
		: QString();
}

std::vector<RequestMessage> RequestMessages(
		const Conversation &conversation,
		int maxChars,
		const ContextText &contextText) {
	auto picked = std::vector<RequestMessage>();
	const auto &system = conversation.system;
	auto chars = system ? int(system->size()) : 0;
	auto count = 0;
	const auto &messages = conversation.messages;
	for (auto i = int(messages.size()) - 1;
		i >= 0 && count < kContextMessages;
		--i) {
		const auto &m = messages[i];
		if (m.pending || m.errorCode) {
			continue;
		}
		auto text = contextText ? contextText(m) : DefaultContextText(m);
		if (text.isEmpty()) {
			continue;
		}
		if (chars + text.size() > maxChars) {
			if (picked.empty()) {
				// The newest message alone is too long: send its end, the
				// server would reject all of it.
				text = Text::SafeRight(text, std::max(0, maxChars - chars));
			} else {
				break;
			}
		}
		chars += int(text.size());
		picked.insert(picked.begin(), RequestMessage{
			m.isUser()
				? QString::fromLatin1(kRoleUser)
				: QString::fromLatin1(kRoleAssistant),
			text,
		});
		++count;
	}
	if (system && !system->isEmpty()) {
		picked.insert(picked.begin(), RequestMessage{
			QString::fromLatin1(kRoleSystem),
			*system,
		});
	}
	return picked;
}

QString AutoTitle(const QString &firstUserText) {
	auto t = firstUserText;
	t.replace(QChar('\n'), QChar(' '));
	t = t.trimmed();
	return (t.size() > 40) ? (Text::SafeLeft(t, 40) + QString::fromUtf8("…")) : t;
}

QString PreviewText(const Conversation &conversation) {
	const auto &messages = conversation.messages;
	for (auto i = int(messages.size()) - 1; i >= 0; --i) {
		const auto &text = messages[i].text;
		if (text && !text->isEmpty()) {
			auto result = *text;
			result.replace(QChar('\n'), QChar(' '));
			return result;
		}
	}
	return QString();
}

void SortForHistory(std::vector<Conversation> &list) {
	std::stable_sort(list.begin(), list.end(), [](
			const Conversation &a,
			const Conversation &b) {
		return a.updatedAt > b.updatedAt;
	});
}

} // namespace Luckgram::AI::Conversations
