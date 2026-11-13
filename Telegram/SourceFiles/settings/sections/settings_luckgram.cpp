/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "settings/sections/settings_luckgram.h"

#include "settings/sections/settings_luckgram_pro.h"

#include "chat_helpers/compose/compose_show.h"
#include "core/file_utilities.h"
#include "core/version.h"
#include "luckgram/luckgram_ai.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_features.h"
#include "luckgram/luckgram_onboarding.h"
#include "luckgram/luckgram_service.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "settings/detailed_settings_button.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>

namespace Settings {
namespace {

using Luckgram::Tr;
using Luckgram::TrValue;
namespace Features = Luckgram::Features;

[[nodiscard]] QString AudioMime(const QString &path) {
	const auto suffix = QFileInfo(path).suffix().toLower();
	if (suffix == u"ogg"_q || suffix == u"oga"_q || suffix == u"opus"_q) {
		return u"audio/ogg"_q;
	} else if (suffix == u"mp3"_q) {
		return u"audio/mpeg"_q;
	} else if (suffix == u"m4a"_q || suffix == u"mp4"_q) {
		return u"audio/mp4"_q;
	} else if (suffix == u"wav"_q) {
		return u"audio/wav"_q;
	}
	return QString();
}

// D2 §10: dev builds only. Registers the device, refreshes the config, runs
// one streaming chat and one transcription.
void DebugBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setTitle(rpl::single(u"Luckgram API debug"_q));
	box->setWidth(st::boxWideWidth);

	const auto status = box->lifetime().make_state<rpl::variable<QString>>();
	const auto output = box->lifetime().make_state<rpl::variable<QString>>();
	const auto requests = box->lifetime().make_state<
		std::vector<Luckgram::Api::RequestPtr>>();
	const auto refreshStatus = [=] {
		*status = u"platform=%1\nentry=%2%3\ntoken=%4"_q
			.arg(Luckgram::Api::Platform())
			.arg(Luckgram::Api::CurrentBase())
			.arg(Luckgram::Api::UsingBackup() ? u" (backup)"_q : QString())
			.arg(Luckgram::Api::HasToken() ? u"yes"_q : u"no"_q);
	};
	refreshStatus();
	const auto weak = QPointer<Ui::GenericBox>(box.get());
	const auto setOutput = [=](QString text) {
		if (weak) {
			*output = std::move(text);
			refreshStatus();
		}
	};
	const auto fail = [=](Luckgram::Api::Error error) {
		setOutput(u"error: %1 (%2)\n%3"_q
			.arg(error.code)
			.arg(error.status)
			.arg(error.text()));
	};

	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		status->value(),
		st::boxLabel));
	const auto container = box->verticalLayout();
	const auto addAction = [&](const QString &text, Fn<void()> callback) {
		AddButtonWithIcon(
			container,
			rpl::single(text),
			st::settingsButtonNoIcon
		)->addClickHandler(std::move(callback));
	};
	addAction(u"POST /v1/devices"_q, [=] {
		Luckgram::Api::EnsureRegistered([=] {
			setOutput(u"registered"_q);
		}, fail);
	});
	addAction(u"GET /v1/config"_q, [=] {
		Luckgram::RefreshRemoteConfig([=](bool changed) {
			setOutput(changed ? u"config updated"_q : u"config unchanged"_q);
		});
	});
	addAction(u"POST /v1/ai/chat (stream)"_q, [=] {
		Luckgram::AI::WithConsent(controller->uiShow(), [=] {
			setOutput(u"…"_q);
			requests->push_back(Luckgram::AI::Chat({
				.messages = { {
					u"user"_q,
					Tr("用一句话介绍你自己。", "Introduce yourself in one sentence."),
				} },
				.stream = true,
			}, [=](QString, QString text) {
				setOutput(text);
			}, [=](QString text) {
				setOutput(text + u"\n\n[done]"_q);
			}, fail));
		}, [=] {
			setOutput(u"consent declined, nothing sent"_q);
		});
	});
	addAction(u"POST /v1/ai/transcribe"_q, [=] {
		FileDialog::GetOpenPath(
			box.get(),
			u"Audio"_q,
			u"Audio (*.ogg *.oga *.opus *.mp3 *.m4a *.mp4 *.wav)"_q,
			crl::guard(box, [=](FileDialog::OpenResult &&result) {
				if (result.paths.isEmpty()) {
					return;
				}
				const auto path = result.paths.front();
				const auto mime = AudioMime(path);
				auto file = QFile(path);
				if (mime.isEmpty() || !file.open(QIODevice::ReadOnly)) {
					setOutput(u"unsupported file"_q);
					return;
				}
				const auto audio = file.readAll();
				Luckgram::AI::WithConsent(controller->uiShow(), [=] {
					setOutput(u"…"_q);
					requests->push_back(Luckgram::AI::Transcribe(
						audio,
						mime,
						QString(),
						[=](QString text) { setOutput(text); },
						fail));
				}, [=] {
					setOutput(u"consent declined, nothing sent"_q);
				});
			}));
	});
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		output->value(),
		st::boxLabel));
	box->addButton(rpl::single(Tr("关闭", "Close")), [=] {
		box->closeBox();
	});
	box->boxClosing() | rpl::on_next([=] {
		for (const auto &request : *requests) {
			request->cancel();
		}
	}, box->lifetime());
}

