/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/pro/luckgram_pro.h"

#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "ui/basic_click_handlers.h"
#include "main/main_domain.h"
#include "window/window_controller.h"
#include "luckgram/luckgram_ai.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "settings/sections/settings_luckgram_pro.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"

#include <QtCore/QDateTime>

namespace Luckgram::Pro {
namespace {

constexpr auto kCacheKey = "_pro_state";

struct ServiceState {
	bool started = false;
	State state;
	Api::RequestPtr request;
	int64 lastRequestMs = 0;
	rpl::event_stream<> changes;
	rpl::lifetime lifetime;
};

[[nodiscard]] ServiceState &Service() {
	static auto result = ServiceState();
	return result;
}

[[nodiscard]] int64 NowMs() {
	return QDateTime::currentMSecsSinceEpoch();
}

[[nodiscard]] QString CacheKey() {
	return QString::fromLatin1(kCacheKey);
}

[[nodiscard]] State LoadCache() {
	return FromCache(Settings::GetString(CacheKey()).toUtf8());
}

// A member of the remote config's "values" (S4).
[[nodiscard]] QJsonValue RemoteValue(const char *key) {
	return Settings::RemoteConfig().value(
		u"values"_q
	).toObject().value(QString::fromLatin1(key));
}

[[nodiscard]] int RemoteInt(const char *key, int def) {
	const auto value = RemoteValue(key);
	return value.isDouble() ? int(value.toDouble()) : def;
}

void Update(State next) {
	auto &service = Service();
	const auto same = next.sameAs(service.state);
	service.state = std::move(next);
	Settings::SetString(
		CacheKey(),
		QString::fromUtf8(ToCache(service.state)));
	if (!same) {
		service.changes.fire({});
	}
}

} // namespace

void Start() {
	auto &service = Service();
	if (service.started) {
		return;
	}
	service.started = true;
	service.state = LoadCache();

	// Logged out of every account (Settings::ClearAll fires an empty key):
	// the cache is gone, so is Pro.
	Settings::Changes(
	) | rpl::filter([](const QString &key) {
		return key.isEmpty();
	}) | rpl::on_next([] {
		auto &service = Service();
		auto cached = LoadCache();
		if (!cached.sameAs(service.state)) {
			service.state = std::move(cached);
			service.changes.fire({});
		}
	}, service.lifetime);

	Core::App().appDeactivatedValue(
	) | rpl::filter([](bool deactivated) {
		return !deactivated;
	}) | rpl::on_next([] {
		RefreshIfStale();
	}, service.lifetime);

	Refresh();
}

const State &Current() {
	return Service().state;
}

bool IsPro() {
	return Current().isPro(NowMs());
}

rpl::producer<> Changes() {
	return Service().changes.events();
}

rpl::producer<bool> IsProValue() {
	return rpl::single(rpl::empty_value()) | rpl::then(
		Changes()
	) | rpl::map([] {
		return IsPro();
	}) | rpl::distinct_until_changed();
}

void Refresh() {
	auto &service = Service();
	if (service.request) {
		return;
	}
	service.lastRequestMs = NowMs();
	service.request = AI::Me([](QJsonObject me) {
		Service().request = nullptr;
		Update(ParseMe(me, NowMs()));
	}, [](Api::Error error) {
		// Keep the cached state (offline: Pro until the cached expiry).
		Service().request = nullptr;
		LOG(("Luckgram Pro: /v1/me failed, %1.").arg(error.code));
	});
}

void RefreshIfStale() {
	if (NowMs() - Service().lastRequestMs < kMinRefreshIntervalMs) {
		return;
	}
	Refresh();
}

bool BillingEnabled() {
	return RemoteValue(kConfigBillingEnabled).toBool(false);
}

Numbers ConfigNumbers() {
	auto result = Numbers();
	result.accountsMaxFree = RemoteInt(kConfigAccountsMaxFree, 0);
	result.chatPro = RemoteInt(kConfigChatPro, 0);
	result.chatFree = RemoteInt(kConfigChatFree, 0);
	result.monthlyCredits = RemoteInt(
		kConfigMonthlyCredits,
		kDefaultMonthlyCredits);
	result.summaryMaxPro = RemoteInt(kConfigSummaryMax, kSummaryMaxPro);
	result.translatePro = RemoteInt(kConfigTranslatePro, 0);
	result.translateFree = RemoteInt(kConfigTranslateFree, 0);
	return result;
}

std::vector<Row> LiveRows() {
	return Rows(
		LivePerks(Current().perks, RemoteValue(kConfigPerks)),
		ConfigNumbers(),
		IsChinese());
}

static_assert(::Main::Domain::kPremiumMaxAccounts == kAccountSlots);

int AccountsLimitNow() {
	return AccountsLimit(IsPro(), RemoteInt(kConfigAccountsMaxFree, 0));
}

rpl::producer<> AccountsLimitChanges() {
	return rpl::merge(
		Changes(),
		Settings::Changes() | rpl::filter([](const QString &key) {
			return key.isEmpty(); // Remote config merged, all cleared.
		}) | rpl::to_empty);
}

void FillAccountsLimitBox(not_null<Ui::GenericBox*> box) {
	const auto texts = Texts(IsChinese());
	const auto remoteMaxFree = RemoteInt(kConfigAccountsMaxFree, 0);
	const auto offerPro = OfferAccountsUpgrade(IsPro(), remoteMaxFree)
		&& BillingEnabled();
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(texts.accountsLimitReached(
			AccountsLimitNow(),
			OfferAccountsUpgrade(IsPro(), remoteMaxFree))),
		st::boxLabel));
	if (offerPro) {
		box->addButton(rpl::single(texts.viewPro()), [=] {
			box->closeBox();
			const auto window = Core::App().activePrimaryWindow();
			if (const auto controller = window
					? window->sessionController()
					: nullptr) {
				controller->showSettings(::Settings::LuckgramPro::Id());
			}
		});
	}
	box->addButton(
		offerPro
			? Luckgram::TrValue("取消", "Cancel")
			: Luckgram::TrValue("好", "OK"),
		[=] { box->closeBox(); });
}

