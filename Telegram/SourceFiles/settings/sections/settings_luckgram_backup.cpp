/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "settings/sections/settings_luckgram_backup.h"

#include "core/application.h"
#include "data/data_user.h"
#include "luckgram/backup/luckgram_backup.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"

namespace Settings {

namespace Backup = Luckgram::Backup;

LuckgramBackup::LuckgramBackup(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> LuckgramBackup::title() {
	return rpl::single(Backup::TitleText());
}

void LuckgramBackup::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);

	// Every logged-in account is selected at first, like on mobile.
	const auto deselected = lifetime().make_state<
		base::flat_set<not_null<Main::Account*>>>();
	auto accounts = std::vector<not_null<Main::Account*>>();
	for (const auto account : Core::App().domain().orderedAccounts()) {
		if (account->sessionExists()) {
			accounts.push_back(account);
		}
	}

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		rpl::single(Backup::SelectHeaderText()));
	for (const auto account : accounts) {
		const auto button = AddButtonWithIcon(
			container,
			rpl::single(account->session().user()->name()),
			st::settingsButtonNoIcon);
		button->toggleOn(rpl::single(true));
		button->toggledChanges(
		) | rpl::on_next([=](bool toggled) {
			if (toggled) {
				deselected->remove(account);
			} else {
				deselected->emplace(account);
			}
		}, button->lifetime());
	}
	Ui::AddSkip(container);
	AddButtonWithIcon(
		container,
		rpl::single(Backup::ExportText()),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		// The list again: an account may have logged out meanwhile.
		auto selected = std::vector<not_null<Main::Account*>>();
		for (const auto account : Core::App().domain().orderedAccounts()) {
			if (account->sessionExists()
				&& !deselected->contains(account)) {
				selected.push_back(account);
			}
		}
		Backup::StartExport(controller->uiShow(), std::move(selected));
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		rpl::single(
			Backup::ExportHintText() + u"\n\n"_q + Backup::WarningText()));
	Ui::AddSkip(container);
	AddButtonWithIcon(
		container,
		rpl::single(Backup::RestoreText()),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		Backup::StartRestore(controller->uiShow());
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		rpl::single(
			Backup::RestoreHintText() + u"\n\n"_q + Backup::InfoText()));
	Ui::AddSkip(container);

	Ui::ResizeFitChild(this, container);
}

} // namespace Settings
