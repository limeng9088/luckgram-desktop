/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_onboarding.h"

#include "base/call_delayed.h"
#include "luckgram/update/luckgram_update.h"
#include "base/event_filter.h"
#include "boxes/abstract_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/file_utilities.h"
#include "core/version.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "luckgram/discover/luckgram_nearby_state.h"
#include "luckgram/luckgram_desktop.h"
#include "luckgram/luckgram_features.h"
#include "luckgram/luckgram_onboarding_logic.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/pro/luckgram_pro.h"
#include "settings/sections/settings_luckgram.h"
#include "settings/sections/settings_luckgram_pro.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>

namespace Luckgram {
namespace {

using namespace Onboarding;

constexpr auto kAutoPrivacyDelay = crl::time(250);
constexpr auto kAfterLoginDelay = crl::time(1500);

[[nodiscard]] Texts CurrentTexts() {
	return Texts(IsChinese());
}

[[nodiscard]] rpl::producer<Texts> TextsValue() {
	return rpl::single(rpl::empty) | rpl::then(
		Lang::GetInstance().updated()
	) | rpl::map([] {
		return CurrentTexts();
	});
}

[[nodiscard]] QString Key(const char *key) {
	return QString::fromLatin1(key);
}

[[nodiscard]] QJsonValue RemoteValue(const char *key) {
	return Settings::RemoteConfig().value(
		u"values"_q
	).toObject().value(Key(key));
}

[[nodiscard]] QString CurrentPrivacyUrl() {
	return PrivacyUrl(RemoteValue(kRemotePrivacyUrl).toString(), IsChinese());
}

[[nodiscard]] qint64 RemotePrivacyVersion() {
	return qint64(Settings::RemoteNumber(Key(kRemotePrivacyVersion), 0.));
}

[[nodiscard]] bool PrivacyAccepted() {
	return Settings::GetBool(Key(kPrivacyAcceptedKey), false);
}

void AcceptPrivacy() {
	Settings::SetBool(Key(kPrivacyAcceptedKey), true);
	Settings::SetInt(
		Key(kPrivacyAcceptedAtKey),
		QDateTime::currentMSecsSinceEpoch());
	Settings::SetInt(Key(kPrivacyVersionKey), RemotePrivacyVersion());
}

// D12: the Pro page is on the desktop; users who already have Pro skip it.
[[nodiscard]] bool ProStepAvailable() {
	return !Pro::IsPro();
}

[[nodiscard]] bool FeatureOnDesktop(const QString &key) {
	return Features::Find(key) && Settings::IsRemotelyAllowed(key);
}

[[nodiscard]] QStringList SeenSteps() {
	return Settings::GetString(Key(kSeenStepsKey)).split(
		QChar(','),
		Qt::SkipEmptyParts);
}

void MarkStepsSeen(const std::vector<Step> &steps) {
	Settings::SetString(
		Key(kSeenStepsKey),
		MarkSeen(SeenSteps(), steps).join(QChar(',')));
}

QPointer<Ui::BoxContent> PrivacyBoxShown;

void PrivacyBox(
		not_null<Ui::GenericBox*> box,
		bool updated,
		Fn<void()> accepted) {
	// Texts follow the interface language live: on the first start the box
	// opens before the Chinese language pack is in (the welcome page then
	// turned Chinese while this box stayed English, build 81 D5).
	const auto text = [](auto method) {
		return TextsValue() | rpl::map([=](const Texts &texts) {
			return (texts.*method)();
		});
	};
	box->setTitle(text(updated
		? &Texts::privacyUpdatedTitle
		: &Texts::privacyTitle));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		text(updated ? &Texts::privacyUpdatedText : &Texts::privacyText),
		st::boxLabel));
	box->setCloseByEscape(false);
	box->setCloseByOutsideClick(false);
	box->addLeftButton(text(&Texts::privacyPolicyButton), [] {
		File::OpenUrl(CurrentPrivacyUrl()); // The dialog stays open.
	});
	box->addButton(text(&Texts::continueButton), [=] {
		AcceptPrivacy();
		const auto done = accepted;
		box->closeBox();
		if (done) {
			done();
		}
	});
}

void ShowPrivacyBox(
		bool updated,
		Fn<void()> accepted,
		std::shared_ptr<Ui::Show> show = nullptr) {
	if (PrivacyBoxShown) {
		return;
	}
	auto box = Box(PrivacyBox, updated, std::move(accepted));
	PrivacyBoxShown = box.data();
	if (show) {
		show->showBox(std::move(box));
	} else {
		Ui::show(std::move(box));
	}
}

void OnboardingBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		std::vector<Step> steps) {
	struct State {
		rpl::variable<int> index = 0;
		Fn<void()> rebuild;
		Fn<void(bool)> choose;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto texts = CurrentTexts();
	const auto count = int(steps.size());
	const auto step = [=](int index) {
		return steps[std::clamp(index, 0, count - 1)];
	};

	box->setWidth(st::boxWideWidth);
	box->setCloseByOutsideClick(false);
	box->setTitle(state->index.value() | rpl::map([=](int index) {
		return texts.title(step(index));
	}));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		state->index.value() | rpl::map([=](int index) {
			return Progress(index, count);
		}),
		st::boxDividerLabel));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		state->index.value() | rpl::map([=](int index) {
			return texts.body(step(index));
		}),
		st::boxLabel));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		state->index.value() | rpl::map([=](int index) {
			return tr::bold(texts.question(step(index)));
		}),
		st::boxLabel));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		state->index.value() | rpl::map([=](int index) {
			auto result = TextWithEntities();
			if (step(index) == Step::Pro) {
				for (const auto &[title, about] : texts.proFeatures()) {
					if (!result.text.isEmpty()) {
						result.append(u"\n\n"_q);
					}
					result.append(tr::bold(title)).append(u"\n"_q).append(about);
				}
			}
			return result;
		}),
		st::boxLabel));

	state->choose = [=](bool primary) {
		const auto current = step(state->index.current());
		if (primary
			&& current == Step::Pro
			&& !CanSubscribe(current, Pro::BillingEnabled())) {
			return; // "Coming soon".
		}
		for (const auto &key : Toggles(current)) {
			Settings::SetEnabled(key, primary);
		}
		MarkStepsSeen({ current });
		const auto subscribe = primary && (current == Step::Pro);
		if (!subscribe && state->index.current() + 1 < count) {
			state->index = state->index.current() + 1;
			state->rebuild();
			return;
		}
		const auto weak = base::make_weak(controller);
		box->closeBox();
		if (subscribe) {
			if (const auto strong = weak.get()) {
				// Paying needs the Pro page (D12), the user is logged in.
				strong->showSettings(::Settings::LuckgramPro::Id());
			}
		}
	};
	state->rebuild = [=] {
		box->clearButtons();
		const auto current = step(state->index.current());
		const auto comingSoon = (current == Step::Pro)
			&& !CanSubscribe(current, Pro::BillingEnabled());
		box->addButton(
			rpl::single(comingSoon ? texts.comingSoon() : texts.primary(current)),
			[=] { state->choose(true); });
		if (HasSecondary(current)) {
			box->addLeftButton(
				rpl::single(texts.secondary(current)),
				[=] { state->choose(false); });
		}
	};
	state->rebuild();

	// Closed early: the pages shown this time are not shown again.
	box->boxClosing() | rpl::on_next([=] {
		MarkStepsSeen(steps);
	}, box->lifetime());
}

void ShowOnboarding(not_null<Window::SessionController*> controller) {
	auto steps = StepsToShow(FeatureOnDesktop, ProStepAvailable(), SeenSteps());
	if (!steps.empty()) {
		controller->show(Box(OnboardingBox, controller, std::move(steps)));
	}
}

// Returns true when the "updated" dialog was shown.
bool CheckPrivacyUpdate(not_null<Window::SessionController*> controller) {
	static auto prompted = false;
	if (prompted) {
		return false;
	}
	const auto accepted = Settings::GetInt(Key(kPrivacyVersionKey), 0);
	if (!ShouldPromptUpdate(true, RemotePrivacyVersion(), accepted)) {
		return false;
	}
	prompted = true;
	const auto weak = base::make_weak(controller);
	ShowPrivacyBox(true, [=] {
		if (const auto strong = weak.get()) {
			ShowOnboarding(strong);
		}
	}, controller->uiShow());
	return true;
}

void HelpBox(not_null<Ui::GenericBox*> box) {
	const auto texts = CurrentTexts();
	box->setTitle(rpl::single(texts.codeTitle()));
	box->setWidth(st::boxWideWidth);
	auto index = 0;
	for (const auto &text : texts.codeSteps()) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString::number(++index) + u". "_q + text),
			st::boxLabel));
	}
	Ui::AddSkip(box->verticalLayout());
	Ui::AddSubsectionTitle(
		box->verticalLayout(),
		rpl::single(texts.codeHelpTitle()));
	const auto link = tr::lng_code_no_telegram(tr::now);
	for (const auto &text : texts.codeHelpTexts(link)) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(text),
			st::boxLabel));
		Ui::AddSkip(box->verticalLayout());
	}
	box->addButton(rpl::single(texts.codeOk()), [=] { box->closeBox(); });
}

} // namespace

