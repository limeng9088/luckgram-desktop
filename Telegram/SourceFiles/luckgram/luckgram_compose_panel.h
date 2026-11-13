/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "base/object_ptr.h"
#include "base/unique_qptr.h"

#include <QtCore/QPointer>

namespace Ui {
class RpWidget;
class InputField;
} // namespace Ui

// D7: the panel container above the message field (AI reply candidates,
// "已润色 · 撤销", AI post...), shared by HistoryWidget and ComposeControls.
// One panel at a time. It floats over the bottom of the message list, right
// above the compose area (and above the reply / forward preview), full
// width of that area; the panel decides its own height.
//
// Hosts attach once with one line:
//   HistoryWidget:   AttachPanelHost(_field, _scroll, PanelAnchor::Bottom);
//   ComposeControls: AttachPanelHost(_field, _wrap.get(), PanelAnchor::Top);
// Features find it by the field (Compose::SlotContext::field).
//
// A panel sizes itself in resizeGetHeight (e.g. a Ui::VerticalLayout); it is
// destroyed when hidden, replaced, or with the host.
namespace Luckgram::Compose {

enum class PanelAnchor {
	Bottom, // The panel's bottom is the anchor's bottom (the message list).
	Top, // The panel's bottom is the anchor's top (the compose area).
};

class PanelHost final {
public:
	PanelHost(not_null<QWidget*> anchor, PanelAnchor how);
	~PanelHost();

	[[nodiscard]] static PanelHost *For(not_null<Ui::InputField*> field);

	// Takes the panel, replacing (destroying) any other; returns it.
	Ui::RpWidget *show(object_ptr<Ui::RpWidget> panel);
	// Destroys the panel if it is the one shown.
	void hide(not_null<Ui::RpWidget*> panel);
	void hide();
	[[nodiscard]] Ui::RpWidget *current() const;

private:
	void updateGeometry();

	const not_null<QWidget*> _anchor;
	const PanelAnchor _how;
	QPointer<QObject> _filter;
	base::unique_qptr<Ui::RpWidget> _panel;
	bool _updating = false;

};

// Destroyed with the field.
void AttachPanelHost(
	not_null<Ui::InputField*> field,
	not_null<QWidget*> anchor,
	PanelAnchor how);

} // namespace Luckgram::Compose
