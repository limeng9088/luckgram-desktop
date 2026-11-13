/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/d9/luckgram_notes_text.h"

#include <QtCore/QStringList>
#include <QtCore/QTextBoundaryFinder>

namespace Luckgram::NotesText {
namespace {

[[nodiscard]] QString Fold(const QString &text) {
	return text.toLower();
}

[[nodiscard]] QStringList Lines(const QString &note) {
	auto normalized = note;
	normalized.replace(u"\r\n"_q, u"\n"_q);
	normalized.replace(QChar('\r'), QChar('\n'));
	return normalized.split(QChar('\n'));
}

} // namespace

int GraphemeCount(const QString &text) {
	if (text.isEmpty()) {
		return 0;
	}
	auto finder = QTextBoundaryFinder(
		QTextBoundaryFinder::Grapheme,
		text);
	auto count = 0;
	while (finder.toNextBoundary() > 0) {
		++count;
	}
	return count;
}

int OffsetOfGrapheme(const QString &text, int index) {
	if (index <= 0 || text.isEmpty()) {
		return 0;
	}
	auto finder = QTextBoundaryFinder(
		QTextBoundaryFinder::Grapheme,
		text);
	auto position = 0;
	for (auto i = 0; i != index; ++i) {
		const auto next = finder.toNextBoundary();
		if (next < 0) {
			return int(text.size());
		}
		position = int(next);
	}
	return position;
}

QString Normalize(const QString &note) {
	auto value = note.trimmed();
	if (GraphemeCount(value) > kMaxLength) {
		value = value.left(OffsetOfGrapheme(value, kMaxLength)).trimmed();
	}
	return value;
}

int Remaining(const QString &text) {
	return kMaxLength - GraphemeCount(text);
}

QString FirstLine(const QString &note) {
	for (const auto &line : Lines(note)) {
		const auto trimmed = line.trimmed();
		if (!trimmed.isEmpty()) {
			return trimmed;
		}
	}
	return QString();
}

bool Matches(
		const QString &query,
		const QString &note,
		const QString &name) {
	const auto q = Fold(query.trimmed());
	if (q.isEmpty()) {
		return true;
	}
	return Fold(note).contains(q) || Fold(name).contains(q);
}

QString Snippet(const QString &note, const QString &query) {
	const auto q = Fold(query.trimmed());
	if (!q.isEmpty()) {
		for (const auto &line : Lines(note)) {
			if (Fold(line).contains(q)) {
				return line.trimmed();
			}
		}
	}
	return FirstLine(note);
}

} // namespace Luckgram::NotesText
