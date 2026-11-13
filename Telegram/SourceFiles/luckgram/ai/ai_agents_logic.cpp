/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_agents_logic.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUuid>

#include <algorithm>

namespace Luckgram::AI::Agents {
namespace {

[[nodiscard]] Agent Preset(
		const char *id,
		const char *nameZh,
		const char *nameEn,
		const char *descriptionZh,
		const char *descriptionEn,
		quint32 color,
		const char *prompt) {
	auto result = Agent();
	result.id = QString::fromLatin1(id);
	result.preset = true;
	result.nameZh = QString::fromUtf8(nameZh);
	result.nameEn = QString::fromUtf8(nameEn);
	result.descriptionZh = QString::fromUtf8(descriptionZh);
	result.descriptionEn = QString::fromUtf8(descriptionEn);
	result.color = color;
	result.prompt = QString::fromUtf8(prompt);
	return result;
}

// First character (one code point) of s, upper case; "?" when empty.
[[nodiscard]] QString Initial(const QString &s) {
	const auto t = s.trimmed();
	if (t.isEmpty()) {
		return QStringLiteral("?");
	}
	const auto length = (t[0].isHighSurrogate()
		&& t.size() > 1
		&& t[1].isLowSurrogate()) ? 2 : 1;
	return t.left(length).toUpper();
}

} // namespace

const std::vector<quint32> &Colors() {
	static const auto result = std::vector<quint32>{
		0xFF5B8DEFU,
		0xFF4CAF50U,
		0xFFFF9800U,
		0xFFE91E63U,
		0xFF9C27B0U,
		0xFF00ACC1U,
		0xFF795548U,
		0xFF607D8BU,
	};
	return result;
}

QString Agent::name(bool chinese) const {
	return (preset && !chinese) ? nameEn : nameZh;
}

QString Agent::description(bool chinese) const {
	return (preset && !chinese) ? descriptionEn : descriptionZh;
}

QString Agent::initial(bool chinese) const {
	return Initial(name(chinese));
}

const std::vector<Agent> &Presets() {
	static const auto result = std::vector<Agent>{
		Preset(
			"travel",
			"旅行规划师",
			"Travel planner",
			"按天安排行程、预算和交通",
			"Day-by-day itineraries, budgets and transport",
			0xFF00ACC1U,
			"You are a travel planner. Ask for missing essentials (dates, "
			"budget, interests) only when needed. Plan trips day by day "
			"with morning / afternoon / evening, rough costs, transport "
			"between places and practical tips. Keep it realistic and "
			"concise."),
		Preset(
			"copywriter",
			"文案写手",
			"Copywriter",
			"广告、社媒、产品文案",
			"Ads, social posts and product copy",
			0xFFFF9800U,
			"You are a copywriter. Write punchy, clear marketing copy: "
			"headlines, social posts, product descriptions and slogans. "
			"Offer two or three variants when useful, match the requested "
			"tone and platform, and keep claims honest."),
		Preset(
			"language",
			"语言老师",
			"Language teacher",
			"陪你练口语、改错、讲语法",
			"Practice conversation, corrections, grammar",
			0xFF4CAF50U,
			"You are a friendly language teacher. Practice conversation in "
			"the language the user is learning, keep sentences at their "
			"level, correct mistakes gently after each reply (show the "
			"corrected sentence and a short why), and explain grammar "
			"simply when asked."),
		Preset(
			"researcher",
			"研究员",
			"Researcher",
			"整理资料、对比观点、给出处",
			"Organize facts, compare views, cite sources",
			0xFF5B8DEFU,
			"You are a careful researcher. Answer with well-organized "
			"facts, compare viewpoints, separate what is established from "
			"what is uncertain, and say where information usually comes "
			"from. Never invent sources or numbers; say when you are not "
			"sure."),
		Preset(
			"coder",
			"编程助手",
			"Coding assistant",
			"写代码、找 bug、解释原理",
			"Write code, find bugs, explain concepts",
			0xFF607D8BU,
			"You are a senior software engineer. Give working, idiomatic "
			"code in fenced code blocks with the language name, explain "
			"briefly why it works, point out bugs and edge cases, and ask "
			"for the language or framework when it matters."),
	};
	return result;
}

Agent Custom(
		const QString &id,
		const QString &name,
		const QString &description,
		const QString &prompt,
		quint32 color) {
	auto result = Agent();
	result.id = id.isEmpty()
		? QUuid::createUuid().toString(QUuid::WithoutBraces)
		: id;
	result.preset = false;
	result.nameZh = result.nameEn = name;
	result.descriptionZh = result.descriptionEn = description;
	result.prompt = prompt;
	result.color = color;
	return result;
}

std::optional<Invalid> Validate(const QString &name, const QString &prompt) {
	const auto n = name.trimmed();
	const auto p = prompt.trimmed();
	if (n.isEmpty()) {
		return Invalid::EmptyName;
	} else if (n.size() > kMaxName) {
		return Invalid::NameTooLong;
	} else if (p.isEmpty()) {
		return Invalid::EmptyPrompt;
	} else if (p.size() > kMaxPrompt) {
		return Invalid::PromptTooLong;
	}
	return std::nullopt;
}

std::vector<Agent> ParseCustom(const QString &stored) {
	auto result = std::vector<Agent>();
	const auto document = QJsonDocument::fromJson(stored.toUtf8());
	for (const auto &value : document.array()) {
		if (!value.isObject()) {
			continue;
		}
		const auto o = value.toObject();
		const auto color = o.value(QStringLiteral("color"));
		result.push_back(Custom(
			o.value(QStringLiteral("id")).toString(),
			o.value(QStringLiteral("name")).toString(),
			o.value(QStringLiteral("desc")).toString(),
			o.value(QStringLiteral("prompt")).toString(),
			// Android stores the ARGB int (signed, e.g. -10777105).
			color.isDouble()
				? quint32(qint64(color.toDouble()))
				: Colors().front()));
	}
	return result;
}

QString CustomToString(const std::vector<Agent> &agents) {
	auto list = QJsonArray();
	for (const auto &agent : agents) {
		auto o = QJsonObject();
		o.insert(QStringLiteral("id"), agent.id);
		o.insert(QStringLiteral("name"), agent.nameZh);
		o.insert(QStringLiteral("desc"), agent.descriptionZh);
		o.insert(QStringLiteral("prompt"), agent.prompt);
		// Same as Android: the color as a signed 32-bit int.
		o.insert(QStringLiteral("color"), double(qint32(agent.color)));
		list.push_back(o);
	}
	return QString::fromUtf8(
		QJsonDocument(list).toJson(QJsonDocument::Compact));
}

std::vector<Agent> SaveCustom(std::vector<Agent> list, const Agent &agent) {
	auto replaced = false;
	for (auto &existing : list) {
		if (existing.id == agent.id) {
			existing = agent;
			replaced = true;
		}
	}
	if (!replaced) {
		list.push_back(agent);
	}
	return list;
}

std::vector<Agent> DeleteCustom(std::vector<Agent> list, const QString &id) {
	list.erase(std::remove_if(list.begin(), list.end(), [&](const Agent &a) {
		return a.id == id;
	}), list.end());
	return list;
}

QStringList ParsePinned(const QString &stored) {
	auto result = QStringList();
	const auto document = QJsonDocument::fromJson(stored.toUtf8());
	for (const auto &value : document.array()) {
		const auto id = value.toString();
		if (!id.isEmpty() && !result.contains(id)) {
			result.push_back(id);
		}
	}
	return result;
}

QString PinnedToString(const QStringList &pinned) {
	return QString::fromUtf8(QJsonDocument(
		QJsonArray::fromStringList(pinned)).toJson(QJsonDocument::Compact));
}

QStringList SetPinned(QStringList pinned, const QString &id, bool value) {
	if (value) {
		if (!pinned.contains(id)) {
			pinned.push_back(id);
		}
	} else {
		pinned.removeAll(id);
	}
	return pinned;
}

QString ConversationKey(const QString &agentId) {
	return QStringLiteral("agent:") + agentId;
}

QString MarketAgentId(const QString &marketId) {
	return QString::fromLatin1(kMarketPrefix) + marketId;
}

} // namespace Luckgram::AI::Agents
