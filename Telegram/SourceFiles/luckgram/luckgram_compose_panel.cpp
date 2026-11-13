/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_compose_panel.h"

#include "base/event_filter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/fields/input_field.h"

namespace Luckgram::Compose {
namespace {

[[nodiscard]] auto &Hosts() {
	static auto result = base::flat_map<
		not_null<Ui::InputField*>,
		std::unique_ptr<PanelHost>>();
	return result;
}

// Panels are often closed from their own buttons: hide at once, delete
// after the click handler returned.
void Destroy(base::unique_qptr<Ui::RpWidget> &panel) {
	if (const auto raw = panel.release()) {
		raw->hide();
		raw->deleteLater();
	}
}

} // namespace

PanelHost::PanelHost(not_null<QWidget*> anchor, PanelAnchor how)
: _anchor(anchor)
, _how(how) {
	_filter = base::install_event_filter(anchor, [=](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::Move:
		case QEvent::Resize:
		case QEvent::Show:
		case QEvent::Hide:
			updateGeometry();
			break;
		default:
			break;
		}
		return base::EventFilterResult::Continue;
	});
}

PanelHost::~PanelHost() {
	// The anchor may outlive the field: the filter must not call us.
	delete _filter.data();
}

PanelHost *PanelHost::For(not_null<Ui::InputField*> field) {
	const auto i = Hosts().find(field);
	return (i != end(Hosts())) ? i->second.get() : nullptr;
}

Ui::RpWidget *PanelHost::show(object_ptr<Ui::RpWidget> panel) {
	Destroy(_panel);
	const auto raw = panel.release();
	if (!raw) {
		return nullptr;
	}
	raw->setParent(_anchor->parentWidget());
	_panel.reset(raw);
	raw->heightValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		updateGeometry();
	}, raw->lifetime());
	updateGeometry();
	return raw;
}

void PanelHost::hide(not_null<Ui::RpWidget*> panel) {
	if (_panel.get() == panel.get()) {
		Destroy(_panel);
	}
}

void PanelHost::hide() {
	Destroy(_panel);
}

Ui::RpWidget *PanelHost::current() const {
	return _panel.get();
}

void PanelHost::updateGeometry() {
	const auto panel = _panel.get();
	if (!panel || _updating) {
		return;
	}
	_updating = true;
	if (_anchor->isHidden()) {
		panel->hide();
	} else {
		const auto geometry = _anchor->geometry();
		panel->resizeToWidth(geometry.width());
		const auto bottom = (_how == PanelAnchor::Bottom)
			? (geometry.y() + geometry.height())
			: geometry.y();
		panel->move(geometry.x(), bottom - panel->height());
		panel->show();
		panel->raise();
	}
	_updating = false;
}

void AttachPanelHost(
		not_null<Ui::InputField*> field,
		not_null<QWidget*> anchor,
		PanelAnchor how) {
	if (Hosts().contains(field)) {
		return;
	}
	Hosts().emplace(field, std::make_unique<PanelHost>(anchor, how));
	field->lifetime().add([=] {
		Hosts().remove(field);
	});
}

} // namespace Luckgram::Compose
