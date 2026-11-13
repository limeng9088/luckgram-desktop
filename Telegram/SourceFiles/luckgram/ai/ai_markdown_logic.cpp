/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_markdown_logic.h"

#include <QtCore/QStringList>

namespace Luckgram::AI::Markdown {
namespace {

[[nodiscard]] bool StartsAt(const QString &s, int i, QLatin1String marker) {
	return (i >= 0)
		&& (i + marker.size() <= s.size())
		&& (QStringView(s).mid(i, marker.size()) == marker);
}

[[nodiscard]] bool IsBullet(const QString &t) {
	if (t.size() <= 2 || t[1] != QChar(' ')) {
		return false;
	}
	const auto c = t[0];
	return (c == QChar('-')) || (c == QChar('*')) || (c == QChar('+'));
}

void Inline(const QString &s, QString &out, std::vector<Span> &spans) {
	static const auto kEscapable = QStringLiteral("\\`*_~[]()#");
	auto i = 0;
	const auto n = int(s.size());
	while (i < n) {
		const auto c = s[i];
		if (c == QChar('\\')
			&& i + 1 < n
			&& kEscapable.contains(s[i + 1])) {
			out.append(s[i + 1]);
			i += 2;
			continue;
		}
		if (c == QChar('`')) {
			const auto end = int(s.indexOf(QChar('`'), i + 1));
			if (end > i + 1) {
				const auto start = int(out.size());
				out.append(s.mid(i + 1, end - i - 1));
				spans.push_back({ Type::Code, start, int(out.size()) });
				i = end + 1;
				continue;
			}
		}
		if (StartsAt(s, i, QLatin1String("**"))
			|| StartsAt(s, i, QLatin1String("__"))) {
			const auto marker = s.mid(i, 2);
			const auto end = int(s.indexOf(marker, i + 2));
			if (end > i + 2) {
				const auto start = int(out.size());
				Inline(s.mid(i + 2, end - i - 2), out, spans);
				spans.push_back({ Type::Bold, start, int(out.size()) });
				i = end + 2;
				continue;
			}
		}
		if (StartsAt(s, i, QLatin1String("~~"))) {
			const auto end = int(s.indexOf(QStringLiteral("~~"), i + 2));
			if (end > i + 2) {
				const auto start = int(out.size());
				Inline(s.mid(i + 2, end - i - 2), out, spans);
				spans.push_back({ Type::Strike, start, int(out.size()) });
				i = end + 2;
				continue;
			}
		}
		if ((c == QChar('*') || c == QChar('_'))
			&& i + 1 < n
			&& s[i + 1] != QChar(' ')
			&& s[i + 1] != c
			&& (c == QChar('*')
				|| i == 0
				|| !s[i - 1].isLetterOrNumber())) {
			const auto end = int(s.indexOf(c, i + 1));
			if (end > i + 1
				&& s[end - 1] != QChar(' ')
				&& (c == QChar('*')
					|| end + 1 >= n
					|| !s[end + 1].isLetterOrNumber())) {
				const auto start = int(out.size());
				Inline(s.mid(i + 1, end - i - 1), out, spans);
				spans.push_back({ Type::Italic, start, int(out.size()) });
				i = end + 1;
				continue;
			}
		}
		if (c == QChar('[')) {
			const auto close = int(s.indexOf(QStringLiteral("]("), i + 1));
			const auto paren = (close < 0)
				? -1
				: int(s.indexOf(QChar(')'), close + 2));
			if (close > i + 1 && paren > close + 2) {
				const auto url = s.mid(close + 2, paren - close - 2).trimmed();
				if (url.startsWith(QLatin1String("http://"))
					|| url.startsWith(QLatin1String("https://"))
					|| url.startsWith(QLatin1String("tg://"))) {
					const auto start = int(out.size());
					Inline(s.mid(i + 1, close - i - 1), out, spans);
					spans.push_back({ Type::Link, start, int(out.size()), url });
					i = paren + 1;
					continue;
				}
			}
		}
		out.append(c);
		++i;
	}
}

} // namespace

Result Parse(const QString &markdown) {
	auto result = Result();
	auto &out = result.text;
	auto &spans = result.spans;
	if (markdown.isEmpty()) {
		return result;
	}
	auto normalized = markdown;
	normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
	const auto lines = normalized.split(QChar('\n'), Qt::KeepEmptyParts);
	const auto count = int(lines.size());
	auto i = 0;
	while (i < count) {
		const auto &line = lines[i];
		const auto trimmed = line.trimmed();
		if (trimmed.startsWith(QLatin1String("```"))) {
			const auto language = trimmed.mid(3).trimmed();
			auto code = QString();
			auto j = i + 1;
			auto closed = false;
			for (; j < count; ++j) {
				if (lines[j].trimmed().startsWith(QLatin1String("```"))) {
					closed = true;
					break;
				}
				if (!code.isEmpty()) {
					code.append(QChar('\n'));
				}
				code.append(lines[j]);
			}
			const auto start = int(out.size());
			out.append(code);
			spans.push_back({ Type::CodeBlock, start, int(out.size()), language });
			i = closed ? (j + 1) : j;
			if (i < count) {
				out.append(QChar('\n'));
			}
			continue;
		}
		auto level = 0;
		while (level < trimmed.size()
			&& level < 6
			&& trimmed[level] == QChar('#')) {
			++level;
		}
		if (level > 0
			&& level < trimmed.size()
			&& trimmed[level] == QChar(' ')) {
			const auto start = int(out.size());
			Inline(trimmed.mid(level + 1).trimmed(), out, spans);
			spans.push_back({
				Type::Heading,
				start,
				int(out.size()),
				QString::number(level),
			});
		} else if (IsBullet(trimmed)) {
			const auto indent = int(line.indexOf(trimmed[0]));
			for (auto k = 0; k < indent / 2; ++k) {
				out.append(QLatin1String("  "));
			}
			out.append(QStringLiteral("• "));
			Inline(trimmed.mid(2).trimmed(), out, spans);
		} else {
			Inline(line, out, spans);
		}
		++i;
		if (i < count) {
			out.append(QChar('\n'));
		}
	}
	return result;
}

} // namespace Luckgram::AI::Markdown
