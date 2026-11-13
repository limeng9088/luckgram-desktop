/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/d9/luckgram_notes_widgets.h"

#include "base/event_filter.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "info/profile/info_profile_text.h"
#include "luckgram/d9/luckgram_notes.h"
#include "luckgram/d9/luckgram_notes_text.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "ui/userpic_view.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtGui/QtEvents>

namespace Luckgram::Notes {
namespace {

void OpenEditor(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box(EditBox, controller, peer));
}

void ConfirmDelete(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Ui::MakeConfirmBox({
		.text = Tr("确定删除这条备注吗？", "Delete this note?"),
		.confirmed = [=](Fn<void()> close) {
			Set(peer, QString());
			close();
		},
		.confirmText = Tr("删除", "Delete"),
		.cancelText = Tr("取消", "Cancel"),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

} // namespace

object_ptr<Ui::SlideWrap<Ui::VerticalLayout>> CreateProfileRow(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	result->toggleOn(EnabledValue());
	result->finishAnimating();
	const auto container = result->entity();

	auto text = Value(peer) | rpl::map([](const QString &note) {
		return note.isEmpty()
			? Ui::Text::Link(AddNote(), u"internal:luckgram_note"_q)
			: TextWithEntities{ note };
	});
	auto label = TrValue(
		"私人备注 · 只有你能看到",
		"Private note · only visible to you"
	) | rpl::map([](const QString &value) {
		return TextWithEntities{ value };
	});
	auto line = Info::Profile::CreateTextWithLabel(
		container,
		std::move(label),
		std::move(text),
		st::infoLabel,
		st::infoLabeled,
		st::infoProfileLabeledPadding,
		st::popupMenuWithIcons);
	const auto open = [=] {
		OpenEditor(controller, peer);
	};
	line.text->setSelectable(false);
	line.text->overrideLinkClickHandler(open);
	for (const auto label : { line.text, line.subtext }) {
		label->setCursor(style::cur_pointer);
		base::install_event_filter(label, [=](not_null<QEvent*> e) {
			if (e->type() == QEvent::MouseButtonRelease) {
				const auto mouse = static_cast<QMouseEvent*>(e.get());
				if (mouse->button() == Qt::LeftButton
					&& !Get(peer).isEmpty()) {
					open();
				}
			}
			return base::EventFilterResult::Continue;
		});
	}
	line.text->setContextMenuHook([=](
			Ui::FlatLabel::ContextMenuRequest request) {
		const auto has = !Get(peer).isEmpty();
		request.menu->addAction(
			has ? Tr("编辑备注", "Edit note") : AddNote(),
			open,
			&st::menuIconEdit);
		if (has) {
			request.menu->addAction(
				Tr("删除备注", "Delete note"),
				[=] { ConfirmDelete(controller, peer); },
				&st::menuIconDelete);
		}
	});
	container->add(std::move(line.wrap));
	return result;
}

NoteBar::NoteBar(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer)
: _controller(controller)
, _peer(peer)
, _wrap(parent, object_ptr<Ui::AbstractButton>(parent))
, _shadow(std::make_unique<Ui::PlainShadow>(parent)) {
	_wrap.hide(anim::type::instant);
	_shadow->hide();
	_shadow->showOn(rpl::combine(
		_wrap.shownValue(),
		_wrap.heightValue(),
		rpl::mappers::_1 && rpl::mappers::_2 > 0
	) | rpl::filter([=](bool shown) {
		return (shown == _shadow->isHidden());
	}));
	setup();
}

NoteBar::~NoteBar() = default;

void NoteBar::setup() {
	_wrap.geometryValue(
	) | rpl::on_next([=](QRect rect) {
		updateShadowGeometry(rect);
	}, _wrap.lifetime());

	const auto button = _wrap.entity();
	button->resize(0, st::historyTranslateBarHeight);
	button->setAttribute(Qt::WA_OpaquePaintEvent);
	button->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		QPainter(button).fillRect(clip, st::historyComposeButtonBg);
	}, button->lifetime());
	button->setClickedCallback([=] {
		setExpanded(!_expanded);
	});

	_title = Ui::CreateChild<Ui::FlatLabel>(
		button,
		BarTitle(),
		st::historyTranslateLabel);
	_short = Ui::CreateChild<Ui::FlatLabel>(
		button,
		QString(),
		st::infoLabeledOneLine);
	_full = Ui::CreateChild<Ui::FlatLabel>(
		button,
		QString(),
		st::infoLabeled);
	for (const auto label : { _title, _short, _full }) {
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	_edit = Ui::CreateChild<Ui::LinkButton>(
		button,
		Tr("编辑", "Edit"));
	_edit->setClickedCallback([=] {
		OpenEditor(_controller, _peer);
	});
	_toggle = Ui::CreateChild<Ui::LinkButton>(button, QString());
	_toggle->setClickedCallback([=] {
		setExpanded(!_expanded);
	});

	_expanded = BarExpanded(_peer);
	Value(
		_peer
	) | rpl::on_next([=](const QString &note) {
		_short->setText(NotesText::FirstLine(note));
		_full->setText(note);
		relayout();
		_wrap.toggle(!note.isEmpty(), anim::type::normal);
	}, _wrap.lifetime());
}

void NoteBar::setExpanded(bool expanded) {
	if (_expanded == expanded) {
		return;
	}
	_expanded = expanded;
	SetBarExpanded(_peer, expanded);
	relayout();
}

void NoteBar::relayout() {
	const auto button = _wrap.entity();
	const auto width = _width;
	const auto left = st::boxRowPadding.left();
	const auto right = st::boxRowPadding.right();
	const auto skip = st::semiboldFont->spacew * 2;
	const auto rowHeight = st::historyTranslateBarHeight;

	_title->setText(BarTitle());
	_edit->setText(Tr("编辑", "Edit"));
	_toggle->setText(_expanded
		? Tr("收起", "Collapse")
		: Tr("展开备注", "Show note"));

	_toggle->moveToRight(right, (rowHeight - _toggle->height()) / 2, width);
	_edit->moveToRight(
		right + _toggle->width() + skip,
		(rowHeight - _edit->height()) / 2,
		width);
	const auto buttonsLeft = width
		- right
		- _toggle->width()
		- skip
		- _edit->width()
		- skip;

	_title->resizeToWidth(std::min(
		_title->textMaxWidth(),
		std::max(buttonsLeft - left, 0)));
	_title->moveToLeft(left, (rowHeight - _title->height()) / 2, width);

	auto height = rowHeight;
	if (_expanded) {
		_short->hide();
		_full->show();
		_full->resizeToWidth(std::max(width - left - right, 1));
		_full->moveToLeft(left, rowHeight - skip, width);
		height = _full->y() + _full->height() + skip;
	} else {
		_full->hide();
		const auto shortLeft = _title->x() + _title->width() + skip;
		const auto available = buttonsLeft - shortLeft;
		_short->setVisible(available > 0);
		if (available > 0) {
			_short->resizeToWidth(available);
			_short->moveToLeft(
				shortLeft,
				(rowHeight - _short->height()) / 2,
				width);
		}
	}
	button->resize(width, height);
}

void NoteBar::updateShadowGeometry(QRect wrapGeometry) {
	const auto regular = QRect(
		wrapGeometry.x(),
		wrapGeometry.y() + wrapGeometry.height(),
		wrapGeometry.width(),
		st::lineWidth);
	_shadow->setGeometry(_shadowGeometryPostprocess
		? _shadowGeometryPostprocess(regular)
		: regular);
}

void NoteBar::raise() {
	_wrap.raise();
	_shadow->raise();
}

void NoteBar::finishAnimating() {
	_wrap.finishAnimating();
}

void NoteBar::setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess) {
	_shadowGeometryPostprocess = std::move(postprocess);
	updateShadowGeometry(_wrap.geometry());
}

void NoteBar::move(int x, int y) {
	_wrap.move(x, y);
}

void NoteBar::resizeToWidth(int width) {
	if (_width != width) {
		_width = width;
		relayout();
	}
}

int NoteBar::height() const {
	return _wrap.height();
}

rpl::producer<int> NoteBar::heightValue() const {
	return _wrap.heightValue();
}

} // namespace Luckgram::Notes

