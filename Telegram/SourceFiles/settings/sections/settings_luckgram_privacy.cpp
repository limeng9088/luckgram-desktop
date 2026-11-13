/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "settings/sections/settings_luckgram_privacy.h"

#include "core/application.h"
#include "data/data_user.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "luckgram/luckgram_double_bottom.h"
#include "luckgram/luckgram_hidden_chats.h"
#include "luckgram/luckgram_privacy_logic.h"
#include "luckgram/luckgram_privacy_ui.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings/sections/settings_local_passcode.h"
#include "settings/settings_common.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using Luckgram::Tr;
using Luckgram::TrValue;
namespace DoubleBottom = Luckgram::DoubleBottom;
namespace HiddenChats = Luckgram::HiddenChats;

[[nodiscard]] QString NeedPasscodeText() {
	return Tr(
		"请先在「隐私与安全 → 密码锁」里开启应用密码锁。",
		"Turn on Passcode Lock in Privacy and Security first.");
}

[[nodiscard]] QString OpenPasscodeText() {
	return Tr("开启密码锁", "Turn on Passcode Lock");
}

void OpenPasscodeSettings(not_null<Window::SessionController*> controller) {
	controller->showSettings(LocalPasscodeCreateId());
}

// The current account is visible by default (mobile ensureVisibleDefault).
void EnsureVisibleDefault(not_null<Window::SessionController*> controller) {
	if (DoubleBottom::VisibleUserIds().empty()) {
		DoubleBottom::SetVisibleUserIds({
			quint64(controller->session().userId().bare),
		});
	}
}

// The switch turns on once a second passcode and an account are set.
void UpdateEnabled() {
	const auto key = QString::fromLatin1(DoubleBottom::kFeatureKey);
	if (DoubleBottom::HasSecondPasscode()
		&& !DoubleBottom::VisibleUserIds().empty()
		&& !Luckgram::Settings::IsLocallyEnabled(key)) {
		Luckgram::Settings::SetEnabled(key, true);
	}
}

void SetSecondPasscode(
		not_null<Window::SessionController*> controller,
		Fn<void()> done = nullptr) {
	const auto show = controller->uiShow();
	Luckgram::PrivacyUi::AskNewSecondPasscode(show, crl::guard(controller, [=](
			QString second,
			QString) {
		DoubleBottom::SetSecondPasscode(second, crl::guard(controller, [=](
				bool ok) {
			if (!ok) {
				show->showToast(Tr(
					"保存失败，请重试",
					"Couldn't save, please try again"));
				return;
			}
			EnsureVisibleDefault(controller);
			UpdateEnabled();
			show->showToast(Tr("第二密码已设置", "Second passcode set"));
			if (done) {
				done();
			}
		}));
	}));
}

void TurnOffWithMainPasscode(
		not_null<Window::SessionController*> controller,
		Fn<void(bool)> done) {
	Luckgram::PrivacyUi::AskMainPasscode(
		controller->uiShow(),
		Tr("关闭双重空间", "Turn off double space"),
		[=](QString) {
			DoubleBottom::TurnOff();
			if (done) {
				done(true);
			}
		},
		[=] {
			if (done) {
				done(false);
			}
		});
}

