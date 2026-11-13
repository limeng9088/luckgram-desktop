/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/discover/luckgram_nearby_state.h"

#include "base/timer.h"
#include "core/application.h"
#include "luckgram/discover/luckgram_silent_link.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "settings/sections/settings_luckgram_discover.h"
#include "ui/boxes/confirm_box.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QDateTime>

namespace Luckgram::NearbyState {
namespace {

constexpr auto kExpiresKey = "nearby.expires_at";
constexpr auto kIntroKey = "nearby.intro_done";
constexpr auto kBioKey = "nearby.bio";
constexpr auto kPhotoKey = "nearby.show_photo";
constexpr auto kRemindedKey = "nearby.reminded_for";
constexpr auto kManualLatKey = "nearby.manual_lat";
constexpr auto kManualLonKey = "nearby.manual_lon";

struct Shared {
	rpl::event_stream<> changes;
	std::unique_ptr<base::Timer> reminder;
};

[[nodiscard]] Shared &Data() {
	static auto result = Shared();
	return result;
}

[[nodiscard]] QString K(const char *key) {
	return QString::fromLatin1(key);
}

[[nodiscard]] qint64 NowMs() {
	return QDateTime::currentMSecsSinceEpoch();
}

void Changed() {
	Data().changes.fire({});
}

void ShowReminder() {
	const auto expires = ExpiresAt();
	if (!FeatureEnabled() || !IsVisible()) {
		return;
	}
	const auto window = Core::App().activePrimaryWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	if (!controller) {
		return;
	}
	Settings::SetInt(K(kRemindedKey), expires);
	const auto texts = Nearby::Texts(IsChinese());
	const auto weak = base::make_weak(controller);
	controller->show(Ui::MakeConfirmBox({
		.text = texts.reminderText(),
		.confirmed = [=](Fn<void()> close) {
			close();
			if (const auto strong = weak.get()) {
				strong->showSettings(::Settings::LuckgramNearby::Id());
			}
		},
		.confirmText = texts.renew(),
		.title = texts.reminderTitle(),
	}));
}

void ScheduleReminder() {
	auto &data = Data();
	if (!data.reminder) {
		data.reminder = std::make_unique<base::Timer>([] { ShowReminder(); });
	}
	data.reminder->cancel();
	const auto at = Nearby::ReminderAt(ExpiresAt(), NowMs());
	if (at > 0 && FeatureEnabled()) {
		data.reminder->callOnce(at - NowMs());
	}
}

} // namespace

bool FeatureEnabled() {
	return Settings::IsEnabled(K(Nearby::kFeatureKey));
}

qint64 ExpiresAt() {
	return Settings::GetInt(K(kExpiresKey), 0);
}

bool IsVisible() {
	const auto expires = ExpiresAt();
	return (expires > 0) && !Nearby::IsExpired(expires, NowMs());
}

bool IntroDone() {
	return Settings::GetBool(K(kIntroKey), false);
}

void SetIntroDone() {
	Settings::SetBool(K(kIntroKey), true);
	Changed();
}

QString Bio() {
	return Settings::GetString(K(kBioKey));
}

void SetBio(const QString &bio) {
	Settings::SetString(K(kBioKey), Nearby::CleanBio(bio));
	Changed();
}

bool ShowPhoto() {
	return Settings::GetBool(K(kPhotoKey), true);
}

void SetShowPhoto(bool show) {
	Settings::SetBool(K(kPhotoKey), show);
	Changed();
}

std::optional<QPointF> ManualLocation() {
	if (!Settings::Has(K(kManualLatKey)) || !Settings::Has(K(kManualLonKey))) {
		return std::nullopt;
	}
	auto okLat = false;
	auto okLon = false;
	const auto lat = Settings::GetString(K(kManualLatKey)).toDouble(&okLat);
	const auto lon = Settings::GetString(K(kManualLonKey)).toDouble(&okLon);
	if (!okLat || !okLon || !Nearby::IsValidLocation(lat, lon)) {
		return std::nullopt;
	}
	return QPointF(lat, lon);
}

void SetManualLocation(std::optional<QPointF> point) {
	if (point && Nearby::IsValidLocation(point->x(), point->y())) {
		// Only the coarsened value is ever stored.
		Settings::SetString(K(kManualLatKey), Nearby::Coord(point->x()));
		Settings::SetString(K(kManualLonKey), Nearby::Coord(point->y()));
	} else {
		Settings::Remove(K(kManualLatKey));
		Settings::Remove(K(kManualLonKey));
	}
	Changed();
}

void ApplyState(const Nearby::MyState &state) {
	if (state.visible && state.expiresAtMs > NowMs()) {
		Settings::SetInt(K(kExpiresKey), state.expiresAtMs);
	} else {
		Settings::Remove(K(kExpiresKey));
	}
	ScheduleReminder();
	Changed();
}

void ClearLocal() {
	Settings::Remove(K(kExpiresKey));
	ScheduleReminder();
	Changed();
}

void OnError(const Api::Error &error) {
	if (error.code == u"not_linked"_q || error.code == u"banned"_q) {
		ClearLocal();
	}
	if (error.code == u"not_linked"_q) {
		SilentLink::ClearCache();
	}
}

void TurnOff(Fn<void()> done, Fn<void(Api::Error)> fail) {
	ClearLocal();
	Api::Send({
		.method = "DELETE",
		.path = u"/v1/nearby/me"_q,
	}, [=](Api::Response) {
		if (done) {
			done();
		}
	}, [=](Api::Error error) {
		if (fail) {
			fail(error);
		}
	});
}

void OnLoggedOutCompletely() {
	SilentLink::ClearCache();
	if (!IsVisible() || !Api::HasToken()) {
		return;
	}
	// Sent now, with the token that is about to be forgotten.
	Api::Send({
		.method = "DELETE",
		.path = u"/v1/nearby/me"_q,
	}, nullptr, nullptr);
}

void RestoreReminder(not_null<Window::SessionController*> controller) {
	if (!FeatureEnabled() || !IsVisible()) {
		return;
	}
	ScheduleReminder();
	const auto expires = ExpiresAt();
	if (Nearby::MissedReminder(
			expires,
			NowMs(),
			Settings::GetInt(K(kRemindedKey), 0))) {
		ShowReminder();
	}
}

rpl::producer<> Changes() {
	return Data().changes.events();
}

} // namespace Luckgram::NearbyState
