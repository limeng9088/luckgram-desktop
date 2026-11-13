/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "settings/sections/settings_luckgram_call_recording.h"

#include "data/data_chat_participant_status.h"
#include "data/data_peer.h"
#include "data/data_thread.h"
#include "lang/lang_keys.h"
#include "luckgram/calls/luckgram_call_recording.h"
#include "luckgram/calls/luckgram_call_recording_logic.h"
#include "luckgram/luckgram_ai.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

namespace Rec = Luckgram::CallRecording;

[[nodiscard]] Rec::Texts Tx() {
	return Rec::Texts(Luckgram::IsChinese());
}

void FillCallRecording(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		Fn<void()> rebuild) {
	const auto tx = Tx();
	const auto session = &controller->session();
	const auto enabled = Rec::FeatureEnabled();

	Ui::AddSkip(container);
	const auto enable = AddButtonWithIcon(
		container,
		rpl::single(tx.enable()),
		st::settingsButtonNoIcon);
	enable->toggleOn(rpl::single(enabled));
	enable->toggledChanges(
	) | rpl::filter([=](bool now) {
		return now != enabled;
	}) | rpl::on_next([=](bool now) {
		const auto key = QString::fromLatin1(Rec::kFeatureKey);
		if (!now) {
			Luckgram::Settings::SetEnabled(key, false);
			crl::on_main(container, rebuild);
			return;
		}
		Rec::ConfirmEnable(controller, crl::guard(container, [=](bool ok) {
			if (ok) {
				Luckgram::Settings::SetEnabled(key, true);
			}
			crl::on_main(container, rebuild);
		}));
	}, enable->lifetime());
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		rpl::single(tx.enableInfo() + u"\n\n"_q + tx.legalText()));
	if (!enabled) {
		return;
	}

	Ui::AddSkip(container);
	const auto autoRecord = Rec::AutoRecord();
	const auto autoButton = AddButtonWithIcon(
		container,
		rpl::single(tx.autoRecord()),
		st::settingsButtonNoIcon);
	autoButton->toggleOn(rpl::single(autoRecord));
	autoButton->toggledChanges(
	) | rpl::filter([=](bool now) {
		return now != Rec::AutoRecord();
	}) | rpl::on_next([=](bool now) {
		Rec::SetAutoRecord(now);
	}, autoButton->lifetime());
	Ui::AddSkip(container);
	Ui::AddDividerText(container, rpl::single(tx.autoRecordInfo()));

	Ui::AddSkip(container);
	const auto ai = Rec::AiTranscript();
	const auto aiButton = AddButtonWithIcon(
		container,
		rpl::single(tx.aiTranscript()),
		st::settingsButtonNoIcon);
	aiButton->toggleOn(rpl::single(ai));
	aiButton->toggledChanges(
	) | rpl::filter([=](bool now) {
		return now != Rec::AiTranscript();
	}) | rpl::on_next([=](bool now) {
		Rec::SetAiTranscript(now);
		if (now) {
			// Asked now: after the call no dialog can be shown. Declining
			// turns the AI transcript off again (mobile).
			Luckgram::AI::WithConsent(controller->uiShow(), [] {
			}, crl::guard(container, [=] {
				Rec::SetAiTranscript(false);
				crl::on_main(container, rebuild);
			}));
		}
	}, aiButton->lifetime());
	Ui::AddSkip(container);
	Ui::AddDividerText(container, rpl::single(tx.aiTranscriptInfo()));

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rpl::single(tx.saveTo()));
	const auto target = Rec::Target(session);
	AddButtonWithLabel(
		container,
		rpl::single(tx.chooseChat()),
		rpl::single(target ? target->name() : tx.savedMessages()),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		Window::ShowChooseRecipientBox(
			controller,
			[=](not_null<Data::Thread*> thread) {
				const auto peer = thread->peer();
				if (!Data::CanSend(peer, ChatRestriction::SendFiles)) {
					controller->showToast(tr::lng_restricted_send_files_all(
						tr::now));
					return false;
				}
				Rec::SetTarget(session, peer);
				crl::on_main(container, rebuild);
				return true;
			},
			rpl::single(tx.chooseChat()));
	});
	if (target) {
		AddButtonWithIcon(
			container,
			rpl::single(tx.resetToSaved()),
			st::settingsAttentionButton
		)->addClickHandler([=] {
			Rec::SetTarget(session, nullptr);
			crl::on_main(container, rebuild);
		});
	}
	Ui::AddSkip(container);
	Ui::AddDividerText(container, rpl::single(tx.saveToInfo()));
}

class LuckgramCallRecording : public Section<LuckgramCallRecording> {
public:
	LuckgramCallRecording(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

LuckgramCallRecording::LuckgramCallRecording(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> LuckgramCallRecording::title() {
	return rpl::single(Tx().featureName());
}

void LuckgramCallRecording::setupContent(
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
		FillCallRecording(container, controller, request);
		container->resizeToWidth(width());
	};
	FillCallRecording(container, controller, request);

	// The switch changed in the feature list.
	Luckgram::Settings::Changes(
	) | rpl::filter([](const QString &key) {
		return key.isEmpty()
			|| key == QString::fromLatin1(Rec::kFeatureKey);
	}) | rpl::on_next([=] {
		crl::on_main(container, request);
	}, container->lifetime());
	container->lifetime().add([=] { *rebuild = nullptr; });

	Ui::ResizeFitChild(this, container);
}

} // namespace

Type LuckgramCallRecordingId() {
	return LuckgramCallRecording::Id();
}

} // namespace Settings
