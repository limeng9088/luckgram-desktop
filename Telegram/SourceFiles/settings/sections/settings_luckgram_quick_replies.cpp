/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "settings/sections/settings_luckgram_quick_replies.h"

#include "base/event_filter.h"
#include "lang/lang_keys.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/quickreplies/quick_replies.h"
#include "luckgram/quickreplies/quick_replies_logic.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using namespace Luckgram::QuickReplies;
using Luckgram::Tr;
using Luckgram::TrValue;

[[nodiscard]] QString RowText(const QString &text) {
	auto line = text.section(QChar('\n'), 0, 0);
	if (line.size() < text.size()) {
		line += QString::fromUtf8("…");
	}
	return line;
}

// index < 0: a new one.
void EditBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		int index,
		QString current) {
	box->setTitle(index < 0
		? TrValue("添加快捷回复", "Add quick reply")
		: TrValue("编辑快捷回复", "Edit quick reply"));
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine,
		TrValue("输入回复内容", "Reply text"),
		current));
	box->setFocusCallback([=] { field->setFocusFast(); });
	const auto save = [=] {
		const auto text = field->getLastText();
		if (!Normalize(text)) {
			field->showError();
			return;
		}
		const auto ok = (index < 0)
			? Add(session, text)
			: Edit(session, index, text);
		if (ok) {
			box->closeBox();
		} else {
			box->showToast(Tr(
				"最多只能保存 100 条快捷回复",
				"You can save up to 100 quick replies"));
		}
	};
	field->submits() | rpl::on_next([=](Qt::KeyboardModifiers m) {
		if (m & Qt::ControlModifier) {
			save(); // Enter makes a new line, Ctrl+Enter saves.
		}
	}, field->lifetime());
	box->addButton(TrValue("保存", "Save"), save);
	box->addButton(TrValue("取消", "Cancel"), [=] { box->closeBox(); });
}

void ConfirmRemove(
		not_null<Window::SessionController*> controller,
		int index) {
	const auto session = &controller->session();
	controller->show(Ui::MakeConfirmBox({
		.text = TrValue("删除这条快捷回复？", "Delete this quick reply?"),
		.confirmed = [=](Fn<void()> &&close) {
			Remove(session, index);
			close();
		},
		.confirmText = TrValue("删除", "Delete"),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void FillList(
		not_null<Ui::VerticalLayout*> list,
		not_null<Window::SessionController*> controller,
		std::shared_ptr<base::unique_qptr<Ui::PopupMenu>> menu) {
	const auto session = &controller->session();
	const auto items = Items(session);
	if (items.isEmpty()) {
		AddButtonWithLabel(
			list,
			TrValue("还没有快捷回复", "No quick replies yet"),
			rpl::single(QString()),
			st::settingsButtonNoIcon)->setAttribute(
				Qt::WA_TransparentForMouseEvents);
		return;
	}
	const auto count = int(items.size());
	for (auto i = 0; i != count; ++i) {
		const auto text = items[i];
		const auto button = AddButtonWithIcon(
			list,
			rpl::single(RowText(text)),
			st::settingsButtonNoIcon);
		button->addClickHandler([=] {
			controller->show(Box(EditBox, session, i, text));
		});
		base::install_event_filter(button, [=](not_null<QEvent*> e) {
			if (e->type() != QEvent::ContextMenu) {
				return base::EventFilterResult::Continue;
			}
			*menu = base::make_unique_q<Ui::PopupMenu>(
				button,
				st::popupMenuWithIcons);
			if (i > 0) {
				(*menu)->addAction(Tr("上移", "Move up"), [=] {
					Move(session, i, i - 1);
				}, &st::menuIconAbove);
			}
			if (i + 1 < count) {
				(*menu)->addAction(Tr("下移", "Move down"), [=] {
					Move(session, i, i + 1);
				}, &st::menuIconBelow);
			}
			(*menu)->addAction(Tr("编辑", "Edit"), [=] {
				controller->show(Box(EditBox, session, i, text));
			}, &st::menuIconEdit);
			(*menu)->addAction(Tr("删除", "Delete"), [=] {
				ConfirmRemove(controller, i);
			}, &st::menuIconDelete);
			(*menu)->popup(QCursor::pos());
			return base::EventFilterResult::Cancel;
		});
	}
}

} // namespace

LuckgramQuickReplies::LuckgramQuickReplies(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> LuckgramQuickReplies::title() {
	return TrValue("快捷回复", "Quick replies");
}

void LuckgramQuickReplies::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto session = &controller->session();

	Ui::AddSkip(container);
	AddButtonWithIcon(
		container,
		TrValue("添加快捷回复", "Add quick reply"),
		st::settingsButtonActive,
		{ &st::menuIconAdd }
	)->addClickHandler([=] {
		if (!CanAdd(Items(session).size())) {
			controller->showToast(Tr(
				"最多只能保存 100 条快捷回复",
				"You can save up to 100 quick replies"));
			return;
		}
		controller->show(Box(EditBox, session, -1, QString()));
	});
	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);

	const auto list = container->add(
		object_ptr<Ui::VerticalLayout>(container));
	const auto menu = std::make_shared<base::unique_qptr<Ui::PopupMenu>>();
	const auto refill = [=] {
		while (list->count()) {
			delete list->widgetAt(0);
		}
		FillList(list, controller, menu);
		list->resizeToWidth(container->width());
	};
	refill();
	Changes(session) | rpl::on_next(refill, list->lifetime());

	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		TrValue(
			"模板只保存在本机当前账号，不会上传。右键一条可上移、下移或删除，"
			"点击编辑。每条最多 1000 字，最多 100 条。",
			"Templates are stored only on this device, for the current "
			"account, and are never uploaded. Right-click one to move it up "
			"or down or delete it, click to edit. Up to 1000 characters "
			"each, 100 in total."));

	Ui::ResizeFitChild(this, container);
}

} // namespace Settings
