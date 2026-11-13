/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "ui/wrap/slide_wrap.h"

class History;
class PeerData;

namespace Ui {
class PlainShadow;
class RpWidget;
} // namespace Ui

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Window {
class SessionController;
} // namespace Window

// D7 (mobile 19): chat summary at the top of a chat. A thin bar
// 「128 条未读消息 · 总结」 from 30 unread messages, and the card with 3–8
// points, references that jump to the message, 「AI 生成，可能有误」 and
// 「问这些消息…」. The range is loaded straight from Telegram (nothing is
// marked read), long ranges are summarized in parts and merged. Same
// interface as the other top bars of HistoryWidget (D9 JumpBar).
namespace Luckgram::AI {

class SummaryBar final {
public:
	SummaryBar(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<History*> history);
	~SummaryBar();

	// The bar of an open chat, for the 「…」 menu.
	[[nodiscard]] static SummaryBar *For(not_null<History*> history);

	// 「总结聊天」: unread / last 100 / 300 / 500.
	void showRangePicker();

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
	struct State;

	void setup();
	void updateShadowGeometry(QRect wrapGeometry);

	const not_null<Window::SessionController*> _controller;
	const not_null<History*> _history;
	Ui::SlideWrap<Ui::RpWidget> _wrap;
	std::unique_ptr<Ui::PlainShadow> _shadow;
	Fn<QRect(QRect)> _shadowGeometryPostprocess;
	std::unique_ptr<State> _state;
	Fn<void()> _relayout;
	Fn<void(int, QString)> _start; // count, range label

};

// 「总结聊天」 in the chat's 「…」 menu (window_peer_menu.cpp, one line).
void AddSummaryMenuAction(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const Ui::Menu::MenuCallback &addAction);

} // namespace Luckgram::AI
