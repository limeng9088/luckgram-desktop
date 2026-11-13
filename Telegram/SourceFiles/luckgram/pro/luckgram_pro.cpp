/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/pro/luckgram_pro.h"

#include "core/application.h"
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

void ShowUpgrade(
		not_null<Window::SessionController*> controller,
		Reason reason) {
	const auto texts = Texts(IsChinese());
	const auto weak = base::make_weak(controller);
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(texts.upgradeTitle()));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(texts.upgradeReason(reason)),
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

} // namespace Luckgram::Pro
