/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_service.h"

#include "base/timer.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/pro/luckgram_pro.h"
#include "luckgram/xray/luckgram_xray.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonObject>

namespace Luckgram {
namespace {

// D2 §7: at launch and every 6 hours (S4 App side).
constexpr auto kConfigRefreshSeconds = 6 * 60 * 60;
// After a failure: try again in 5 minutes (mobile RETRY_AFTER_FAILURE).
constexpr auto kRetryAfterFailureSeconds = 5 * 60;
// How often the timer looks whether a refresh is due.
constexpr auto kCheckInterval = crl::time(10 * 60 * 1000);

struct ServiceState {
	base::Timer timer;
	Api::RequestPtr configRequest;
	int64 lastFailure = 0;
	bool started = false;
};

// Never destroyed: the timer must not outlive QApplication in a static.
[[nodiscard]] ServiceState &State() {
	static const auto result = new ServiceState();
	return *result;
}

[[nodiscard]] bool ConfigStale() {
	const auto now = QDateTime::currentSecsSinceEpoch();
	const auto &state = State();
	if (state.lastFailure
		&& now >= state.lastFailure
		&& now - state.lastFailure < kRetryAfterFailureSeconds) {
		return false;
	}
	const auto at = Settings::RemoteConfigFetchedAt();
	return !at || now < at || now - at >= kConfigRefreshSeconds;
}

void Check() {
	if (ConfigStale()) {
		RefreshRemoteConfig();
	}
}

} // namespace

void Start() {
	auto &state = State();
	if (state.started) {
		return;
	}
	state.started = true;
	Api::EnsureRegistered();
	RefreshRemoteConfig(); // At every launch.
	Pro::Start(); // D12: Pro state from /v1/me.
	Xray::Start(); // D12: the built-in proxy core, when there are nodes.
	state.timer.setCallback([] { Check(); });
	state.timer.callEach(kCheckInterval);
}

void OnLoggedOutCompletely() {
	if (const auto request = base::take(State().configRequest)) {
		request->cancel();
	}
	Settings::ClearAll();
	Api::ForgetToken();
	LOG(("Luckgram: logged out of every account, local data cleared."));
}

void RefreshRemoteConfig(Fn<void(bool)> done) {
	auto &state = State();
	if (state.configRequest) {
		state.configRequest->cancel();
	}
	auto headers = std::vector<std::pair<QByteArray, QByteArray>>();
	const auto version = Settings::RemoteConfig().value(
		u"version"_q).toString();
	if (!version.isEmpty()) {
		headers.emplace_back("If-None-Match", '"' + version.toUtf8() + '"');
	}
	state.configRequest = Api::Send({
		.path = u"/v1/config"_q,
		.headers = std::move(headers),
	}, [=](Api::Response response) {
		State().configRequest = nullptr;
		State().lastFailure = 0;
		const auto now = QDateTime::currentSecsSinceEpoch();
		const auto json = response.json();
		const auto changed = (response.status != 304)
			&& (json.contains(u"values"_q) || json.contains(u"features"_q))
			&& (json != Settings::RemoteConfig());
		Settings::SetRemoteConfig(
			changed ? json : Settings::RemoteConfig(),
			now);
		if (done) {
			done(changed);
		}
	}, [=](Api::Error error) {
		State().configRequest = nullptr;
		if (error.status == 304) {
			Settings::SetRemoteConfig(
				Settings::RemoteConfig(),
				QDateTime::currentSecsSinceEpoch());
		} else {
			State().lastFailure = QDateTime::currentSecsSinceEpoch();
			LOG(("Luckgram: GET /v1/config failed, %1").arg(error.code));
		}
		if (done) {
			done(false);
		}
	});
}

} // namespace Luckgram