void SetupWelcomePage(not_null<Ui::RpWidget*> step) {
	const auto link = Ui::CreateChild<Ui::LinkButton>(
		step.get(),
		CurrentTexts().privacyPolicyLink(),
		st::introLink);
	link->setClickedCallback([] {
		File::OpenUrl(CurrentPrivacyUrl());
	});
	TextsValue() | rpl::on_next([=](const Texts &texts) {
		link->setText(texts.privacyPolicyLink());
	}, link->lifetime());
	rpl::combine(
		step->sizeValue(),
		link->widthValue()
	) | rpl::on_next([=](QSize size, int) {
		link->moveToLeft(
			(size.width() - link->width()) / 2,
			size.height() - st::introTermsBottom - link->height());
	}, link->lifetime());
	link->show();

	static auto asked = false;
	if (!asked && ShouldAskPrivacy(PrivacyAccepted())) {
		asked = true;
		base::call_delayed(kAutoPrivacyDelay, step, [] {
			if (ShouldAskPrivacy(PrivacyAccepted())) {
				ShowPrivacyBox(false, nullptr);
			}
		});
	}
}

bool ConfirmPrivacyBeforeStart(Fn<void()> accepted) {
	if (!ShouldAskPrivacy(PrivacyAccepted())) {
		return true;
	}
	ShowPrivacyBox(false, std::move(accepted));
	return false;
}

void OnMainShown(not_null<Window::SessionController*> controller) {
	static auto started = false;
	if (started) {
		return;
	}
	started = true;
	Update::Start(controller); // D4: at start, then every 6 hours.
	base::call_delayed(kAfterLoginDelay, controller, [=] {
		NearbyState::RestoreReminder(controller); // D10: 25 reminder.
		if (!CheckPrivacyUpdate(controller)) {
			ShowOnboarding(controller);
		}
		// The remote config may come after the start: check the policy
		// version again when it is merged.
		Settings::Changes(
		) | rpl::filter([](const QString &key) {
			return key.isEmpty();
		}) | rpl::on_next([=] {
			CheckPrivacyUpdate(controller);
		}, controller->lifetime());
	});
}

QString CurrentVersionLine() {
	return VersionLine(
		QString::fromLatin1(AppVersionStr),
		BuildNumber(),
		CommitHash());
}

void CopyVersionLine(std::shared_ptr<Ui::Show> show) {
	QGuiApplication::clipboard()->setText(CurrentVersionLine());
	if (show) {
		show->showToast(CurrentTexts().copied());
	}
}

void AddVersionRow(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	const auto padding = st::defaultBoxDividerLabelPadding;
	const auto row = container->add(object_ptr<Ui::RpWidget>(container));
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		row,
		CurrentVersionLine(),
		st::boxDividerLabel);
	label->setCursor(style::cur_pointer);
	row->resize(
		container->width(),
		padding.top() + label->height() + padding.bottom());
	row->widthValue() | rpl::on_next([=](int width) {
		label->resizeToNaturalWidth(
			std::max(width - padding.left() - padding.right(), 1));
		label->moveToLeft((width - label->width()) / 2, padding.top());
		row->resize(
			width,
			padding.top() + label->height() + padding.bottom());
	}, label->lifetime());

	const auto weak = base::make_weak(controller);
	base::install_event_filter(label, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonRelease) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			if (mouse->button() == Qt::LeftButton) {
				if (const auto strong = weak.get()) {
					CopyVersionLine(strong->uiShow());
				}
			}
		} else if (e->type() == QEvent::ContextMenu) {
			if (Features::IsDevBuild()) {
				if (const auto strong = weak.get()) {
					::Settings::ShowLuckgramDebugBox(strong);
				}
				return base::EventFilterResult::Cancel;
			}
		}
		return base::EventFilterResult::Continue;
	});
}

void SetupCodeHelp(
		not_null<Ui::RpWidget*> step,
		not_null<Ui::LinkButton*> noTelegramCode) {
	const auto help = Ui::CreateChild<Ui::LinkButton>(
		step.get(),
		CurrentTexts().codeHelpButton(),
		st::introLink);
	help->setClickedCallback([] {
		Ui::show(Box(HelpBox));
	});
	TextsValue() | rpl::on_next([=](const Texts &texts) {
		help->setText(texts.codeHelpButton());
	}, help->lifetime());

	// Same line as the SMS link (right aligned) when both fit in the
	// content column, else on the next line.
	rpl::combine(
		noTelegramCode->geometryValue(),
		help->widthValue(),
		step->widthValue()
	) | rpl::on_next([=](QRect link, int helpWidth, int) {
		const auto right = link.x() - st::buttonRadius
			+ st::introNextButton.width
			- st::buttonRadius;
		const auto sameLine = (link.x() + link.width() + st::introLinkTop
			+ helpWidth <= right);
		if (sameLine) {
			help->moveToLeft(right - helpWidth, link.y());
		} else {
			help->moveToLeft(link.x(), link.y() + link.height() + st::buttonRadius);
		}
	}, help->lifetime());

	noTelegramCode->shownValue(
	) | rpl::on_next([=](bool shown) {
		help->setVisible(shown);
	}, help->lifetime());
}

} // namespace Luckgram
