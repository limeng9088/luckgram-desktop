/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_image_logic.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QSet>

namespace Luckgram::AI::Draw {
namespace {

constexpr auto kNamePrefix = "ai_image_";
constexpr auto kMaxNameLength = 64; // luckgram_storage_logic.h

[[nodiscard]] QString StateText(State state) {
	switch (state) {
	case State::Pending: return QStringLiteral("pending");
	case State::Done: return QStringLiteral("done");
	case State::Error: return QStringLiteral("error");
	}
	return QStringLiteral("pending");
}

[[nodiscard]] bool IsNameChar(QChar ch) {
	const auto c = ch.unicode();
	return (c >= 'a' && c <= 'z')
		|| (c >= '0' && c <= '9')
		|| (c == '_')
		|| (c == '-');
}

} // namespace

const QStringList &Prefixes() {
	static const auto result = QStringList{
		QStringLiteral("/image"),
		QString::fromUtf8("画"),
	};
	return result;
}

std::optional<Intercepted> Intercept(const QString &text) {
	const auto trimmed = text.trimmed();
	for (const auto &prefix : Prefixes()) {
		if (!trimmed.startsWith(prefix, Qt::CaseInsensitive)) {
			continue;
		}
		const auto prompt = trimmed.mid(prefix.size()).trimmed();
		if (prompt.isEmpty()) {
			return std::nullopt;
		}
		return Intercepted{ prefix, prompt };
	}
	return std::nullopt;
}

QString QuickPrefix(bool chinese) {
	return chinese ? QString::fromUtf8("画 ") : QStringLiteral("/image ");
}

QJsonObject Data::toJson() const {
	auto result = QJsonObject();
	result.insert(QStringLiteral("state"), StateText(state));
	if (!file.isEmpty()) {
		result.insert(QStringLiteral("file"), file);
	}
	if (!mime.isEmpty()) {
		result.insert(QStringLiteral("mime"), mime);
	}
	if (!error.isEmpty()) {
		result.insert(QStringLiteral("error"), error);
	}
	return result;
}

Data Data::FromJson(const QJsonObject &object) {
	auto result = Data();
	const auto state = object.value(QStringLiteral("state")).toString();
	result.file = object.value(QStringLiteral("file")).toString();
	result.mime = object.value(QStringLiteral("mime")).toString();
	result.error = object.value(QStringLiteral("error")).toString();
	if (state == QLatin1String("done") && !result.file.isEmpty()) {
		result.state = State::Done;
	} else if (state == QLatin1String("error")) {
		result.state = State::Error;
	} else if (state == QLatin1String("done")) {
		// Done without a picture: nothing to show, offer 重画.
		result.state = State::Error;
	}
	return result;
}

QString StorageName(const QString &messageId) {
	const auto id = messageId.toLower();
	if (id.isEmpty()) {
		return QString();
	}
	for (const auto ch : id) {
		if (!IsNameChar(ch)) {
			return QString();
		}
	}
	const auto result = QString::fromLatin1(kNamePrefix) + id;
	return (result.size() <= kMaxNameLength) ? result : QString();
}

QString Extension(const QString &mime) {
	const auto lower = mime.toLower();
	if (lower.contains(QLatin1String("jpeg"))
		|| lower.contains(QLatin1String("jpg"))) {
		return QStringLiteral("jpg");
	} else if (lower.contains(QLatin1String("webp"))) {
		return QStringLiteral("webp");
	}
	return QStringLiteral("png");
}

QByteArray SerializeIndex(const std::vector<Written> &list) {
	auto array = QJsonArray();
	for (const auto &entry : list) {
		auto object = QJsonObject();
		object.insert(QStringLiteral("c"), entry.conversation);
		object.insert(QStringLiteral("f"), entry.file);
		array.append(object);
	}
	return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

std::vector<Written> DeserializeIndex(const QByteArray &bytes) {
	auto result = std::vector<Written>();
	auto seen = QSet<QString>();
	const auto array = QJsonDocument::fromJson(bytes).array();
	for (const auto &value : array) {
		const auto object = value.toObject();
		auto entry = Written{
			.conversation = object.value(QStringLiteral("c")).toString(),
			.file = object.value(QStringLiteral("f")).toString(),
		};
		if (entry.conversation.isEmpty()
			|| !entry.file.startsWith(QLatin1String(kNamePrefix))
			|| seen.contains(entry.file)) {
			continue;
		}
		seen.insert(entry.file);
		result.push_back(std::move(entry));
	}
	return result;
}

QStringList Orphans(
		const std::vector<Written> &written,
		const QString &conversation,
		const std::optional<QStringList> &referenced) {
	auto result = QStringList();
	for (const auto &entry : written) {
		if (entry.conversation != conversation
			|| (referenced && referenced->contains(entry.file))
			|| result.contains(entry.file)) {
			continue;
		}
		result.push_back(entry.file);
	}
	return result;
}

} // namespace Luckgram::AI::Draw
