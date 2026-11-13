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
// Desktop note: upstream (non-store) Telegram Desktop obeyed only "all"
// entries. Like the mobile apps, Luckgram Desktop now also obeys ios /
// android entries while the switch is off (the default), and skips them
// when it is on.
namespace Luckgram {

inline constexpr auto kStoreRestrictionsKey = "store_restrictions_off";

[[nodiscard]] bool StoreRestrictionsOff();

// Before the switch goes on: the mobile explanation, 「打开」 / 「取消」.
void ConfirmStoreRestrictionsOff(
	std::shared_ptr<Ui::Show> show,
	Fn<void(bool)> done);

} // namespace Luckgram
