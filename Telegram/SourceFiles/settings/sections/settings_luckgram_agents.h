/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "settings/settings_common_session.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Settings {

// D7 需求 5 (mobile 22): the agents page, opened from 「智能体」 at the top
// of the AI assistant. 「热门智能体」 (the five presets), 「我的智能体」 (the
// user's own), 「创建你自己的智能体」. Click opens the agent's conversation;
// right-click (mobile: long press) offers 添加到聊天列表 / 编辑 / 删除.
class LuckgramAgents : public Section<LuckgramAgents> {
public:
	LuckgramAgents(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void rebuild();

	const not_null<Window::SessionController*> _controller;
	const not_null<Ui::VerticalLayout*> _content;

};

// D7 需求 5 (mobile 30): the agent market. Search, a category picker, the
// list (avatar, name, one line, users, 「Pro」), 「更多」 for the next page,
// the cached agents when offline. Click shows the details.
class LuckgramAgentMarket : public Section<LuckgramAgentMarket> {
public:
	LuckgramAgentMarket(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~LuckgramAgentMarket();

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	struct State;

	void load(bool more);
	void rebuildList();

	const not_null<Window::SessionController*> _controller;
	const not_null<Ui::VerticalLayout*> _content;
	const not_null<Ui::VerticalLayout*> _list;
	const std::unique_ptr<State> _state;

};

// The create / edit box (agentId empty: a new agent).
void ShowAgentEditBox(
	not_null<Window::SessionController*> controller,
	const QString &agentId = QString());

} // namespace Settings
