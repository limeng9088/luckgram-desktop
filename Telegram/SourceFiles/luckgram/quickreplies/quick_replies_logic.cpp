/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/quickreplies/quick_replies_logic.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTextBoundaryFinder>

namespace Luckgram::QuickReplies {
namespace {

// UTF-16 offset where grapheme number `index` starts (or the size).
[[nodiscard]] int OffsetOfGrapheme(const QString &text, int index) {
	auto finder = QTextBoundaryFinder(
		QTextBoundaryFinder::Grapheme,
		text);
	auto count = 0;
	auto position = 0;
	while (count < index) {
		const auto next = finder.toNextBoundary();
		if (next < 0) {
			return int(text.size());
		}
		position = int(next);
		++count;
	}
	return position;
}

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

} // namespace

int GraphemeCount(const QString &text) {
	if (text.isEmpty()) {
		return 0;
	}
	auto finder = QTextBoundaryFinder(
		QTextBoundaryFinder::Grapheme,
		text);
	auto result = 0;
	while (finder.toNextBoundary() >= 0) {
		++result;
	}
	return result;
}

std::optional<QString> Normalize(const QString &text) {
	auto result = text.trimmed();
	if (result.isEmpty()) {
		return std::nullopt;
	}
	result = result.left(OffsetOfGrapheme(result, kMaxLength)).trimmed();
	return result.isEmpty() ? std::nullopt : std::make_optional(result);
}

bool CanAdd(int currentCount) {
	return currentCount < kMaxCount;
}

QStringList Matching(const QStringList &items, const QString &query) {
	auto result = QStringList();
	for (const auto &item : items) {
		if (item.startsWith(query, Qt::CaseInsensitive)) {
			result.push_back(item);
		}
	}
	return result;
}

std::optional<QString> SlashQuery(const QString &field) {
	if (!field.startsWith(QChar('/'))
		|| field.contains(QChar('\n'))) {
		return std::nullopt;
	}
	return field.mid(1);
}

QStringList Move(const QStringList &items, int from, int to) {
	auto result = items;
	const auto size = int(result.size());
	if (from < 0 || from >= size || to < 0 || to >= size || from == to) {
		return result;
	}
	result.move(from, to);
	return result;
}

QStringList Presets(bool chinese) {
	return chinese
		? QStringList{
			U("在路上了！"),
			U("10 分钟后给你回电话可以吗？"),
			U("谢谢，收到。"),
			U("抱歉，要晚 5 分钟！"),
		}
		: QStringList{
			U("On my way!"),
			U("Can I call you back in 10 minutes?"),
			U("Thanks, got it."),
			U("Sorry, I'll be 5 minutes late!"),
		};
}

QByteArray Serialize(const Data &data) {
	auto items = QJsonArray();
	for (const auto &item : data.items) {
		items.push_back(item);
	}
	auto object = QJsonObject();
	object.insert(U("items"), items);
	object.insert(U("seeded"), data.seeded);
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

Data Parse(const QByteArray &bytes) {
	auto result = Data();
	const auto document = QJsonDocument::fromJson(bytes);
	if (!document.isObject()) {
		return result;
	}
	const auto object = document.object();
	result.seeded = object.value(U("seeded")).toBool();
	for (const auto &value : object.value(U("items")).toArray()) {
		if (result.items.size() >= kMaxCount) {
			break;
		}
		if (const auto item = Normalize(value.toString())) {
			result.items.push_back(*item);
		}
	}
	return result;
}

Data Seeded(Data data, bool chinese) {
	if (!data.seeded) {
		data.items = Presets(chinese) + data.items;
		while (data.items.size() > kMaxCount) {
			data.items.pop_back();
		}
		data.seeded = true;
	}
	return data;
}

} // namespace Luckgram::QuickReplies