void FillDoubleBottom(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		Fn<void()> rebuild) {
	Ui::AddSkip(container);
	if (!DoubleBottom::HasLocalPasscode()) {
		AddButtonWithIcon(
			container,
			rpl::single(OpenPasscodeText()),
			st::settingsButton,
			{ &st::menuIconLock }
		)->addClickHandler([=] {
			OpenPasscodeSettings(controller);
		});
		Ui::AddSkip(container);
		Ui::AddDividerText(container, rpl::single(NeedPasscodeText()));
		return;
	}
	const auto hasSecond = DoubleBottom::HasSecondPasscode();
	AddButtonWithIcon(
		container,
		rpl::single(hasSecond
			? Tr("修改第二密码", "Change second passcode")
			: Tr("设置第二密码", "Set second passcode")),
		st::settingsButton,
		{ &st::menuIconLock }
	)->addClickHandler([=] {
		SetSecondPasscode(controller, rebuild);
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(container, TrValue(
		"用第二个密码解锁时只显示选定的账号",
		"A second passcode that shows only the accounts you pick"));

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		TrValue(
			"第二密码下可见的账号",
			"Accounts visible with the second passcode"));
	const auto visible = DoubleBottom::VisibleUserIds();
	for (const auto &account : Core::App().domain().orderedAccounts()) {
		if (!account->sessionExists()) {
			continue;
		}
		const auto user = account->session().user();
		const auto userId = quint64(account->session().userId().bare);
		const auto checked = ranges::contains(visible, userId);
		const auto button = AddButtonWithIcon(
			container,
			Info::Profile::NameValue(user),
			st::settingsButtonNoIcon);
		button->toggleOn(rpl::single(checked));
		button->toggledChanges(
		) | rpl::filter([=](bool now) {
			return now != checked;
		}) | rpl::on_next([=](bool) {
			const auto updated = Luckgram::Privacy::ToggleVisible(
				DoubleBottom::VisibleUserIds(),
				userId);
			if (!updated) {
				controller->uiShow()->showToast(Tr(
					"至少选择一个账号",
					"Pick at least one account"));
			} else {
				DoubleBottom::SetVisibleUserIds(*updated);
				UpdateEnabled();
			}
			crl::on_main(container, rebuild);
		}, button->lifetime());
	}
	Ui::AddSkip(container);
	Ui::AddDividerText(container, TrValue(
		"用第二密码解锁时，只有这些账号会出现在账号列表、快速切换和通知中。至少选一个。",
		"With the second passcode, only these accounts appear in the account list, the account switcher and notifications. Pick at least one."));

	if (hasSecond) {
		Ui::AddSkip(container);
		AddButtonWithIcon(
			container,
			TrValue("关闭双重空间", "Turn off double space"),
			st::settingsAttentionButton
		)->addClickHandler([=] {
			TurnOffWithMainPasscode(controller, [=](bool ok) {
				if (ok) {
					crl::on_main(container, rebuild);
				}
			});
		});
		Ui::AddSkip(container);
		Ui::AddDivider(container);
	}
}

class LuckgramDoubleBottom : public Section<LuckgramDoubleBottom> {
public:
	LuckgramDoubleBottom(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

LuckgramDoubleBottom::LuckgramDoubleBottom(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> LuckgramDoubleBottom::title() {
	return TrValue("双重空间", "Double space");
}

void LuckgramDoubleBottom::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto rebuild = std::make_shared<Fn<void()>>();
	const auto request = [=] {
		if (*rebuild) {
			(*rebuild)();
		}
	};
	*rebuild = [=] {
		container->clear();
		FillDoubleBottom(container, controller, request);
		container->resizeToWidth(width());
	};
	FillDoubleBottom(container, controller, request);

	// The local passcode or the stored second passcode changed elsewhere.
	rpl::merge(
		Core::App().domain().local().localPasscodeChanged(),
		Luckgram::Settings::Changes() | rpl::filter([](const QString &key) {
			return key.isEmpty() || key.startsWith(u"double_bottom"_q);
		}) | rpl::to_empty
	) | rpl::on_next([=] {
		crl::on_main(container, request);
	}, container->lifetime());
	container->lifetime().add([=] { *rebuild = nullptr; });

	Ui::ResizeFitChild(this, container);
}

void FillHiddenChats(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	Ui::AddSkip(container);
	AddButtonWithIcon(
		container,
		TrValue("打开隐藏的聊天", "Open hidden chats"),
		st::settingsButton,
		{ &st::menuIconStealth }
	)->addClickHandler([=] {
		HiddenChats::Open(controller);
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(container, TrValue(
		"在聊天列表右键聊天，选择「隐藏」，它就会出现在这里。",
		"Right-click a chat in the chat list and choose Hide; it will show up here."));

	Ui::AddSkip(container);
	const auto passcodeOn = DoubleBottom::HasLocalPasscode();
	const auto lock = AddButtonWithIcon(
		container,
		TrValue("进入时需要验证", "Require passcode to open"),
		st::settingsButtonNoIcon);
	lock->toggleOn(rpl::single(passcodeOn && HiddenChats::LockEnabled()));
	lock->setToggleLocked(!passcodeOn);
	lock->toggledChanges(
	) | rpl::on_next([=](bool enabled) {
		if (DoubleBottom::HasLocalPasscode()) {
			HiddenChats::SetLockEnabled(enabled);
		}
	}, lock->lifetime());
	if (!passcodeOn) {
		lock->addClickHandler([=] {
			controller->uiShow()->showToast(Tr(
				"打开隐藏的聊天前要求输入应用密码。需要先开启 Telegram 的密码锁。",
				"Ask for the app passcode before opening hidden chats. Telegram's Passcode Lock must be on first."));
		});
	}
	Ui::AddSkip(container);
	Ui::AddDividerText(container, TrValue(
		"打开隐藏的聊天前要求输入应用密码。需要先开启 Telegram 的密码锁。",
		"Ask for the app passcode before opening hidden chats. Telegram's Passcode Lock must be on first."));

	Ui::AddSkip(container);
	const auto notify = AddButtonWithIcon(
		container,
		TrValue("隐藏的聊天照常通知", "Notify for hidden chats"),
		st::settingsButtonNoIcon);
	notify->toggleOn(rpl::single(HiddenChats::NotifyEnabled()));
	notify->toggledChanges(
	) | rpl::on_next([=](bool enabled) {
		HiddenChats::SetNotifyEnabled(enabled);
	}, notify->lifetime());
	Ui::AddSkip(container);
	Ui::AddDividerText(container, TrValue(
		"关闭时，隐藏聊天的新消息不弹通知，也不计入角标。",
		"When off, new messages in hidden chats show no notification and don't count in the badge."));
}

class LuckgramHiddenChats : public Section<LuckgramHiddenChats> {
public:
	LuckgramHiddenChats(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

LuckgramHiddenChats::LuckgramHiddenChats(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> LuckgramHiddenChats::title() {
	return TrValue("隐藏的聊天", "Hidden chats");
}

void LuckgramHiddenChats::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);
	FillHiddenChats(container, controller);
	Ui::ResizeFitChild(this, container);
}

} // namespace

Type LuckgramDoubleBottomId() {
	return LuckgramDoubleBottom::Id();
}

Type LuckgramHiddenChatsId() {
	return LuckgramHiddenChats::Id();
}

void ConfirmDoubleBottomEnable(
		not_null<Window::SessionController*> controller,
		Fn<void(bool)> done) {
	if (!DoubleBottom::HasLocalPasscode()) {
		controller->show(Ui::MakeConfirmBox({
			.text = NeedPasscodeText(),
			.confirmed = [=](Fn<void()> close) {
				close();
				OpenPasscodeSettings(controller);
			},
			.confirmText = OpenPasscodeText(),
			.cancelText = tr::lng_cancel(tr::now),
			.title = Tr("双重空间", "Double space"),
		}));
		done(false);
	} else if (!DoubleBottom::HasSecondPasscode()
		|| DoubleBottom::VisibleUserIds().empty()) {
		controller->showSettings(LuckgramDoubleBottomId());
		done(false);
	} else {
		done(true);
	}
}

void ConfirmDoubleBottomDisable(
		not_null<Window::SessionController*> controller,
		Fn<void(bool)> done) {
	TurnOffWithMainPasscode(controller, std::move(done));
}

} // namespace Settings