namespace Settings {
namespace {

using Luckgram::Tr;
using Luckgram::TrValue;

class NoteRow final : public Ui::RippleButton {
public:
	NoteRow(
		QWidget *parent,
		not_null<PeerData*> peer,
		QString name,
		QString snippet);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const not_null<PeerData*> _peer;
	const QString _name;
	const QString _snippet;
	Ui::PeerUserpicView _userpic;

};

NoteRow::NoteRow(
	QWidget *parent,
	not_null<PeerData*> peer,
	QString name,
	QString snippet)
: RippleButton(parent, st::defaultRippleAnimation)
, _peer(peer)
, _name(std::move(name))
, _snippet(std::move(snippet)) {
	resize(width(), st::peerListBoxItem.height);
	peer->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		update();
	}, lifetime());
}

void NoteRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto &st = st::peerListBoxItem;
	p.fillRect(rect(), isOver() ? st::contactsBgOver : st::contactsBg);
	paintRipple(p, 0, 0);
	_peer->paintUserpicLeft(
		p,
		_userpic,
		st.photoPosition.x(),
		st.photoPosition.y(),
		width(),
		st.photoSize);
	const auto left = st.namePosition.x();
	const auto available = std::max(
		width() - left - st::boxRowPadding.right(),
		1);
	p.setPen(st::contactsNameFg);
	p.setFont(st::semiboldFont);
	p.drawTextLeft(
		left,
		st.namePosition.y(),
		width(),
		st::semiboldFont->elided(_name, available));
	p.setPen(st::contactsStatusFg);
	p.setFont(st::normalFont);
	p.drawTextLeft(
		st.statusPosition.x(),
		st.statusPosition.y(),
		width(),
		st::normalFont->elided(_snippet, available));
}

