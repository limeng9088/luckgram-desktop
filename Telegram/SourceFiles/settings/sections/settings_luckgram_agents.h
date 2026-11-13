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

// The create / edit box (agentId empty: a new agent).
void ShowAgentEditBox(
	not_null<Window::SessionController*> controller,
	const QString &agentId = QString());

} // namespace Settings
