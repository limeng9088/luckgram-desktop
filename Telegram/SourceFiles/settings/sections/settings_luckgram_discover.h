/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "settings/settings_common_session.h"

#include "luckgram/discover/luckgram_nearby_logic.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Settings {

// D10 (mobile 27): 「发现」 — the Square and People Nearby rows, each only
// while its switch is on; 「暂时没有可用的功能」 when none is.
class LuckgramDiscover : public Section<LuckgramDiscover> {
public:
	LuckgramDiscover(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void rebuild();

	const not_null<Window::SessionController*> _controller;
	const not_null<Ui::VerticalLayout*> _content;

};

// D10 (mobile 26): the Square feed — 最新 / 热门 / 推荐 tabs, refresh,
// paging, the saved first page shown at once, posts with likes, comments,
// report, block and delete. Browsing needs nothing; writes link silently.
class LuckgramSquare : public Section<LuckgramSquare> {
public:
	LuckgramSquare(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~LuckgramSquare();

	[[nodiscard]] rpl::producer<QString> title() override;

protected:
	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;

private:
	struct State;

	void showTab(const QString &tab);
	void refresh(bool manual);
	void loadMore();
	void rebuildTabs();
	void rebuildList();

	const not_null<Window::SessionController*> _controller;
	const not_null<Ui::VerticalLayout*> _content;
	const std::unique_ptr<State> _state;

};

// D10 (mobile 25): 「附近的人」. Username first, the intro once, the silent
// link, one position per open / refresh / renew (system location service,
// or a place picked on the map; only ~1 km is ever sent), the list grouped
// by distance, the card ("在附近可见" for 24 hours, renew, bio, photo),
// report, "don't show this person", the blocked people.
class LuckgramNearby : public Section<LuckgramNearby> {
public:
	LuckgramNearby(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~LuckgramNearby();

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	struct State;

	void connectService();
	void refresh();
	void locate(Fn<void()> then);
	void pickOnMap();
	void loadList();
	// Not setVisible(): that one is RpWidget's (final).
	void setNearbyVisible(bool visible);
	void uploadCard();
	void editBio();
	void showPerson(const Luckgram::Nearby::Person &person);
	void rebuild();

	const not_null<Window::SessionController*> _controller;
	const not_null<Ui::VerticalLayout*> _content;
	const std::unique_ptr<State> _state;

};

// D10 (mobile BlockedUsersActivity): the people blocked in People Nearby
// and Square (one list, /v1/blocks), names from the local cache, unblock.
class LuckgramBlocked : public Section<LuckgramBlocked> {
public:
	LuckgramBlocked(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~LuckgramBlocked();

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	struct State;

	void load();
	void rebuild();

	const not_null<Window::SessionController*> _controller;
	const not_null<Ui::VerticalLayout*> _content;
	const std::unique_ptr<State> _state;

};

// The 「发现」 item of the main menu (one line in window_main_menu.cpp),
// shown while Discover has a row; then 「扫一扫」 and 「代理配置」.
void AddLuckgramDiscoverMenuItem(
	not_null<Ui::VerticalLayout*> menu,
	not_null<Window::SessionController*> controller);

} // namespace Settings