void ShowUpgrade(
		not_null<Window::SessionController*> controller,
		Reason reason) {
	ShowUpgradeText(controller, Texts(IsChinese()).upgradeReason(reason));
}

void ShowUpgradeText(
		not_null<Window::SessionController*> controller,
		const QString &reason) {
	const auto texts = Texts(IsChinese());
	const auto weak = base::make_weak(controller);
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(texts.upgradeTitle()));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(reason),
			st::boxLabel));
		box->addButton(rpl::single(texts.upgradeButton()), [=] {
			box->closeBox();
			if (const auto strong = weak.get()) {
				strong->showSettings(::Settings::LuckgramPro::Id());
			}
		});
		box->addButton(Luckgram::TrValue("取消", "Cancel"), [=] {
			box->closeBox();
		});
	}));
}

bool HandleError(
		not_null<Window::SessionController*> controller,
		const Api::Error &error) {
	if (error.code == u"quota_exceeded"_q) {
		ShowUpgrade(controller, Reason::Quota);
	} else if (error.code == QString::fromLatin1(kErrorNotPro)) {
		ShowUpgrade(controller, Reason::ProOnly);
	} else if (error.code == QString::fromLatin1(kErrorCreditsExhausted)) {
		// Credits used up: back to the standard model, nothing to buy now.
		controller->showToast(error.text());
	} else {
		return false;
	}
	return true;
}

namespace {

// The Pro group's channel id, this run only: a peer id on disk would
// count as user content (docs/desktop.md), and the link works without it.
int64 CommunityChannelId = 0;

Api::RequestPtr CommunityRequest;

// 「你已经在 Pro 社群里」, then the group when we know which it is.
void OpenCommunity(
		not_null<Window::SessionController*> controller,
		const CommunityInvite &invite) {
	if (invite.channelId > 0) {
		CommunityChannelId = invite.channelId;
	}
	const auto channelId = CommunityChannelId;
	controller->showToast(Texts(IsChinese()).communityAlreadyMember());
	const auto channel = (channelId > 0)
		? controller->session().data().channelLoaded(
			ChannelId(BareId(channelId)))
		: nullptr;
	if (channel) {
		controller->showPeerHistory(channel);
	} else if (!invite.link.isEmpty()) {
		UrlClickHandler::Open(invite.link);
	}
}

} // namespace

bool ShowCommunityRowNow() {
	return ShowCommunityRow(
		Settings::IsEnabled(QString::fromLatin1(kCommunityFeatureKey)),
		Current().hasPerk(QString::fromLatin1(kPerkCommunity)));
}

void JoinCommunity(not_null<Window::SessionController*> controller) {
	const auto texts = Texts(IsChinese());
	if (CommunityRequest) {
		return;
	} else if (!IsPro()) {
		ShowUpgradeText(controller, texts.communityProOnly());
		return;
	}
	const auto weak = base::make_weak(controller);
	CommunityRequest = Api::Send({
		.method = "POST",
		.path = QString::fromLatin1(kCommunityInvitePath),
		.body = QByteArray("{}"),
		.contentType = "application/json",
	}, [=](Api::Response response) {
		CommunityRequest = nullptr;
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		const auto invite = ParseCommunityInvite(response.json());
		switch (invite.kind) {
		case CommunityInvite::Kind::Join:
			UrlClickHandler::Open(invite.link); // Telegram's join box.
			break;
		case CommunityInvite::Kind::AlreadyMember:
			OpenCommunity(strong, invite);
			break;
		case CommunityInvite::Kind::BadAnswer:
			strong->showToast(Api::ErrorText(
				QString::fromLatin1(Api::kUpstreamUnavailable)));
			break;
		}
	}, [=](Api::Error error) {
		CommunityRequest = nullptr;
		const auto strong = weak.get();
		if (!strong || error.code == QString::fromLatin1(Api::kCancelled)) {
			return;
		} else if (error.code == QString::fromLatin1(kErrorNotPro)) {
			Refresh();
			ShowUpgradeText(strong, texts.communityProOnly());
		} else if (error.code == QString::fromLatin1(kErrorAlreadyMember)) {
			OpenCommunity(strong, CommunityAlreadyMember(QJsonObject()));
		} else {
			strong->showToast(error.text());
		}
	});
}

} // namespace Luckgram::Pro
