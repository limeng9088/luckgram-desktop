/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <optional>
#include <vector>

// D7 / mobile 23 (AI 画图): the rules without UI, QtCore only
// (luckgram_tests). Same prefixes and states as Android LuckgramAiImage.
namespace Luckgram::AI::Draw {

// The message type registered with the assistant (18 需求 8).
inline constexpr auto kType = "image";

// 23 需求 1: "/image" and "画" start a drawing, case-insensitive. The
// order is Android's registration order.
[[nodiscard]] const QStringList &Prefixes();

struct Intercepted {
	QString prefix; // As registered.
	QString prompt; // The rest, trimmed; never empty.
};

// What the interceptor takes: a prefix followed by a description. Only a
// prefix (or nothing after it) is not taken, so it goes to the chat as
// typed, like on Android.
[[nodiscard]] std::optional<Intercepted> Intercept(const QString &text);

// What the 「画图」 quick button puts into the field.
[[nodiscard]] QString QuickPrefix(bool chinese);

enum class State {
	Pending,
	Done,
	Error,
};

// Message.data of an image message.
struct Data {
	State state = State::Pending;
	QString file; // Account storage name of the picture (Done).
	QString mime;
	QString error; // Server error code (Error).

	[[nodiscard]] QJsonObject toJson() const;
	[[nodiscard]] static Data FromJson(const QJsonObject &object);

	friend inline bool operator==(const Data &a, const Data &b) {
		return (a.state == b.state)
			&& (a.file == b.file)
			&& (a.mime == b.mime)
			&& (a.error == b.error);
	}
};

// The account storage name of a message's picture: "ai_image_<id>", or
// empty when the id can't be part of a storage name.
[[nodiscard]] QString StorageName(const QString &messageId);

// The file extension for "保存" (png when unknown).
[[nodiscard]] QString Extension(const QString &mime);

// Pictures this device wrote, so the ones of removed conversations and
// messages can be deleted (the assistant's store keeps no files of its own).
struct Written {
	QString conversation;
	QString file;

	friend inline bool operator==(const Written &a, const Written &b) {
		return (a.conversation == b.conversation) && (a.file == b.file);
	}
};
[[nodiscard]] QByteArray SerializeIndex(const std::vector<Written> &list);
[[nodiscard]] std::vector<Written> DeserializeIndex(const QByteArray &bytes);

// Pictures of one conversation to delete after it changed: all of them
// when it is gone (referenced is nullopt), else those no message refers
// to any more.
[[nodiscard]] QStringList Orphans(
	const std::vector<Written> &written,
	const QString &conversation,
	const std::optional<QStringList> &referenced);

} // namespace Luckgram::AI::Draw