void FillList(
		not_null<Ui::VerticalLayout*> list,
		not_null<Window::SessionController*> controller,
		const QString &query) {
	list->clear();
	const auto session = &controller->session();
	auto shown = 0;
	for (const auto &entry : Luckgram::Notes::All(session)) {
		const auto peer = session->data().peerLoaded(entry.peer);
		const auto name = peer
			? peer->name()
			: Tr("未知", "Unknown");
		if (!Luckgram::NotesText::Matches(query, entry.note, name)) {
			continue;
		}
		const auto snippet = Luckgram::NotesText::Snippet(entry.note, query);
		const auto target = peer ? peer : session->data().peer(entry.peer);
		const auto row = list->add(object_ptr<NoteRow>(
			list,
			target,
			name,
			snippet));
		row->setClickedCallback([=] {
			controller->showPeerInfo(target);
		});
		++shown;
	}
	if (!shown) {
		list->add(
			object_ptr<Ui::FlatLabel>(
				list,
				query.trimmed().isEmpty()
					? Tr(
						"还没有备注。打开资料页，点击「添加备注…」即可添加。",
						"No notes yet. Open a profile and click “Add a note…”.")
					: Tr("没有找到结果", "No results"),
				st::boxDividerLabel),
			st::boxRowPadding);
	}
	list->resizeToWidth(list->width());
}

} // namespace

LuckgramNotes::LuckgramNotes(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> LuckgramNotes::title() {
	return TrValue("全部备注", "All notes");
}

void LuckgramNotes::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);
	Ui::AddSkip(container);
	const auto search = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::defaultInputField,
			TrValue("搜索备注或名字", "Search notes or names")),
		st::boxRowPadding);
	Ui::AddSkip(container);
	const auto list = container->add(object_ptr<Ui::VerticalLayout>(container));
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		TrValue(
			"只有你能看到这条备注。它只保存在这台设备的当前账号中，不会上传。",
			"Only you can see this note. It is stored on this device for the current account and is never uploaded."));

	const auto refill = [=] {
		FillList(list, controller, search->getLastText());
	};
	search->changes() | rpl::on_next(refill, search->lifetime());
	Luckgram::Notes::Changes(
		&controller->session()
	) | rpl::on_next([=] {
		refill();
	}, list->lifetime());
	refill();

	Ui::ResizeFitChild(this, container);
}

} // namespace Settings
