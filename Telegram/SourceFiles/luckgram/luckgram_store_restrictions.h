/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Ui {
class Show;
} // namespace Ui

// D8 (mobile 13 受限频道): store_restrictions_off. Data::UnavailableReason
// keeps each entry's platform and Compute() asks Privacy::Applies() with
// this switch, so turning it on / off takes effect the next time a chat or
// message is checked. Entries for all platforms and everything the server
// refuses by region stay in force.
//
// Desktop note (w-1109, 2026-11-13): the desktop app doesn't go through an
// app store, so it is never stricter than upstream Telegram Desktop, which
// obeys only "all" entries. The switch is therefore on by default (same
// result as upstream); only a user who turns it off also gets the ios /
// android entries, like the store versions of the mobile apps.
namespace Luckgram {

inline constexpr auto kStoreRestrictionsKey = "store_restrictions_off";

[[nodiscard]] bool StoreRestrictionsOff();

// Before the switch goes on: the mobile explanation, 「打开」 / 「取消」.
void ConfirmStoreRestrictionsOff(
	std::shared_ptr<Ui::Show> show,
	Fn<void(bool)> done);

} // namespace Luckgram
