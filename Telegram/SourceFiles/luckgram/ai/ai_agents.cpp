/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_agents.h"

#include "luckgram/ai/ai_assistant.h"
#include "luckgram/ai/ai_market.h"
#include "luckgram/ai/ai_registry.h"
#include "luckgram/ai/ai_store.h"
#include "luckgram/luckgram_double_bottom.h"
#include "luckgram/luckgram_features.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_storage.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "settings/sections/settings_luckgram_agents.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

namespace Luckgram::AI::Agents {
namespace {

// Account storage names (same keys as the Android preferences).
constexpr auto kCustomName = "ai_agents_custom";
constexpr auto kPinnedName = "ai_agents_pinned";

[[nodiscard]] base::flat_map<
	not_null<Main::Session*>,
	std::unique_ptr<Mine>> &All() {
	static auto result = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<Mine>>();
	return result;
}

constexpr auto kTaskAgent = "agent";

} // namespace

bool Enabled() {
	return Settings::IsEnabled(QString::fromLatin1(Features::kAiAgents))
		&& AssistantEnabled();
}

Mine::Mine(not_null<Main::Session*> session) : _session(session) {
	// D8 second space: the account's storage goes away / comes back; read
	// again then (and drop what belonged to the hidden state).
	DoubleBottom::ActiveChanges(
	) | rpl::on_next([=] {
		_loaded = false;
		_custom.clear();
		_pinned.clear();
		_removedUnsaved.clear();
		_unpinnedUnsaved.clear();
		_changes.fire({});
	}, _lifetime);
}

Mine &Mine::For(not_null<Main::Session*> session) {
	auto &all = All();
	const auto i = all.find(session);
	if (i != end(all)) {
		return *i->second;
	}
	const auto result = all.emplace(
		session,
		std::make_unique<Mine>(session)).first->second.get();
	session->lifetime().add([=] {
		All().remove(session);
	});
	return *result;
}

void Mine::load() {
	if (_loaded || !Storage::AccountAvailable(_session)) {
		return; // Memory only until the account storage is there.
	}
	_loaded = true;
	const auto unsaved = !_custom.empty()
		|| !_pinned.isEmpty()
		|| !_removedUnsaved.empty()
		|| !_unpinnedUnsaved.empty();
	auto stored = ParseCustom(QString::fromUtf8(
		Storage::ReadAccount(_session, QString::fromLatin1(kCustomName))));
	for (const auto &id : _removedUnsaved) {
		stored = DeleteCustom(std::move(stored), id);
	}
	// Agents made while the storage wasn't there win (same id).
	for (const auto &agent : _custom) {
		stored = SaveCustom(std::move(stored), agent);
	}
	_custom = std::move(stored);
	auto pinned = ParsePinned(QString::fromUtf8(
		Storage::ReadAccount(_session, QString::fromLatin1(kPinnedName))));
	for (const auto &id : _unpinnedUnsaved) {
		pinned = SetPinned(std::move(pinned), id, false);
	}
	for (const auto &id : _pinned) {
		pinned = SetPinned(std::move(pinned), id, true);
	}
	_pinned = std::move(pinned);
	_removedUnsaved.clear();
	_unpinnedUnsaved.clear();
	if (unsaved) {
		write();
	}
}

void Mine::write() {
	load();
	if (!_loaded) {
		return;
	}
	Storage::WriteAccount(
		_session,
		QString::fromLatin1(kCustomName),
		CustomToString(_custom).toUtf8());
	Storage::WriteAccount(
		_session,
		QString::fromLatin1(kPinnedName),
		PinnedToString(_pinned).toUtf8());
}

const std::vector<Agent> &Mine::custom() {
	load();
	return _custom;
}

std::optional<Agent> Mine::find(const QString &id) {
	for (const auto &agent : Presets()) {
		if (agent.id == id) {
			return agent;
		}
	}
	for (const auto &agent : custom()) {
		if (agent.id == id) {
			return agent;
		}
	}
	return std::nullopt;
}

void Mine::save(const Agent &agent) {
	load();
	_removedUnsaved.remove(agent.id);
	_custom = SaveCustom(std::move(_custom), agent);
	write();
	_changes.fire({});
}

void Mine::remove(const QString &id) {
	load();
	if (!_loaded) {
		_removedUnsaved.emplace(id);
		_unpinnedUnsaved.emplace(id);
	}
	_custom = DeleteCustom(std::move(_custom), id);
	_pinned = SetPinned(std::move(_pinned), id, false);
	write();
	auto &store = Store::For(_session);
	if (const auto conversation = store.findByKey(ConversationKey(id))) {
		store.remove(conversation->id);
	}
	_changes.fire({});
}

const QStringList &Mine::pinned() {
	load();
	return _pinned;
}

bool Mine::isPinned(const QString &id) {
	return pinned().contains(id);
}

void Mine::setPinned(const QString &id, bool value) {
	load();
	if (!_loaded) {
		if (value) {
			_unpinnedUnsaved.remove(id);
		} else {
			_unpinnedUnsaved.emplace(id);
		}
	}
	_pinned = SetPinned(std::move(_pinned), id, value);
	write();
	_changes.fire({});
}

rpl::producer<> Mine::changes() const {
	return _changes.events();
}

void Open(
		not_null<Window::SessionController*> controller,
		const Agent &agent) {
	const auto chinese = IsChinese();
	const auto conversation = Store::For(&controller->session()).findOrCreate(
		ConversationKey(agent.id),
		QString::fromLatin1(kTaskAgent),
		agent.prompt.isEmpty()
			? std::nullopt
			: std::make_optional(agent.prompt),
		agent.name(chinese));
	OpenAssistant(controller, conversation.id);
}

void Register() {
	RegisterTopEntry({
		.id = u"agents"_q,
		.titleZh = "智能体",
		.titleEn = "Agents",
		.open = [](not_null<Window::SessionController*> controller) {
			controller->showSettings(::Settings::LuckgramAgents::Id());
		},
		.visible = [] { return Enabled(); },
		.order = 0,
	});
}

void AddPinnedMenuEntries(
		not_null<Ui::VerticalLayout*> menu,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	const auto wrap = menu->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			menu,
			object_ptr<Ui::VerticalLayout>(menu)));
	const auto inner = wrap->entity();
	const auto rebuild = [=] {
		inner->clear();
		auto &mine = Mine::For(session);
		const auto marketPrefix = QString::fromLatin1(kMarketPrefix);
		for (const auto &id : mine.pinned()) {
			if (id.startsWith(marketPrefix)) {
				// 30 需求 6: a market agent from 「我的」.
				const auto used = Market::Service::For(session).findUsed(
					id.mid(marketPrefix.size()));
				if (!used || !Market::Enabled()) {
					continue;
				}
				const auto button = inner->add(::Settings::CreateButtonWithIcon(
					inner,
					rpl::single(used->name.pick(IsChinese())),
					st::mainMenuButton,
					{ &st::menuIconBot }));
				const auto copy = *used;
				button->setClickedCallback([=] {
					Market::Open(controller, copy);
				});
				continue;
			}
			const auto agent = mine.find(id);
			if (!agent) {
				continue; // Deleted.
			}
			const auto button = inner->add(::Settings::CreateButtonWithIcon(
				inner,
				rpl::single(agent->name(IsChinese())),
				st::mainMenuButton,
				{ &st::menuIconBot }));
			const auto copy = *agent;
			button->setClickedCallback([=] {
				Open(controller, copy);
			});
		}
		inner->resizeToWidth(menu->width());
	};
	rpl::merge(
		Mine::For(session).changes(),
		Market::Service::For(session).usedChanges()
	) | rpl::on_next(rebuild, wrap->lifetime());
	rebuild();
	wrap->toggleOn(rpl::single(QString()) | rpl::then(
		Settings::Changes()
	) | rpl::map([] {
		return Enabled();
	}));
	wrap->finishAnimating();
}

QString TitleText() {
	return Tr("智能体", "Agents");
}

QString PopularText() {
	return Tr("热门智能体", "Popular agents");
}

QString MineText() {
	return Tr("我的智能体", "My agents");
}

QString CreateOwnText() {
	return Tr("创建你自己的智能体", "Create your own agent");
}

} // namespace Luckgram::AI::Agents
