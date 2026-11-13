/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_compose_logic.h"
#include "luckgram/ai/ai_text_logic.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QSet>

#include <algorithm>
#include <numeric>

namespace Luckgram::AI::Writing {
namespace {

[[nodiscard]] int Find(
		const QString &text,
		const QString &s,
		int from,
		const QSet<QString> &claimed) {
	auto at = int(text.indexOf(s, from));
	while (at >= 0
		&& claimed.contains(QString::number(at) + QChar(':') + s)) {
		at = int(text.indexOf(s, at + 1));
	}
	return at;
}

} // namespace

const std::vector<Effect> &Effects() {
	static const auto result = std::vector<Effect>{
		{ "polish", "润色", "Polish", 100, "已润色", "Polished" },
		{ "formal", "更正式", "More formal", 110, "已改得更正式", "Made more formal" },
		{ "friendly", "更友好", "Friendlier", 120, "已改得更友好", "Made friendlier" },
		{ "shorter", "更简短", "Shorter", 130, "已缩短", "Shortened" },
		{ "grammar", "修正语法", "Fix grammar", 140, "已修正语法", "Grammar fixed" },
	};
	return result;
}

std::vector<int> Relocate(
		const QString &original,
		const std::vector<std::pair<int, int>> &ranges,
		const QString &result) {
	const auto n = int(ranges.size());
	auto out = std::vector<int>(n, -1);
	auto order = std::vector<int>(n);
	std::iota(order.begin(), order.end(), 0);
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
		if (ranges[a].first != ranges[b].first) {
			return ranges[a].first < ranges[b].first;
		}
		return ranges[a].second > ranges[b].second;
	});
	// The same text at the same place may be claimed once (two "😀" must
	// land on two different ones), but different ranges may overlap (bold
	// around a mention).
	auto claimed = QSet<QString>();
	auto cursor = 0;
	for (const auto i : order) {
		const auto start = ranges[i].first;
		const auto end = ranges[i].second;
		if (start < 0 || end > original.size() || start >= end) {
			continue;
		}
		const auto s = original.mid(start, end - start);
		if (s.trimmed().isEmpty()) {
			continue;
		}
		auto at = Find(result, s, cursor, claimed);
		if (at < 0) {
			at = Find(result, s, 0, claimed);
		}
		if (at < 0) {
			continue;
		}
		claimed.insert(QString::number(at) + QChar(':') + s);
		out[i] = at;
		cursor = at + 1;
	}
	return out;
}

} // namespace Luckgram::AI::Writing

namespace Luckgram::AI::Reply {

QString TranscriptLine(const QString &name, QString text) {
	if (text.size() > kMaxMessageChars) {
		text = Text::SafeLeft(text, kMaxMessageChars) + QString::fromUtf8("…");
	}
	text.replace(QChar('\n'), QChar(' '));
	return name + QStringLiteral(": ") + text;
}

QString Prompt(
		const QString &transcript,
		const QString &draft,
		const QString &me) {
	auto result = QStringLiteral("Recent messages (oldest first; \"")
		+ me
		+ QStringLiteral("\" is me):\n")
		+ (transcript.isEmpty() ? QStringLiteral("(none)") : transcript);
	const auto trimmed = draft.trimmed();
	if (!trimmed.isEmpty()) {
		result += QStringLiteral("\n\nMy draft: ") + trimmed;
	} else {
		result += QStringLiteral(
			"\n\nI have no draft; suggest what I could reply.");
	}
	return result;
}

QStringList Candidates(const QString &text) {
	static const auto kMarker = QRegularExpression(
		QString::fromUtf8("^(\\d[.)、]|[-•*])\\s*"));
	auto out = QStringList();
	if (text.isEmpty()) {
		return out;
	}
	for (const auto &raw : text.split(QChar('\n'))) {
		auto line = raw.trimmed();
		if (const auto m = kMarker.match(line); m.hasMatch()) {
			line = line.mid(m.capturedEnd(0));
		}
		const auto quoted = (line.size() >= 2)
			&& ((line.startsWith(QChar('"')) && line.endsWith(QChar('"')))
				|| (line.startsWith(QChar(0x201C))
					&& line.endsWith(QChar(0x201D))));
		if (quoted) {
			line = line.mid(1, line.size() - 2).trimmed();
		}
		if (!line.isEmpty()) {
			out.push_back(line);
		}
		if (out.size() == kCandidates) {
			break;
		}
	}
	return out;
}

} // namespace Luckgram::AI::Reply

namespace Luckgram::AI::Post {
namespace {

[[nodiscard]] QString ExamplesHeader(const QString &examples) {
	return examples.isEmpty()
		? QString()
		: (QStringLiteral("Example posts from this channel (style reference "
			"only, do not copy):\n")
			+ examples
			+ QStringLiteral("---\n\n"));
}

} // namespace

const std::vector<Language> &Languages() {
	static const auto result = std::vector<Language>{
		{ "English", "English" },
		{ "Chinese (Simplified)", "简体中文" },
		{ "Japanese", "日本語" },
		{ "Korean", "한국어" },
		{ "Russian", "Русский" },
		{ "Spanish", "Español" },
		{ "French", "Français" },
		{ "German", "Deutsch" },
	};
	return result;
}

QString Translate(const QString &language) {
	return QStringLiteral("Translate it into ")
		+ language
		+ QStringLiteral(". Keep the formatting and emoji.");
}

QString Examples(const QStringList &posts, int maxChars) {
	auto result = QString();
	auto count = 0;
	for (const auto &post : posts) {
		if (count >= kExamples) {
			break;
		}
		auto t = post.trimmed();
		if (t.isEmpty()) {
			continue;
		}
		if (t.size() > kMaxExampleChars) {
			t = Text::SafeLeft(t, kMaxExampleChars) + QString::fromUtf8("…");
		}
		if (result.size() + t.size() + 8 > maxChars) {
			break;
		}
		result += QStringLiteral("---\n") + t + QChar('\n');
		++count;
	}
	return result;
}

QString Generate(const QString &examples, const QString &topic) {
	return ExamplesHeader(examples)
		+ QStringLiteral("Topic: ")
		+ topic.trimmed()
		+ QStringLiteral("\nWrite a new post for the channel.");
}

QString Edit(
		const QString &examples,
		const QString &draft,
		const QString &instruction) {
	return ExamplesHeader(examples)
		+ QStringLiteral("Current draft:\n")
		+ draft.trimmed()
		+ QStringLiteral("\n\nInstruction: ")
		+ instruction;
}

} // namespace Luckgram::AI::Post
