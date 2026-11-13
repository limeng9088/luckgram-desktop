/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/d9/luckgram_keyword_logic.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <algorithm>
#include <utility>

namespace Luckgram::Keywords {
namespace {

[[nodiscard]] bool IsCjk(QChar ch) {
	// CJK radicals, kana, ideographs, hangul, compatibility, full-width.
	const auto c = ch.unicode();
	return (c >= 0x2E80 && c <= 0x9FFF)
		|| (c >= 0xAC00 && c <= 0xD7AF)
		|| (c >= 0xF900 && c <= 0xFAFF)
		|| (c >= 0xFF00 && c <= 0xFFEF);
}

[[nodiscard]] bool IsWordChar(QChar ch) {
	return (ch.isLetterOrNumber() || ch == QChar('_')) && !IsCjk(ch);
}

[[nodiscard]] bool ContainsCjk(const QString &text) {
	return std::any_of(text.begin(), text.end(), IsCjk);
}

} // namespace

int IndexOf(const QString &text, const QString &keyword, int from) {
	const auto klen = int(keyword.size());
	if (klen == 0 || text.isEmpty()) {
		return -1;
	}
	const auto size = int(text.size());
	const auto limit = std::min(size, kMaxScanLength) - klen;
	const auto substring = ContainsCjk(keyword);
	const auto wordStart = IsWordChar(keyword[0]);
	const auto wordEnd = IsWordChar(keyword[klen - 1]);
	for (auto i = std::max(0, from); i <= limit; ++i) {
		if (QStringView(text).mid(i, klen).compare(
				keyword,
				Qt::CaseInsensitive) != 0) {
			continue;
		}
		if (!substring) {
			if (wordStart && i > 0 && IsWordChar(text[i - 1])) {
				continue;
			} else if (wordEnd
				&& i + klen < size
				&& IsWordChar(text[i + klen])) {
				continue;
			}
		}
		return i;
	}
	return -1;
}

bool Matches(const QString &text, const QString &keyword) {
	return IndexOf(text, keyword, 0) >= 0;
}

QString FirstMatch(const QString &text, const QStringList &keywords) {
	if (text.isEmpty()) {
		return QString();
	}
	for (const auto &keyword : keywords) {
		if (Matches(text, keyword)) {
			return keyword;
		}
	}
	return QString();
}

bool MatchesAny(const QString &text, const QStringList &keywords) {
	return !FirstMatch(text, keywords).isEmpty();
}

std::vector<Range> Highlights(
		const QString &text,
		const QStringList &keywords) {
	auto hits = std::vector<Range>();
	for (const auto &keyword : keywords) {
		const auto klen = int(keyword.size());
		auto index = IndexOf(text, keyword, 0);
		while (index >= 0) {
			hits.push_back({ index, klen });
			index = IndexOf(text, keyword, index + klen);
		}
	}
	std::sort(begin(hits), end(hits), [](const Range &a, const Range &b) {
		return a.from < b.from;
	});
	auto result = std::vector<Range>();
	for (const auto &hit : hits) {
		if (!result.empty()
			&& hit.from <= result.back().from + result.back().length) {
			auto &last = result.back();
			last.length = std::max(
				last.from + last.length,
				hit.from + hit.length) - last.from;
		} else {
			result.push_back(hit);
		}
	}
	return result;
}

bool InScope(int scope, ChatKind kind) {
	switch (kind) {
	case ChatKind::Private: return (scope & kScopePrivate) != 0;
	case ChatKind::Group: return (scope & kScopeGroups) != 0;
	case ChatKind::Channel: return (scope & kScopeChannels) != 0;
	}
	return false;
}

QStringList CleanKeywords(const QStringList &keywords) {
	auto result = QStringList();
	for (const auto &raw : keywords) {
		const auto keyword = raw.trimmed();
		if (keyword.isEmpty()
			|| result.contains(keyword, Qt::CaseInsensitive)) {
			continue;
		}
		result.push_back(keyword);
		if (result.size() >= kMaxKeywords) {
			break;
		}
	}
	return result;
}

Invalid Validate(const Folder &folder) {
	if (folder.name.trimmed().isEmpty()) {
		return Invalid::Name;
	} else if (CleanKeywords(folder.keywords).isEmpty()) {
		return Invalid::Keywords;
	} else if ((folder.scope & kScopeAll) == 0) {
		return Invalid::Scope;
	}
	return Invalid::None;
}

QByteArray Serialize(const std::vector<Folder> &folders) {
	auto list = QJsonArray();
	for (const auto &folder : folders) {
		auto keywords = QJsonArray();
		for (const auto &keyword : folder.keywords) {
			keywords.append(keyword);
		}
		list.append(QJsonObject{
			{ QStringLiteral("id"), folder.id },
			{ QStringLiteral("name"), folder.name },
			{ QStringLiteral("keywords"), keywords },
			{ QStringLiteral("scope"), folder.scope },
			{ QStringLiteral("order"), folder.order },
		});
	}
	return QJsonDocument(list).toJson(QJsonDocument::Compact);
}

std::vector<Folder> Deserialize(const QByteArray &json) {
	auto result = std::vector<Folder>();
	const auto document = QJsonDocument::fromJson(json);
	if (!document.isArray()) {
		return result;
	}
	for (const auto &value : document.array()) {
		if (int(result.size()) >= kMaxFolders) {
			break;
		}
		const auto object = value.toObject();
		auto folder = Folder{
			.id = object.value(QStringLiteral("id")).toInt(),
			.name = object.value(QStringLiteral("name")).toString(),
			.scope = object.value(QStringLiteral("scope")).toInt(kScopeAll),
			.order = object.value(QStringLiteral("order")).toInt(),
		};
		auto keywords = QStringList();
		for (const auto &keyword : object.value(QStringLiteral("keywords")).toArray()) {
			keywords.push_back(keyword.toString());
		}
		folder.keywords = CleanKeywords(keywords);
		if ((folder.scope & kScopeAll) == 0) {
			folder.scope = kScopeAll;
		}
		folder.scope &= kScopeAll;
		const auto duplicate = std::any_of(
			begin(result),
			end(result),
			[&](const Folder &f) { return f.id == folder.id; });
		if (folder.id > 0 && !folder.keywords.isEmpty() && !duplicate) {
			result.push_back(std::move(folder));
		}
	}
	return result;
}

int NextId(const std::vector<Folder> &folders) {
	auto result = 0;
	for (const auto &folder : folders) {
		result = std::max(result, folder.id);
	}
	return result + 1;
}

} // namespace Luckgram::Keywords
