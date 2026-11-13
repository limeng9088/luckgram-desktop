/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_market.h"

#include "luckgram/ai/ai_assistant.h"
#include "luckgram/ai/ai_store.h"
#include "luckgram/luckgram_double_bottom.h"
#include "luckgram/luckgram_features.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_storage.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/pro/luckgram_pro.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

namespace Luckgram::AI::Market {
namespace {

// Account storage names (same keys as the Android preferences).
constexpr auto kCacheName = "ai_market_cache";
constexpr auto kUsedName = "ai_market_used";
constexpr auto kTaskAgent = "agent";

[[nodiscard]] base::flat_map<
	not_null<Main::Session*>,
	std::unique_ptr<Service>> &All() {
	static auto result = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<Service>>();
	return result;
}

} // namespace

bool Enabled() {
	return Settings::IsEnabled(QString::fromLatin1(Features::kAiAgentMarket))
		&& AssistantEnabled();
}

Service::Service(not_null<Main::Session*> session)
: _session(session)
, _cache(std::make_unique<Cache>()) {
	// D8 second space: read the account's storage again.
	DoubleBottom::ActiveChanges(
	) | rpl::on_next([=] {
		_cacheLoaded = _usedLoaded = false;
		*_cache = Cache();
		_used.clear();
		_removedUnsaved.clear();
		_usedChanges.fire({});
	}, _lifetime);
}

Service &Service::For(not_null<Main::Session*> session) {
	auto &all = All();
	const auto i = all.find(session);
	if (i != end(all)) {
		return *i->second;
	}
	const auto result = all.emplace(
		session,
		std::make_unique<Service>(session)).first->second.get();
	session->lifetime().add([=] {
		All().remove(session);
	});
	return *result;
}

void Service::loadCache() {
	if (_cacheLoaded || !Storage::AccountAvailable(_session)) {
		return;
	}
	_cacheLoaded = true;
	auto stored = Cache(QString::fromUtf8(
		Storage::ReadAccount(_session, QString::fromLatin1(kCacheName))));
	// Nothing fetched while the storage wasn't there is worth merging: a
	// cache only, the next request refreshes it.
	if (_cache->allAgents().empty()) {
		*_cache = std::move(stored);
	}
}

void Service::writeCache() {
	loadCache();
	if (_cacheLoaded) {
		Storage::WriteAccount(
			_session,
			QString::fromLatin1(kCacheName),
			_cache->serialize().toUtf8());
	}
}

void Service::loadUsed() {
	if (_usedLoaded || !Storage::AccountAvailable(_session)) {
		return;
	}
	_usedLoaded = true;
	const auto unsaved = !_used.empty() || !_removedUnsaved.empty();
	auto stored = ParseUsed(QString::fromUtf8(
		Storage::ReadAccount(_session, QString::fromLatin1(kUsedName))));
	for (const auto &id : _removedUnsaved) {
		stored = RemoveUsed(stored, id);
	}
	// Used while the storage wasn't there: those go first.
	for (auto i = _used.rbegin(); i != _used.rend(); ++i) {
		stored = MarkUsed(stored, *i);
	}
	_used = std::move(stored);
	_removedUnsaved.clear();
	if (unsaved) {
		writeUsed();
	}
}

void Service::writeUsed() {
	loadUsed();
	if (_usedLoaded) {
		Storage::WriteAccount(
			_session,
			QString::fromLatin1(kUsedName),
			UsedToString(_used).toUtf8());
	}
}

Api::RequestPtr Service::categories(
		Fn<void(std::vector<Category>)> done,
		Fn<void(Api::Error)> fail) {
	loadCache();
	const auto weak = base::make_weak(_session);
	return Api::Send({
		.path = QString::fromLatin1(kCategoriesPath),
	}, [=](Api::Response response) {
		if (!weak) {
			return;
		}
		const auto json = response.json();
		_cache->putCategories(json);
		writeCache();
		done(ParseCategories(json));
	}, [=](Api::Error error) {
		if (!weak) {
			return;
		}
		const auto cached = _cache->categories();
		if (!cached.empty()
			&& error.code != QString::fromLatin1(Api::kCancelled)) {
			done(cached);
		} else {
			fail(error);
		}
	});
}

Api::RequestPtr Service::list(
		const QString &category,
		const QString &query,
		const QString &cursor,
		Fn<void(Loaded)> done,
		Fn<void(Api::Error)> fail) {
	loadCache();
	const auto weak = base::make_weak(_session);
	const auto search = !query.trimmed().isEmpty();
	return Api::Send({
		.path = ListPath(category, query, cursor, kPageSize),
	}, [=](Api::Response response) {
		if (!weak) {
			return;
		}
		const auto json = response.json();
		if (!search && cursor.isEmpty()) {
			_cache->putFirstPage(category, json);
			writeCache();
		}
		auto page = ParsePage(json);
		// 「我的」 keeps fresh names / descriptions / Pro marks.
		auto changed = false;
		for (const auto &agent : page.items) {
			if (findUsed(agent.id)) {
				_used = RefreshUsed(_used, agent);
				changed = true;
			}
		}
		if (changed) {
			writeUsed();
			_usedChanges.fire({});
		}
		done({ .page = std::move(page) });
	}, [=](Api::Error error) {
		if (!weak) {
			return;
		}
		if (error.code == QString::fromLatin1(Api::kCancelled)
			|| !cursor.isEmpty()) {
			fail(error);
			return;
		}
		// 30 需求 7: offline, the cached agents (search filters them).
		auto page = Page();
		if (!search) {
			if (const auto cached = _cache->firstPage(category)) {
				page = *cached;
				page.nextCursor = QString();
			}
		}
		if (page.items.empty()) {
			page.items = _cache->offline(category, query);
		}
		if (page.items.empty()) {
			fail(error);
		} else {
			done({ .page = std::move(page), .offline = true });
		}
	});
}

Api::RequestPtr Service::detail(
		const QString &id,
		Fn<void(Agent)> done,
		Fn<void(Api::Error)> fail) {
	loadCache();
	const auto weak = base::make_weak(_session);
	return Api::Send({
		.path = DetailPath(id),
	}, [=](Api::Response response) {
		if (!weak) {
			return;
		}
		if (const auto agent = ParseAgent(response.json())) {
			_cache->putDetail(*agent);
			writeCache();
			done(*agent);
		} else {
			fail({ .code = QString::fromLatin1(Api::kUpstreamUnavailable) });
		}
	}, [=](Api::Error error) {
		if (!weak) {
			return;
		}
		const auto cached = _cache->detail(id);
		if (cached && error.code != QString::fromLatin1(Api::kCancelled)) {
			done(*cached);
		} else {
			fail(error);
		}
	});
}

const std::vector<Agent> &Service::used() {
	loadUsed();
	return _used;
}

std::optional<Agent> Service::findUsed(const QString &id) {
	for (const auto &agent : used()) {
		if (agent.id == id) {
			return agent;
		}
	}
	return std::nullopt;
}

void Service::markUsed(const Agent &agent) {
	loadUsed();
	_removedUnsaved.remove(agent.id);
	_used = MarkUsed(_used, agent);
	writeUsed();
	_usedChanges.fire({});
}

void Service::removeUsed(const QString &id) {
	loadUsed();
	if (!_usedLoaded) {
		_removedUnsaved.emplace(id);
	}
	_used = RemoveUsed(_used, id);
	writeUsed();
	_usedChanges.fire({});
}

rpl::producer<> Service::usedChanges() const {
	return _usedChanges.events();
}

void Open(
		not_null<Window::SessionController*> controller,
		const Agent &agent,
		const QString &inputText) {
	// 30 需求 5: a free user sees a Pro agent but has to upgrade to chat.
	if (NeedsUpgrade(agent, Pro::IsPro())) {
		Pro::ShowUpgradeText(controller, ProOnlyText());
		return;
	}
	const auto session = &controller->session();
	const auto conversation = Store::For(session).findOrCreate(
		ConversationKey(agent.id),
		QString::fromLatin1(kTaskAgent),
		std::nullopt, // Never on the client: the server injects it (S13).
		agent.name.pick(IsChinese()),
		ChatParams(agent.id));
	Service::For(session).markUsed(agent);
	OpenAssistant(controller, conversation.id, inputText);
}

QString MarketText() {
	return Tr("市场", "Market");
}

QString ProOnlyText() {
	return Tr(
		"Luckgram Pro 专享，升级后即可聊天。",
		"Available with Luckgram Pro.");
}

} // namespace Luckgram::AI::Market
