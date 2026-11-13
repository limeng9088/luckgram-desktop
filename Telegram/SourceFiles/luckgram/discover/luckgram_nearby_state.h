/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/discover/luckgram_nearby_logic.h"

#include <QtCore/QPointF>

#include <optional>

namespace Window {
class SessionController;
} // namespace Window

namespace Luckgram::Api {
struct Error;
} // namespace Luckgram::Api

// D10 (mobile 25, NearbyController): the local People Nearby state of this
// device, the "turns off in 1 hour" reminder and the cleanup on logout.
// Stored in the Luckgram settings with the mobile keys; a position picked on
// the map is kept only coarsened (~1 km).
namespace Luckgram::NearbyState {

[[nodiscard]] bool FeatureEnabled();

// True while the last successful PUT /v1/nearby/me has not expired.
[[nodiscard]] bool IsVisible();
[[nodiscard]] qint64 ExpiresAt();

[[nodiscard]] bool IntroDone();
void SetIntroDone();

[[nodiscard]] QString Bio();
void SetBio(const QString &bio);
[[nodiscard]] bool ShowPhoto();
void SetShowPhoto(bool show);

// Position picked on the map (lat, lon), coarsened; nullopt = system.
[[nodiscard]] std::optional<QPointF> ManualLocation();
void SetManualLocation(std::optional<QPointF> point);

// State from PUT / GET /v1/nearby/me.
void ApplyState(const Nearby::MyState &state);
void ClearLocal();
// Errors meaning the server no longer has (or won't keep) the card.
void OnError(const Api::Error &error);

// "在附近可见" off: DELETE /v1/nearby/me.
void TurnOff(Fn<void()> done, Fn<void(Api::Error)> fail);

// Logged out of every account (before the device token goes): the card is
// deleted on the server right away.
void OnLoggedOutCompletely();

// After login (D5 OnMainShown): the reminder 1 hour before the visibility
// ends while the app runs, or now if that time passed while it was closed.
void RestoreReminder(not_null<Window::SessionController*> controller);

// Local state changed (visible, bio, photo, location).
[[nodiscard]] rpl::producer<> Changes();

} // namespace Luckgram::NearbyState
