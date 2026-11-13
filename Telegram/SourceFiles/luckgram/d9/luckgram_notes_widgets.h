/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "settings/settings_common_session.h"
#include "ui/wrap/slide_wrap.h"

class PeerData;

namespace Ui {
class AbstractButton;
class FlatLabel;
class LinkButton;
class PlainShadow;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Luckgram::Notes {

// Profile row (mobile 05 §1, §6): under the bio, 「私人备注 · 只有你能看到」
// with the note, or 「添加备注…」. Click edits; right click edits / deletes.
[[nodiscard]] object_ptr<Ui::SlideWrap<Ui::VerticalLayout>> CreateProfileRow(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

// Chat bar (mobile 05 §3): under the pinned message bar while the chat has
// a note. Collapsed it shows the first line; click expands the whole note,
// 「编辑」 opens the editor. Collapsed / expanded is remembered per chat.
// Same interface as HistoryView::TranslateBar, for HistoryWidget.
class NoteBar final {
public:
	NoteBar(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer);
	~NoteBar();

	void raise();
	void finishAnimating();
	void setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess);

	void move(int x, int y);
	void resizeToWidth(int width);
	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _wrap.lifetime();
	}

private:
	void setup();
	void relayout();
	void updateShadowGeometry(QRect wrapGeometry);
	void setExpanded(bool expanded);

	const not_null<Window::SessionController*> _controller;
	const not_null<PeerData*> _peer;
	Ui::SlideWrap<Ui::AbstractButton> _wrap;
	std::unique_ptr<Ui::PlainShadow> _shadow;
	Fn<QRect(QRect)> _shadowGeometryPostprocess;
	Ui::FlatLabel *_title = nullptr;
	Ui::FlatLabel *_short = nullptr;
	Ui::FlatLabel *_full = nullptr;
	Ui::LinkButton *_edit = nullptr;
	Ui::LinkButton *_toggle = nullptr;
	int _width = 0;
	bool _expanded = false;

};

} // namespace Luckgram::Notes

namespace Settings {

// 「Luckgram 功能 → 私人备注 → 全部备注」 (mobile 05 §4): every note with
// userpic, name and the matching line; search by note text or name; click
// opens the profile.
class LuckgramNotes : public Section<LuckgramNotes> {
public:
	LuckgramNotes(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings
