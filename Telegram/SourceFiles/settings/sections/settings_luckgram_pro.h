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

// D12 (mobile 29 / S7): 「Luckgram 功能 → Luckgram Pro」. Status and AI
// credits, the live perks in their fixed order, plans paid with Telegram
// Stars (the invoice opens in the app), "link first" when this device isn't
// linked, 「暂不开放购买」 while billing.enabled is off. Same rows and texts
// as Android LuckgramProActivity.
class LuckgramPro : public Section<LuckgramPro> {
public:
	LuckgramPro(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~LuckgramPro();

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	struct State;

	void load(Fn<void()> then = nullptr);
	void rebuild();
	void checkout(int planIndex);
	void refreshAfterPurchase();

	const not_null<Window::SessionController*> _controller;
	const not_null<Ui::VerticalLayout*> _content;
	const std::unique_ptr<State> _state;

};

// D12 (mobile S6): 「Luckgram 功能 → 账号」. Link this device to the
// Telegram account through the bot (the t.me link opens the bot in the
// app, the user clicks Start), show the linked devices, unlink.
class LuckgramAccount : public Section<LuckgramAccount> {
public:
	LuckgramAccount(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~LuckgramAccount();

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	struct State;

	void load();
	void rebuild();
	void startLink();
	void poll();
	void unlink();

	const not_null<Window::SessionController*> _controller;
	const not_null<Ui::VerticalLayout*> _content;
	const std::unique_ptr<State> _state;

};

// The 「账号」 and 「Luckgram Pro」 rows at the top of 「账号与隐私」 on the
// Luckgram Features page (Pro only while billing.enabled, like mobile).
void AddLuckgramAccountRows(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller);

} // namespace Settings
