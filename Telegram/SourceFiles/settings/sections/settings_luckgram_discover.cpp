/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "settings/sections/settings_luckgram_discover.h"

#include "luckgram/discover/luckgram_discover_logic.h"
#include "luckgram/discover/luckgram_nearby_logic.h"
#include "luckgram/discover/luckgram_square_compose.h"
#include "luckgram/discover/luckgram_square_ui.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "settings/settings_common.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

#include <QtCore/QDateTime>

namespace Settings {
namespace {

namespace Discover = Luckgram::Discover;
namespace Square = Luckgram::Square;
namespace SquareUi = Luckgram::SquareUi;
namespace Api = Luckgram::Api;

[[nodiscard]] qint64 NowMs() {
	return QDateTime::currentMSecsSinceEpoch();
}

[[nodiscard]] bool NearbyEnabled() {
	return Luckgram::Settings::IsEnabled(
		QString::fromLatin1(Luckgram::Nearby::kFeatureKey));
}

[[nodiscard]] std::vector<Discover::Entry> Entries() {
	return Discover::VisibleEntries(SquareUi::FeatureEnabled(), NearbyEnabled());
}

[[nodiscard]] rpl::producer<> SettingsChanged() {
	return rpl::single(QString()) | rpl::then(
		Luckgram::Settings::Changes()
	) | rpl::to_empty;
}

[[nodiscard]] QString SavedKey(const QString &tab) {
	return u"square_saved_"_q + tab;
}

[[nodiscard]] QStringList Tabs() {
	return {
		QString::fromLatin1(Square::kTabLatest),
		QString::fromLatin1(Square::kTabHot),
		QString::fromLatin1(Square::kTabRecommend),
	};
}

} // namespace

LuckgramDiscover::LuckgramDiscover(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _controller(controller)
, _content(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	Ui::ResizeFitChild(this, _content);
	SettingsChanged() | rpl::on_next([=] {
		rebuild();
	}, lifetime());
}

rpl::producer<QString> LuckgramDiscover::title() {
	return Luckgram::TrValue("发现", "Discover");
}

void LuckgramDiscover::rebuild() {
	const auto texts = Discover::Texts(Luckgram::IsChinese());
	const auto container = _content.get();
	container->clear();
	Ui::AddSkip(container);
	const auto entries = Entries();
	for (const auto entry : entries) {
		const auto square = (entry == Discover::Entry::Square);
		AddButtonWithLabel(
			container,
			rpl::single(texts.title(entry)),
			rpl::single(texts.subtitle(entry)),
			st::settingsButton,
			{ square ? &st::menuIconDiscussion : &st::menuIconAddress }
		)->addClickHandler([=] {
			if (square) {
				_controller->showSettings(LuckgramSquare::Id());
			}
		});
	}
	if (entries.empty()) {
		Ui::AddDividerText(container, rpl::single(texts.emptyText()));
	}
	Ui::AddSkip(container);
	container->resizeToWidth(width());
}

struct LuckgramSquare::State {
	std::map<QString, Square::Feed> feeds;
	QString tab;
	Api::RequestPtr refreshRequest;
	Api::RequestPtr moreRequest;
	Ui::VerticalLayout *tabs = nullptr;
	Ui::VerticalLayout *list = nullptr;

	Square::Feed &feed() {
		return feeds.try_emplace(tab, tab).first->second;
	}
};

LuckgramSquare::LuckgramSquare(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _controller(controller)
, _content(Ui::CreateChild<Ui::VerticalLayout>(this))
, _state(std::make_unique<State>()) {
	Ui::ResizeFitChild(this, _content);
	_state->tabs = _content->add(object_ptr<Ui::VerticalLayout>(_content));
	_state->list = _content->add(object_ptr<Ui::VerticalLayout>(_content));

	SquareUi::PrefetchMe(controller);

	SquareUi::PostUpdated() | rpl::on_next([=](const Square::Post &post) {
		for (auto &[tab, feed] : _state->feeds) {
			feed.mergeSilently({ post });
		}
	}, lifetime());
	SquareUi::PostCreated() | rpl::on_next([=](const Square::Post &post) {
		auto &latest = _state->feeds.try_emplace(
			QString::fromLatin1(Square::kTabLatest),
			QString::fromLatin1(Square::kTabLatest)).first->second;
		latest.posts.insert(latest.posts.begin(), post);
		showTab(QString::fromLatin1(Square::kTabLatest));
	}, lifetime());
	SquareUi::PostRemoved() | rpl::on_next([=](const QString &id) {
		auto changed = false;
		for (auto &[tab, feed] : _state->feeds) {
			changed = feed.removePost(id) || changed;
		}
		if (changed) {
			rebuildList();
		}
	}, lifetime());
	SquareUi::AuthorBlocked() | rpl::on_next([=](const QString &id) {
		auto removed = 0;
		for (auto &[tab, feed] : _state->feeds) {
			removed += feed.removeAuthor(id);
		}
		if (removed) {
			rebuildList();
		}
	}, lifetime());
	Luckgram::Settings::Changes() | rpl::filter([](const QString &key) {
		return key.isEmpty()
			|| key == QString::fromLatin1(Square::kFeatureKey);
	}) | rpl::on_next([=] {
		rebuildList();
	}, lifetime());

	showTab(QString::fromLatin1(Square::kTabLatest));
}

LuckgramSquare::~LuckgramSquare() {
	for (auto &[tab, feed] : _state->feeds) {
		feed.markSeen(NowMs());
	}
	if (const auto request = base::take(_state->refreshRequest)) {
		request->cancel();
	}
	if (const auto request = base::take(_state->moreRequest)) {
		request->cancel();
	}
}

rpl::producer<QString> LuckgramSquare::title() {
	return rpl::single(SquareUi::Tx().title());
}

void LuckgramSquare::showTab(const QString &tab) {
	if (!_state->tab.isEmpty()) {
		_state->feed().markSeen(NowMs());
	}
	if (const auto request = base::take(_state->refreshRequest)) {
		request->cancel();
		_state->feed().cancelLoading();
	}
	if (const auto request = base::take(_state->moreRequest)) {
		request->cancel();
		_state->feed().cancelLoading();
	}
	_state->tab = tab;
	auto &feed = _state->feed();
	if (!feed.loadedOnce) {
		const auto saved = Square::DecodeSaved(
			Luckgram::Settings::GetString(SavedKey(tab)),
			NowMs());
		if (saved) {
			feed.restore(saved->page, saved->savedAtMs);
		}
	}
	rebuildTabs();
	rebuildList();
	if (feed.isStale(NowMs())) {
		refresh(false);
	}
}

void LuckgramSquare::refresh(bool manual) {
	if (!SquareUi::FeatureEnabled()) {
		return;
	}
	auto &feed = _state->feed();
	if (!feed.beginRefresh()) {
		return;
	}
	const auto tab = _state->tab;
	const auto wasLoaded = feed.loadedOnce;
	rebuildList();
	_state->refreshRequest = Api::Send({
		.path = Square::FeedPath(tab, QString()),
	}, crl::guard(this, [=](Api::Response response) {
		_state->refreshRequest = nullptr;
		const auto json = response.json();
		auto &feed = _state->feeds.try_emplace(tab, tab).first->second;
		const auto fresh = feed.applyRefresh(Square::ParsePosts(json), NowMs());
		Luckgram::Settings::SetString(
			SavedKey(tab),
			Square::EncodeSaved(json, NowMs()));
		if (tab == _state->tab) {
			rebuildList();
			if (manual && wasLoaded) {
				_controller->showToast(SquareUi::Tx().refreshed(fresh));
			}
		}
	}), crl::guard(this, [=](Api::Error error) {
		_state->refreshRequest = nullptr;
		_state->feeds.try_emplace(tab, tab).first->second.failRefresh();
		if (tab == _state->tab) {
			rebuildList();
		}
		SquareUi::ShowError(_controller, error);
	}));
}

void LuckgramSquare::loadMore() {
	if (!SquareUi::FeatureEnabled()) {
		return;
	}
	auto &feed = _state->feed();
	if (!feed.canLoadMore()) {
		return;
	}
	const auto tab = _state->tab;
	const auto generation = feed.beginMore();
	rebuildList();
	_state->moreRequest = Api::Send({
		.path = Square::FeedPath(tab, feed.nextCursor),
	}, crl::guard(this, [=](Api::Response response) {
		_state->moreRequest = nullptr;
		auto &feed = _state->feeds.try_emplace(tab, tab).first->second;
		if (feed.applyMore(Square::ParsePosts(response.json()), generation)
			&& tab == _state->tab) {
			rebuildList();
		}
	}), crl::guard(this, [=](Api::Error error) {
		_state->moreRequest = nullptr;
		_state->feeds.try_emplace(tab, tab).first->second.failMore(generation);
		if (tab == _state->tab) {
			rebuildList();
		}
		SquareUi::ShowError(_controller, error);
	}));
}

void LuckgramSquare::rebuildTabs() {
	const auto container = _state->tabs;
	container->clear();
	const auto tx = SquareUi::Tx();
	const auto row = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::boxRowPadding);
	row->resize(row->width(), st::semiboldFont->height + style::ConvertScale(16));
	auto left = 0;
	for (const auto &tab : Tabs()) {
		const auto title = (tab == QString::fromLatin1(Square::kTabHot))
			? tx.hot()
			: (tab == QString::fromLatin1(Square::kTabRecommend))
			? tx.recommend()
			: tx.latest();
		if (tab == _state->tab) {
			const auto label = Ui::CreateChild<Ui::FlatLabel>(
				row,
				title,
				st::boxLabel);
			label->resizeToNaturalWidth(style::ConvertScale(200));
			label->moveToLeft(left, style::ConvertScale(8));
			left += label->width() + style::ConvertScale(20);
			continue;
		}
		const auto button = Ui::CreateChild<Ui::LinkButton>(row, title);
		button->moveToLeft(left, style::ConvertScale(8));
		left += button->width() + style::ConvertScale(20);
		button->setClickedCallback([=] {
			crl::on_main(this, [=] { showTab(tab); });
		});
	}
	const auto compose = Ui::CreateChild<Ui::LinkButton>(
		row,
		tx.compose());
	compose->setClickedCallback([=] {
		Luckgram::SquareCompose::Open(_controller);
	});
	const auto refreshButton = Ui::CreateChild<Ui::LinkButton>(
		row,
		tx.refresh());
	refreshButton->setClickedCallback([=] { refresh(true); });
	const auto profile = Ui::CreateChild<Ui::LinkButton>(
		row,
		tx.editProfile());
	profile->setClickedCallback([=] {
		SquareUi::EditProfile(_controller);
	});
	row->widthValue() | rpl::on_next([=](int width) {
		refreshButton->moveToRight(0, style::ConvertScale(8), width);
		profile->moveToRight(
			refreshButton->width() + style::ConvertScale(20),
			style::ConvertScale(8),
			width);
		compose->moveToRight(
			refreshButton->width()
				+ profile->width()
				+ style::ConvertScale(40),
			style::ConvertScale(8),
			width);
	}, row->lifetime());
	Ui::AddDivider(container);
	container->resizeToWidth(width());
}

void LuckgramSquare::rebuildList() {
	const auto container = _state->list;
	container->clear();
	const auto tx = SquareUi::Tx();
	if (!SquareUi::FeatureEnabled()) {
		Ui::AddDividerText(container, rpl::single(tx.squareDisabled()));
		container->resizeToWidth(width());
		return;
	}
	auto &feed = _state->feed();
	Ui::AddSkip(container);
	for (const auto &post : feed.posts) {
		container->add(
			SquareUi::CreatePostWidget(container, _controller, post, false),
			st::boxRowPadding);
	}
	const auto addLabel = [&](const QString &text) {
		container->add(
			object_ptr<Ui::FlatLabel>(container, text, st::boxDividerLabel),
			st::boxRowPadding);
	};
	const auto addLink = [&](const QString &text, Fn<void()> callback) {
		const auto link = container->add(
			object_ptr<Ui::LinkButton>(container, text),
			st::boxRowPadding);
		link->setClickedCallback(std::move(callback));
	};
	switch (feed.footer()) {
	case Square::Feed::Footer::Skeleton:
		addLabel(tx.loading());
		break;
	case Square::Feed::Footer::FirstError:
		addLabel(tx.loadFailed());
		addLink(tx.retry(), [=] { refresh(true); });
		break;
	case Square::Feed::Footer::Empty:
		addLabel(tx.empty());
		break;
	case Square::Feed::Footer::LoadingMore:
		if (feed.loadingMore) {
			addLabel(tx.loading());
		} else {
			addLink(tx.loadMore(), [=] { loadMore(); });
		}
		break;
	case Square::Feed::Footer::MoreError:
		addLink(tx.moreFailed(), [=] {
			_state->feed().moreFailed = false;
			loadMore();
		});
		break;
	case Square::Feed::Footer::End:
		addLabel(tx.noMore());
		break;
	}
	Ui::AddSkip(container);
	container->resizeToWidth(width());
}

void LuckgramSquare::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	// Next page a few posts before the end (Feed::kPrefetch on mobile).
	const auto &feed = _state->feed();
	if (!feed.canLoadMore() || feed.moreFailed) {
		return;
	}
	const auto threshold = style::ConvertScale(800);
	if (visibleBottom + threshold >= height()) {
		crl::on_main(this, [=] { loadMore(); });
	}
}

void AddLuckgramDiscoverMenuItem(
		not_null<Ui::VerticalLayout*> menu,
		not_null<Window::SessionController*> controller) {
	const auto wrap = menu->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			menu,
			CreateButtonWithIcon(
				menu,
				Luckgram::TrValue("发现", "Discover"),
				st::mainMenuButton,
				{ &st::menuIconDiscover })));
	wrap->toggleOn(SettingsChanged() | rpl::map([] {
		return !Entries().empty();
	}) | rpl::distinct_until_changed());
	wrap->finishAnimating();
	wrap->entity()->setClickedCallback([=] {
		controller->showSettings(LuckgramDiscover::Id());
	});
}

} // namespace Settings
