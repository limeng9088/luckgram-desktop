/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <optional>
#include <vector>

// D7 (mobile 22): AI agents without UI. The five presets (same ids, texts,
// colors and instructions as Android / iOS), the user's own agents (stored
// per account as JSON, same format as Android) and the agents pinned to the
// chat list. Each agent is one assistant conversation (18 需求 8): key
// "agent:<id>", task=agent, the instruction as the system message. QtCore
// only (luckgram_tests).
namespace Luckgram::AI::Agents {

// 22 需求 4.
inline constexpr auto kMaxName = 30;
inline constexpr auto kMaxPrompt = 4000;

// Avatar colors offered when creating an agent (ARGB).
[[nodiscard]] const std::vector<quint32> &Colors();

// Id prefix of a market agent in these lists (pins); never clashes with
// presets or custom agents.
inline constexpr auto kMarketPrefix = "market:";

struct Agent {
	QString id;
	bool preset = false;
	QString nameZh;
	QString nameEn;
	QString descriptionZh;
	QString descriptionEn;
	quint32 color = 0xFF5B8DEFU;
	QString prompt; // Empty for market agents (the server injects it).

	[[nodiscard]] QString name(bool chinese) const;
	[[nodiscard]] QString description(bool chinese) const;
	// Round avatar letter: first character of the name, upper case.
	[[nodiscard]] QString initial(bool chinese) const;
};

// 22 需求 2: travel, copywriter, language, researcher, coder.
[[nodiscard]] const std::vector<Agent> &Presets();

// A custom agent with the user's texts (one language for both).
[[nodiscard]] Agent Custom(
	const QString &id,
	const QString &name,
	const QString &description,
	const QString &prompt,
	quint32 color);

// Why an agent can't be saved yet, or nothing: an empty name / prompt, or
// one over the limit (counted in characters as typed).
enum class Invalid {
	EmptyName,
	NameTooLong,
	EmptyPrompt,
	PromptTooLong,
};
[[nodiscard]] std::optional<Invalid> Validate(
	const QString &name,
	const QString &prompt);

// The stored list ("ai_agents_custom"): [{id,name,desc,prompt,color}].
[[nodiscard]] std::vector<Agent> ParseCustom(const QString &stored);
[[nodiscard]] QString CustomToString(const std::vector<Agent> &agents);
// Adds or replaces (same id) a custom agent.
[[nodiscard]] std::vector<Agent> SaveCustom(
	std::vector<Agent> list,
	const Agent &agent);
[[nodiscard]] std::vector<Agent> DeleteCustom(
	std::vector<Agent> list,
	const QString &id);

// 22 需求 5: ids pinned to the chat list ("ai_agents_pinned"), pin order.
[[nodiscard]] QStringList ParsePinned(const QString &stored);
[[nodiscard]] QString PinnedToString(const QStringList &pinned);
[[nodiscard]] QStringList SetPinned(
	QStringList pinned,
	const QString &id,
	bool value);

[[nodiscard]] QString ConversationKey(const QString &agentId);
[[nodiscard]] QString MarketAgentId(const QString &marketId);

} // namespace Luckgram::AI::Agents
