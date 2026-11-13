/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/ai/ai_market_logic.h"
#include "luckgram/luckgram_api.h"

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

// D7 需求 5 (mobile 30, server S13): the agent market. Lists, categories
// and details come from the server (the system instruction never does: the
// server injects it by agent_id); the cache of lists / details and the
// "used" list (「我的」) are per account in the encrypted account storage.
// Pro-only agents: free users see the details, 「开始聊天」 opens the
// upgrade box (the server checks too: not_pro -> Pro::HandleError).
namespace Luckgram::AI::Market {

// Switch ai_agent_market (and the assistant).
[[nodiscard]] bool Enabled();

class Service final {
public:
	explicit Service(not_null<Main::Session*> session);

	[[nodiscard]] static Service &For(not_null<Main::Session*> session);

	// GET /v1/agents/categories; done with the categories (from the cache
	// when offline, then fail is not called).
	Api::RequestPtr categories(
		Fn<void(std::vector<Category>)> done,
		Fn<void(Api::Error)> fail);

	struct Loaded {
		Page page;
		bool offline = false; // From the cache after a failed request.
	};
	// GET /v1/agents (first page cached per category; search not cached:
	// offline it filters the cached agents).
	Api::RequestPtr list(
		const QString &category,
		const QString &query,
		const QString &cursor,
		Fn<void(Loaded)> done,
		Fn<void(Api::Error)> fail);

	// GET /v1/agents/{id}; the cached detail when offline.
	Api::RequestPtr detail(
		const QString &id,
		Fn<void(Agent)> done,
		Fn<void(Api::Error)> fail);

	// 30 需求 6: used market agents, most recent first.
	[[nodiscard]] const std::vector<Agent> &used();
	[[nodiscard]] std::optional<Agent> findUsed(const QString &id);
	void markUsed(const Agent &agent);
	void removeUsed(const QString &id);
	[[nodiscard]] rpl::producer<> usedChanges() const;

private:
	void loadCache();
	void writeCache();
	void loadUsed();
	void writeUsed();

	const not_null<Main::Session*> _session;
	std::unique_ptr<Cache> _cache;
	bool _cacheLoaded = false;
	std::vector<Agent> _used;
	// Removed while the storage wasn't readable: not back on load.
	base::flat_set<QString> _removedUnsaved;
	bool _usedLoaded = false;
	rpl::event_stream<> _usedChanges;
	rpl::lifetime _lifetime;

};

// 「开始聊天」: the upgrade box for a Pro agent and a free user, else the
// agent's conversation (key market:<id>, task=agent, agent_id param), and
// it goes first in 「我的」. inputText goes into the field (an example).
void Open(
	not_null<Window::SessionController*> controller,
	const Agent &agent,
	const QString &inputText = QString());

// Texts (mobile LuckgramAgentMarket*).
[[nodiscard]] QString MarketText();
[[nodiscard]] QString ProOnlyText();

} // namespace Luckgram::AI::Market
