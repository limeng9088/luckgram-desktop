/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_store_restrictions.h"

#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/show.h"

namespace Luckgram {

bool StoreRestrictionsOff() {
	return Settings::IsEnabled(QString::fromLatin1(kStoreRestrictionsKey));
}

void ConfirmStoreRestrictionsOff(
		std::shared_ptr<Ui::Show> show,
		Fn<void(bool)> done) {
	const auto answered = std::make_shared<bool>(false);
	auto box = Ui::MakeConfirmBox({
		.text = Tr(
			"打开后，仅在商店版 Telegram 中被隐藏的频道和群组可以查看。Telegram 对所有平台封禁的内容，以及你所在地区依法屏蔽的内容，仍然无法查看。",
			"When on, channels and groups that are hidden only in the App Store / Google Play versions of Telegram can be viewed. Content Telegram blocks on all platforms, and content blocked by law in your region, stays unavailable."),
		.confirmed = [=](Fn<void()> close) {
			*answered = true;
			done(true);
			close();
		},
		.cancelled = [=](Fn<void()> close) {
			*answered = true;
			done(false);
			close();
		},
		.confirmText = Tr("打开", "Turn On"),
		.cancelText = Tr("取消", "Cancel"),
		.title = Tr("受限频道", "Store-restricted channels"),
	});
	box->boxClosing() | rpl::on_next([=] {
		if (!*answered) {
			*answered = true;
			done(false);
		}
	}, box->lifetime());
	show->showBox(std::move(box));
}

} // namespace Luckgram
