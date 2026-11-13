/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/ai/ai_agents_logic.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// D7 需求 5 (mobile 22): AI agents. The 「智能体」 entry at the top of the
// assistant page (18 需求 8 extension point, registered in RegisterAll)
// opens the agents page; each agent is one assistant conversation (key
// "agent:<id>", task=agent, its instruction as the system message). The
// user's own agents and the pinned ones are per account in the encrypted
// account storage (luckgram_storage.h), same JSON as Android.
namespace Luckgram::AI::Agents {

// Switch ai_agents (and the assistant itself).
[[nodiscard]] bool Enabled();

// The user's agents and pins of one account.
class Mine final {
public:
	explicit Mine(not_null<Main::Session*> session);

	[[nodiscard]] static Mine &For(not_null<Main::Session*> session);

	[[nodiscard]] const std::vector<Agent> &custom();
	[[nodiscard]] std::optional<Agent> find(const QString &id);
	void save(const Agent &agent);
	// Also removes its conversation (22: "和它的对话都会删除").
	void remove(const QString &id);

	// 22 需求 5: agents added to the chat list, in pin order.
	[[nodiscard]] const QStringList &pinned();
	[[nodiscard]] bool isPinned(const QString &id);
	void setPinned(const QString &id, bool value);

	[[nodiscard]] rpl::producer<> changes() const;

private:
	void load();
	void write();

	const not_null<Main::Session*> _session;
	std::vector<Agent> _custom;
	QStringList _pinned;
	bool _loaded = false;
	rpl::event_stream<> _changes;

};

// Opens the agent's conversation in the assistant (created on first use).
void Open(
	not_null<Window::SessionController*> controller,
	const Agent &agent);

// Registers the 「智能体」 entry of the assistant page (RegisterAll).
void Register();

// 22 需求 5 on the desktop: the agents added to the chat list are entries
// of the left menu under 「Luckgram AI」 (one line in AddMainMenuEntry),
// shown while ai_agents is on; click opens the conversation.
void AddPinnedMenuEntries(
	not_null<Ui::VerticalLayout*> menu,
	not_null<Window::SessionController*> controller);

// Texts (mobile LuckgramAgents / LuckgramAgentEditActivity).
[[nodiscard]] QString TitleText();
[[nodiscard]] QString PopularText();
[[nodiscard]] QString MineText();
[[nodiscard]] QString CreateOwnText();

} // namespace Luckgram::AI::Agents
