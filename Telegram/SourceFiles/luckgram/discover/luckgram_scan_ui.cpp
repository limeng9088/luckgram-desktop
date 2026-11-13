/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/discover/luckgram_scan_ui.h"

#include "apiwrap.h"
#include "base/timer.h"
#include "boxes/connection_box.h"
#include "calls/calls_instance.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "luckgram/discover/luckgram_scan_logic.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/qr/luckgram_qr_decode.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mtproto/facade.h"
#include "mtproto/mtp_instance.h"
#include "settings/sections/settings_calls.h"
#include "settings/settings_common.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/peer_qr_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "webrtc/webrtc_environment.h"
#include "webrtc/webrtc_video_track.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>

namespace Luckgram::ScanUi {
namespace {

constexpr auto kCameraDecodeInterval = crl::time(400);

[[nodiscard]] Scan::Texts Tx() {
	return Scan::Texts(IsChinese());
}

[[nodiscard]] bool CameraAvailable() {
	return (Core::App().calls().currentCall() == nullptr)
		&& (Core::App().calls().currentGroupCall() == nullptr)
		&& !Core::App().mediaDevices().defaultId(
			Webrtc::DeviceType::Camera).isEmpty();
}

void AcceptLogin(
		not_null<Window::SessionController*> controller,
		const QString &raw) {
	const auto encoded = raw.trimmed().mid(
		int(QString::fromLatin1(Scan::kLoginPrefix).size()));
	const auto token = QByteArray::fromBase64(
		encoded.toLatin1(),
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	if (token.isEmpty()) {
		controller->showToast(Tx().noCode());
		return;
	}
	const auto tx = Tx();
	const auto weak = base::make_weak(controller);
	controller->show(Ui::MakeConfirmBox({
		.text = tx.loginConfirm(),
		.confirmed = [=](Fn<void()> close) {
			close();
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			strong->session().api().request(MTPauth_AcceptLoginToken(
				MTP_bytes(token)
			)).done([=] {
				if (const auto strong = weak.get()) {
					strong->showToast(Tx().loginDone());
				}
			}).fail([=](const MTP::Error &error) {
				if (const auto strong = weak.get()) {
					strong->show(Ui::MakeInformBox({
						.text = Tx().loginError(error.type()),
						.title = Tx().loginTitle(),
					}));
				}
			}).send();
		},
		.confirmText = tx.login(),
		.title = tx.loginTitle(),
	}));
}

void TextBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::Show> show,
		QString text) {
	const auto tx = Tx();
	box->setTitle(rpl::single(tx.text()));
	const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		text,
		st::boxLabel));
	label->setSelectable(true);
	box->addButton(rpl::single(tx.copy()), [=] {
		QGuiApplication::clipboard()->setText(text);
		show->showToast(Tx().copied());
		box->closeBox();
	});
	box->addButton(rpl::single(tx.cancel()), [=] { box->closeBox(); });
}

void Handle(
		not_null<Window::SessionController*> controller,
		const QString &raw) {
	switch (Scan::KindOf(raw)) {
	case Scan::Kind::Login:
		AcceptLogin(controller, raw);
		return;
	case Scan::Kind::Proxy:
	case Scan::Kind::Telegram:
	case Scan::Kind::Web:
		// Telegram's own handlers: proxy box, profile / group / channel,
		// and the usual "open this link?" for other sites.
		UrlClickHandler::Open(
			Scan::Url(raw),
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(controller),
				.show = controller->uiShow(),
			}));
		return;
	case Scan::Kind::Text:
		controller->show(Box(TextBox, controller->uiShow(), raw.trimmed()));
		return;
	}
}

// Decodes on a worker thread; `done` runs on the main thread while `guard`
// lives, with nullopt when nothing was found.
void DecodeAsync(
		QImage image,
		not_null<QObject*> guard,
		Fn<void(std::optional<QString>)> done) {
	const auto weak = QPointer<QObject>(guard.get());
	crl::async([=, image = std::move(image)] {
		auto result = Qr::DecodeQr(image);
		crl::on_main([=, result = std::move(result)] {
			if (weak) {
				done(result);
			}
		});
	});
}

void ScanBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto tx = Tx();
	box->setTitle(rpl::single(tx.scan()));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tx.scanHint(),
		st::boxDividerLabel));
	const auto weak = base::make_weak(controller);
	const auto found = [=](const QString &text) {
		box->closeBox();
		if (const auto strong = weak.get()) {
			Handle(strong, text);
		}
	};

	if (CameraAvailable()) {
		const auto track = ::Settings::AddCameraSubsection(
			box->uiShow(),
			box->verticalLayout(),
			false);
		if (track) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				tx.cameraHint(),
				st::boxDividerLabel));
			struct CameraState {
				bool decoding = false;
				base::Timer timer;
			};
			const auto state = box->lifetime().make_state<CameraState>();
			state->timer.setCallback([=] {
				if (state->decoding) {
					return;
				}
				auto frame = track->frame(Webrtc::FrameRequest());
				if (frame.isNull()) {
					return;
				}
				state->decoding = true;
				DecodeAsync(std::move(frame), box, [=](
						std::optional<QString> text) {
					state->decoding = false;
					if (text) {
						state->timer.cancel();
						found(*text);
					}
				});
			});
			state->timer.callEach(kCameraDecodeInterval);
		}
	} else {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tx.noCamera(),
			st::boxLabel));
	}

	const auto container = box->verticalLayout();
	Ui::AddSkip(container);
	const auto decodeImage = [=](QImage image) {
		if (image.isNull()) {
			box->showToast(Tx().noCode());
			return;
		}
		DecodeAsync(std::move(image), box, [=](std::optional<QString> text) {
			if (text) {
				found(*text);
			} else {
				box->showToast(Tx().noCode());
			}
		});
	};
	::Settings::AddButtonWithIcon(
		container,
		rpl::single(tx.fromImage()),
		st::settingsButton,
		{ &st::menuIconPhoto }
	)->addClickHandler([=] {
		FileDialog::GetOpenPath(
			box.get(),
			tx.fromImage(),
			u"Image files (*.png *.jpg *.jpeg *.webp *.bmp *.gif)"_q,
			crl::guard(box.get(), [=](FileDialog::OpenResult &&result) {
				if (!result.paths.isEmpty()) {
					decodeImage(QImage(result.paths.front()));
				} else if (!result.remoteContent.isEmpty()) {
					decodeImage(QImage::fromData(result.remoteContent));
				}
			}));
	});
	::Settings::AddButtonWithIcon(
		container,
		rpl::single(tx.fromClipboard()),
		st::settingsButton,
		{ &st::menuIconCopy }
	)->addClickHandler([=] {
		const auto image = QGuiApplication::clipboard()->image();
		if (image.isNull()) {
			box->showToast(Tx().clipboardEmpty());
		} else {
			decodeImage(image);
		}
	});
	::Settings::AddButtonWithIcon(
		container,
		rpl::single(tx.myQr()),
		st::settingsButton,
		{ &st::menuIconQrCode }
	)->addClickHandler([=] {
		if (const auto strong = weak.get()) {
			Ui::DefaultShowFillPeerQrBoxCallback(
				strong->uiShow(),
				strong->session().user());
		}
	});
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

[[nodiscard]] QString ProxyState(
		not_null<Window::SessionController*> controller) {
	const auto enabled = Core::App().settings().proxy().isEnabled();
	const auto connected = (controller->session().account().mtp().dcstate()
		== MTP::ConnectedState);
	return Tx().proxyState(Scan::StateOf(enabled, connected));
}

} // namespace

bool FeatureEnabled() {
	return Settings::IsEnabled(QString::fromLatin1(Scan::kFeatureKey));
}

void ShowScan(not_null<Window::SessionController*> controller) {
	controller->show(Box(ScanBox, controller));
}

void AddMainMenuItems(
		not_null<Ui::VerticalLayout*> menu,
		not_null<Window::SessionController*> controller) {
	auto shown = rpl::single(QString()) | rpl::then(
		Settings::Changes()
	) | rpl::map([] {
		return FeatureEnabled();
	}) | rpl::distinct_until_changed();
	const auto scan = menu->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			menu,
			::Settings::CreateButtonWithIcon(
				menu,
				TrValue("扫一扫", "Scan"),
				st::mainMenuButton,
				{ &st::menuIconQrCode })));
	scan->toggleOn(rpl::duplicate(shown));
	scan->finishAnimating();
	scan->entity()->setClickedCallback([=] {
		ShowScan(controller);
	});
	const auto proxy = menu->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			menu,
			::Settings::CreateButtonWithIcon(
				menu,
				TrValue("代理配置", "Proxy settings"),
				st::mainMenuButton,
				{ &st::menuIconNetwork })));
	::Settings::CreateRightLabel(
		proxy->entity(),
		rpl::single(ProxyState(controller)),
		st::mainMenuButton,
		TrValue("代理配置", "Proxy settings"));
	proxy->toggleOn(std::move(shown));
	proxy->finishAnimating();
	proxy->entity()->setClickedCallback([=] {
		controller->show(ProxiesBoxController::CreateOwningBox(
			&controller->session().account()));
	});
}

} // namespace Luckgram::ScanUi
