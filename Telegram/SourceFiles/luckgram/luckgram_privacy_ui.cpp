/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_privacy_ui.h"

#include "core/application.h"
#include "lang/lang_keys.h"
#include "luckgram/luckgram_privacy_logic.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_domain.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <openssl/crypto.h>

namespace Luckgram::PrivacyUi {
namespace {

void Cleanse(QByteArray &bytes) {
	if (!bytes.isEmpty()) {
		OPENSSL_cleanse(bytes.data(), bytes.size());
	}
}

// Checks typed against the local passcode; false when a check is running.
bool CheckMain(const QString &typed, Fn<void(bool correct)> done) {
	auto utf8 = typed.toUtf8();
	const auto started = Core::App().domain().tryPasscode(
		utf8,
		std::move(done));
	Cleanse(utf8);
	return started;
}

[[nodiscard]] not_null<Ui::PasswordInput*> AddField(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> placeholder) {
	// GenericBox::addRow() takes RpWidget-derived rows only and
	// PasswordInput is RpWidgetBase<QLineEdit>: wrap it in a plain row that
	// keeps the field's height and passes its width on.
	const auto wrap = box->addRow(object_ptr<Ui::RpWidget>(box));
	const auto field = Ui::CreateChild<Ui::PasswordInput>(
		wrap,
		st::defaultInputField,
		std::move(placeholder));
	field->show();
	wrap->resize(wrap->width(), field->height());
	wrap->widthValue(
	) | rpl::on_next([=](int width) {
		field->resize(width, field->height());
		field->moveToLeft(0, 0);
	}, field->lifetime());
	return field;
}

[[nodiscard]] not_null<rpl::variable<QString>*> AddErrorLabel(
		not_null<Ui::GenericBox*> box) {
	const auto error = box->lifetime().make_state<rpl::variable<QString>>();
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		error->value(),
		st::boxLabel));
	return error;
}

} // namespace

void AskMainPasscode(
		std::shared_ptr<Ui::Show> show,
		const QString &title,
		Fn<void(QString passcode)> done,
		Fn<void()> cancelled) {
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		const auto succeeded = box->lifetime().make_state<bool>(false);
		box->boxClosing() | rpl::on_next([=] {
			if (!*succeeded && cancelled) {
				cancelled();
			}
		}, box->lifetime());
		const auto field = AddField(
			box,
			TrValue("输入主密码", "Enter the main passcode"));
		const auto error = AddErrorLabel(box);
		const auto checking = box->lifetime().make_state<bool>(false);
		const auto submit = [=] {
			if (*checking) {
				return;
			}
			const auto typed = field->getLastText();
			if (typed.isEmpty()) {
				field->showError();
				return;
			}
			const auto weak = base::make_weak(box);
			*checking = CheckMain(typed, [=](bool correct) {
				if (!weak.get()) {
					return;
				}
				*checking = false;
				if (!correct) {
					*error = Tr("主密码不正确", "Incorrect main passcode");
					field->selectAll();
					field->showError();
					return;
				}
				*succeeded = true;
				box->closeBox();
				done(typed);
			});
		};
		QObject::connect(
			field.get(),
			&Ui::MaskedInputField::submitted,
			submit);
		QObject::connect(field.get(), &Ui::MaskedInputField::changed, [=] {
			*error = QString();
		});
		box->setFocusCallback([=] { field->setFocusFast(); });
		box->addButton(tr::lng_box_ok(), submit);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void AskNewSecondPasscode(
		std::shared_ptr<Ui::Show> show,
		Fn<void(QString second, QString main)> done) {
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(TrValue("设置第二密码", "Set second passcode"));
		const auto main = AddField(
			box,
			TrValue("输入主密码", "Enter the main passcode"));
		const auto second = AddField(
			box,
			TrValue("输入第二密码", "Enter the second passcode"));
		const auto repeat = AddField(
			box,
			TrValue("再次输入第二密码", "Repeat the second passcode"));
		const auto error = AddErrorLabel(box);
		const auto checking = box->lifetime().make_state<bool>(false);
		const auto submit = [=] {
			if (*checking) {
				return;
			}
			const auto typedMain = main->getLastText();
			if (typedMain.isEmpty()) {
				main->showError();
				return;
			}
			using Error = Luckgram::Privacy::SecondPasscodeError;
			switch (Luckgram::Privacy::CheckNewSecondPasscode(
					second->getLastText(),
					repeat->getLastText(),
					typedMain)) {
			case Error::Empty:
				second->showError();
				return;
			case Error::Mismatch:
				*error = Tr(
					"两次输入的密码不一致",
					"The passcodes don't match");
				repeat->selectAll();
				repeat->showError();
				return;
			case Error::SameAsMain:
				*error = Tr(
					"第二密码不能与主密码相同",
					"The second passcode must differ from the main passcode");
				second->selectAll();
				second->showError();
				return;
			case Error::None:
				break;
			}
			const auto weak = base::make_weak(box);
			*checking = CheckMain(typedMain, [=](bool correct) {
				if (!weak.get()) {
					return;
				}
				*checking = false;
				if (!correct) {
					*error = Tr("主密码不正确", "Incorrect main passcode");
					main->selectAll();
					main->showError();
					return;
				}
				const auto typedSecond = second->getLastText();
				box->closeBox();
				done(typedSecond, typedMain);
			});
		};
		for (const auto field : { main, second, repeat }) {
			QObject::connect(
				field.get(),
				&Ui::MaskedInputField::submitted,
				submit);
			QObject::connect(
				field.get(),
				&Ui::MaskedInputField::changed,
				[=] { *error = QString(); });
		}
		box->setFocusCallback([=] { main->setFocusFast(); });
		box->addButton(tr::lng_settings_save(), submit);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

} // namespace Luckgram::PrivacyUi
