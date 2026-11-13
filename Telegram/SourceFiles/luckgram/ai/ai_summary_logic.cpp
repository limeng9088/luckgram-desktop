/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_summary_logic.h"

#include <QtCore/QRegularExpression>

#include <algorithm>

namespace Luckgram::AI::Summary {
namespace {

[[nodiscard]] QString CutLine(QString s) {
	if (s.size() > kMaxLineChars) {
		s = s.left(kMaxLineChars) + QString::fromUtf8("…");
	}
	return s;
}

[[nodiscard]] QString TrimEndSpaces(const QString &s) {
	auto end = int(s.size());
	while (end > 0 && s[end - 1] == QChar(' ')) {
		--end;
	}
	return s.left(end);
}

[[nodiscard]] QString Pick(bool chinese, const char *zh, const char *en) {
	return QString::fromUtf8(chinese ? zh : en);
}

} // namespace

QString FormatLine(const Line &line) {
	auto text = line.text;
	text.replace(QChar('\n'), QChar(' '));
	return QStringLiteral("[#")
		+ QString::number(line.id)
		+ QStringLiteral("] ")
		+ line.sender
		+ QStringLiteral(" (")
		+ line.time
		+ QStringLiteral("): ")
		+ text.trimmed();
}

QString Header(bool merge) {
	if (merge) {
		return QStringLiteral("These are summaries of consecutive parts of "
			"one chat. Merge them into 3 to 8 bullet points, keeping the "
			"[#ID] citations.\n")
			+ QString::fromLatin1(kCiteRule)
			+ QStringLiteral("\n\n");
	}
	return QStringLiteral("Summarize these chat messages.\n")
		+ QString::fromLatin1(kCiteRule)
		+ QStringLiteral("\n\nMessages (oldest first):\n");
}

QStringList Split(const std::vector<Line> &lines, int maxChars) {
	auto parts = QStringList();
	const auto header = Header(false);
	const auto budget = std::max(1000, maxChars - int(header.size()) - 200);
	auto current = QString();
	for (const auto &line : lines) {
		const auto s = CutLine(FormatLine(line));
		if (!current.isEmpty() && current.size() + s.size() + 1 > budget) {
			parts.push_back(header + current);
			current.clear();
		}
		if (!current.isEmpty()) {
			current.append(QChar('\n'));
		}
		current.append(s);
	}
	if (!current.isEmpty()) {
		parts.push_back(header + current);
	}
	return parts;
}

QString MergeRequest(const QStringList &partSummaries, int maxChars) {
	auto result = Header(true);
	for (auto i = 0; i != int(partSummaries.size()); ++i) {
		result += QStringLiteral("Part ")
			+ QString::number(i + 1)
			+ QStringLiteral(":\n")
			+ partSummaries[i]
			+ QStringLiteral("\n\n");
	}
	if (maxChars > 0 && result.size() > maxChars) {
		result = result.left(maxChars);
	}
	return result;
}

QString AskRequest(
		const std::vector<Line> &lines,
		const QString &question,
		int maxChars) {
	const auto tail = QStringLiteral("\n\nQuestion: ")
		+ question
		+ QStringLiteral("\nAnswer briefly, in the language of the "
			"question. ")
		+ QString::fromLatin1(kCiteRule);
	const auto head = QStringLiteral("Chat messages (oldest first):\n");
	const auto budget = std::max(
		1000,
		maxChars - int(tail.size()) - int(head.size()) - 100);
	auto picked = QStringList();
	auto size = 0;
	for (auto i = int(lines.size()) - 1; i >= 0; --i) {
		const auto s = CutLine(FormatLine(lines[i]));
		if (size + s.size() + 1 > budget) {
			break;
		}
		picked.push_front(s);
		size += int(s.size()) + 1;
	}
	return head + picked.join(QChar('\n')) + tail;
}

QString MediaPlaceholder(Media media, const QString &detail, bool chinese) {
	switch (media) {
	case Media::None: return QString();
	case Media::Photo: return Pick(chinese, "[图片]", "[Photo]");
	case Media::Voice: return Pick(chinese, "[语音]", "[Voice message]");
	case Media::VideoMessage:
		return Pick(chinese, "[视频消息]", "[Video message]");
	case Media::Sticker: return Pick(chinese, "[贴纸]", "[Sticker]");
	case Media::Gif: return QStringLiteral("[GIF]");
	case Media::Video: return Pick(chinese, "[视频]", "[Video]");
	case Media::Music: return Pick(chinese, "[音乐]", "[Music]");
	case Media::File:
		return Pick(chinese, "[文件", "[File")
			+ (detail.isEmpty() ? QString() : (QChar(' ') + detail))
			+ QChar(']');
	case Media::Location: return Pick(chinese, "[位置]", "[Location]");
	case Media::Contact: return Pick(chinese, "[联系人]", "[Contact]");
	case Media::Poll: return Pick(chinese, "[投票] ", "[Poll] ") + detail;
	}
	return QString();
}

QString LineText(
		Media media,
		const QString &detail,
		const QString &text,
		bool chinese) {
	const auto trimmed = text.trimmed();
	const auto placeholder = MediaPlaceholder(media, detail, chinese);
	if (placeholder.isEmpty()) {
		return trimmed;
	}
	return trimmed.isEmpty()
		? placeholder
		: (placeholder + QChar(' ') + trimmed);
}

std::vector<Segment> Parse(
		const QString &text,
		const std::set<qint64> &validIds) {
	static const auto kCitation = QRegularExpression(QString::fromUtf8(
		"[\\[（(]\\s*#?\\d+(?:\\s*[,，、]\\s*#?\\d+)*\\s*[\\]）)]"));
	static const auto kNumber = QRegularExpression(QStringLiteral("\\d+"));

	auto out = std::vector<Segment>();
	if (text.isEmpty()) {
		return out;
	}
	auto last = 0;
	auto matches = kCitation.globalMatch(text);
	while (matches.hasNext()) {
		const auto match = matches.next();
		const auto token = match.captured(0);
		const auto hasHash = token.contains(QChar('#'));
		if (!hasHash && !token.startsWith(QChar('['))) {
			continue; // "(2024)" in normal text is not a citation.
		}
		auto ids = std::vector<qint64>();
		auto numbers = kNumber.globalMatch(token);
		while (numbers.hasNext()) {
			auto ok = false;
			const auto id = numbers.next().captured(0).toLongLong(&ok);
			if (ok
				&& validIds.count(id)
				&& std::find(ids.begin(), ids.end(), id) == ids.end()) {
				ids.push_back(id);
			}
		}
		if (ids.empty() && !hasHash) {
			continue; // e.g. "[1]" that is not one of our IDs: keep the text.
		}
		const auto before = TrimEndSpaces(
			text.mid(last, match.capturedStart(0) - last));
		if (!before.isEmpty()) {
			out.push_back({ before, 0 });
		}
		for (const auto id : ids) {
			out.push_back({ QString(), id });
		}
		last = int(match.capturedEnd(0));
	}
	if (last < text.size()) {
		out.push_back({ text.mid(last), 0 });
	}
	return out;
}

QString Describe(const std::vector<Segment> &segments) {
	auto parts = QStringList();
	for (const auto &segment : segments) {
		parts.push_back(segment.messageId
			? (QChar('{') + QString::number(segment.messageId) + QChar('}'))
			: segment.text);
	}
	return QChar('[') + parts.join(QStringLiteral(", ")) + QChar(']');
}

QStringList Bullets(const QString &text) {
	static const auto kNumbered = QRegularExpression(
		QString::fromUtf8("^\\d{1,2}[.)、]\\s*"));
	auto out = QStringList();
	if (text.isEmpty()) {
		return out;
	}
	for (const auto &raw : text.split(QChar('\n'))) {
		auto line = raw.trimmed();
		if (line.isEmpty()) {
			continue;
		}
		if (line.startsWith(QLatin1String("- "))
			|| line.startsWith(QLatin1String("* "))
			|| line.startsWith(QString::fromUtf8("• "))) {
			line = line.mid(2).trimmed();
		} else if (const auto m = kNumbered.match(line); m.hasMatch()) {
			line = line.mid(m.capturedEnd(0));
		} else if (!out.isEmpty()) {
			// Continuation of the previous point.
			out.back() += QChar(' ') + line;
			continue;
		}
		if (!line.isEmpty()) {
			out.push_back(line);
		}
	}
	return out;
}

} // namespace Luckgram::AI::Summary