void AddFeatureRow(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		const Features::Feature &feature) {
	const auto key = feature.key;
	const auto allowed = Luckgram::Settings::IsRemotelyAllowed(key);
	auto about = allowed
		? TrValue(feature.aboutZh, feature.aboutEn)
		: TrValue("暂不可用", "Not available right now");
	const auto button = AddDetailedSettingsButton(
		container,
		TrValue(feature.titleZh, feature.titleEn),
		std::move(about),
		{},
		rpl::single(QString()) | rpl::then(
			Luckgram::Settings::Changes()
		) | rpl::map([=] {
			return Luckgram::Settings::IsLocallyEnabled(key);
		}),
		st::detailedSettingsButtonStyle);
	button->setToggleLocked(!allowed);
	button->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return toggled != Luckgram::Settings::IsLocallyEnabled(key);
	}) | rpl::on_next([=](bool toggled) {
		Luckgram::Settings::SetEnabled(key, toggled);
	}, button->lifetime());
	if (const auto subpage = feature.subpage) {
		AddButtonWithIcon(
			container,
			TrValue("设置", "Settings"),
			st::settingsButtonNoIcon
		)->addClickHandler([=] {
			controller->showSettings(subpage());
		});
	}
}

void AddAiConsentRow(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	AddButtonWithLabel(
		container,
		TrValue("AI 服务授权", "AI service consent"),
		Luckgram::AI::ConsentValue() | rpl::map([](bool given) {
			return given ? Tr("已同意", "Allowed") : Tr("未同意", "Not allowed");
		}),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		if (Luckgram::AI::HasConsent()) {
			Luckgram::AI::ShowRevokeConsent(controller->uiShow());
		}
	});
	Ui::AddDividerText(
		container,
		Luckgram::AI::ConsentValue() | rpl::map([](bool given) {
			return given
				? Tr("已同意，点按可撤回。撤回后下次使用 AI 功能时会重新询问。", "Allowed. Tap to revoke; you'll be asked again the next time you use an AI feature.")
				: Tr("未同意，首次使用 AI 功能时会询问。", "Not allowed. You'll be asked the first time you use an AI feature.");
		}));
}

void SetupLuckgramContent(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	Ui::AddSkip(container);
	for (const auto group : Features::Groups()) {
		auto features = std::vector<const Features::Feature*>();
		for (const auto &feature : Features::All()) {
			if (feature.group == group) {
				features.push_back(&feature);
			}
		}
		const auto ai = (group == Features::Group::AI);
		const auto debug = (group == Features::Group::Debug);
		// D12: 「账号」 / 「Luckgram Pro」 head 账号与隐私 (as on mobile).
		const auto account = (group == Features::Group::AccountPrivacy);
		if (features.empty() && !ai && !debug && !account) {
			continue; // No desktop feature in this group yet.
		}
		Ui::AddSubsectionTitle(
			container,
			rpl::single(Features::GroupTitle(group)));
		if (account) {
			AddLuckgramAccountRows(container, controller);
		}
		for (const auto feature : features) {
			AddFeatureRow(container, controller, *feature);
		}
		if (ai) {
			AddAiConsentRow(container, controller);
		} else if (debug) {
			AddButtonWithIcon(
				container,
				rpl::single(u"Luckgram API debug"_q),
				st::settingsButtonNoIcon
			)->addClickHandler([=] {
				controller->show(Box(DebugBox, controller));
			});
			Ui::AddSkip(container);
			Ui::AddDivider(container);
		} else {
			Ui::AddSkip(container);
			Ui::AddDivider(container);
		}
		Ui::AddSkip(container);
	}
	// D2 §2: version + 「Luckgram 是基于 Telegram 开源代码的独立应用」;
	// D5: the version line in the 28 format, click copies.
	Ui::AddDividerText(
		container,
		TrValue(
			"Luckgram 是基于 Telegram 开源代码的独立应用",
			"Luckgram is an independent app based on Telegram's open-source code"));
	Luckgram::AddVersionRow(container, controller);
}

} // namespace

void ShowLuckgramDebugBox(not_null<Window::SessionController*> controller) {
	if (Features::IsDevBuild()) {
		controller->show(Box(DebugBox, controller));
	}
}

LuckgramFeatures::LuckgramFeatures(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> LuckgramFeatures::title() {
	return TrValue("Luckgram 功能", "Luckgram Features");
}

void LuckgramFeatures::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);
	SetupLuckgramContent(container, controller);
	Ui::ResizeFitChild(this, container);
}

void AddLuckgramFeaturesButton(Builder::SectionBuilder &builder) {
	builder.addSectionButton({
		.title = TrValue("Luckgram 功能", "Luckgram Features"),
		.targetSection = LuckgramFeatures::Id(),
		.icon = { &st::menuIconSend }, // Luckgram paper plane.
		.keywords = { u"luckgram"_q, u"ai"_q, u"功能"_q },
	});
}

} // namespace Settings
