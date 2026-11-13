/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "settings/sections/settings_luckgram_discover.h"

#include "boxes/username_box.h"
#include "core/current_geo_location.h"
#include "data/data_changes.h"
#include "data/data_location.h"
#include "data/data_user.h"
#include "luckgram/discover/luckgram_nearby_state.h"
#include "luckgram/discover/luckgram_silent_link.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/location_picker.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/userpic_view.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_session_controller_link_info.h"
#include "luckgram/discover/luckgram_discover_logic.h"
#include "luckgram/discover/luckgram_discover_storage.h"
#include "luckgram/discover/luckgram_nearby_logic.h"
#include "luckgram/discover/luckgram_scan_ui.h"
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

#include <QtCore/QBuffer>
#include <QtCore/QDateTime>
#include <QtCore/QJsonDocument>
#include <QtCore/QLocale>

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

[[nodiscard]] QString LegacySavedKey(const QString &tab) {
	return u"square_saved_"_q + tab; // Plain tdata/luckgram.json, pre-D2.
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
			_controller->showSettings(square
				? LuckgramSquare::Id()
				: LuckgramNearby::Id());
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
			Luckgram::DiscoverStorage::Read(
				&_controller->session().account(),
				Luckgram::DiscoverStorage::SquareSaved(tab),
				LegacySavedKey(tab)),
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
		Luckgram::DiscoverStorage::Write(
			&_controller->session().account(),
			Luckgram::DiscoverStorage::SquareSaved(tab),
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

namespace {

namespace Nearby = Luckgram::Nearby;
namespace NearbyState = Luckgram::NearbyState;
namespace Blocks = Luckgram::Blocks;

[[nodiscard]] Nearby::Texts NearbyTx() {
	return Nearby::Texts(Luckgram::IsChinese());
}

[[nodiscard]] QString MyUsername(not_null<Window::SessionController*> controller) {
	return controller->session().user()->username();
}

[[nodiscard]] QByteArray JsonBytes(const QJsonObject &o) {
	return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

[[nodiscard]] QString NearbyErrorText(const Api::Error &error) {
	const auto tx = NearbyTx();
	if (error.code == u"rate_limited"_q) {
		return tx.rateLimited(error.retryAfterSeconds);
	} else if (error.code == u"banned"_q) {
		return tx.banned();
	} else if (error.code == u"nearby_disabled"_q) {
		return tx.disabled();
	} else if (error.code == u"not_linked"_q) {
		return tx.connectFailed();
	}
	return error.text();
}

// The account's own photo, small, as base64 JPEG (25 / S10: <= 256 KB),
// only when the real photo is loaded; nullopt = leave it as it is.
[[nodiscard]] std::optional<QString> AvatarBase64(not_null<UserData*> user) {
	auto view = user->createUserpicView();
	if (!user->hasUserpic() || user->useEmptyUserpic(view)) {
		return QString();
	} else if (!view.cloud || view.cloud->isNull()) {
		user->loadUserpic();
		return std::nullopt;
	}
	const auto image = view.cloud->scaled(
		160,
		160,
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
	auto bytes = QByteArray();
	auto buffer = QBuffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.convertToFormat(QImage::Format_RGB32).save(&buffer, "JPEG", 85);
	if (bytes.isEmpty() || bytes.size() > Nearby::kAvatarMaxBytes * 3 / 4) {
		return std::nullopt;
	}
	return QString::fromLatin1(bytes.toBase64());
}

[[nodiscard]] Ui::LocationPickerConfig MapsConfig(
		not_null<Main::Session*> session) {
	auto map = session->appConfig().get<base::flat_map<QString, QString>>(
		u"tdesktop_config_map"_q,
		base::flat_map<QString, QString>());
	return {
		.mapsToken = map[u"maps"_q],
		.geoToken = map[u"geo"_q],
	};
}

void PersonBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Nearby::Person person,
		Fn<void()> hidden) {
	const auto tx = NearbyTx();
	box->setTitle(rpl::single(person.displayName));
	auto text = Ui::Text::Bold(person.displayName);
	text.append(u"\n@"_q + person.username);
	if (!person.bio.isEmpty()) {
		text.append(u"\n"_q + person.bio);
	}
	text.append(u"\n"_q + tx.bucket(person.bucket));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(text),
		st::boxLabel));
	box->addButton(rpl::single(tx.sendMessage()), [=] {
		box->closeBox();
		controller->showPeerByLink({ .usernameOrId = person.username });
	});
	box->addLeftButton(rpl::single(tx.report()), [=] {
		box->closeBox();
		controller->show(Box([=](not_null<Ui::GenericBox*> reasons) {
			const auto tx = NearbyTx();
			reasons->setTitle(rpl::single(tx.reportTitle()));
			const auto container = reasons->verticalLayout();
			for (const auto &reason : Nearby::ReportReasons()) {
				const auto button = container->add(
					object_ptr<Ui::SettingsButton>(
						container,
						rpl::single(tx.reportReason(reason)),
						st::settingsButtonNoIcon));
				button->setClickedCallback([=] {
					reasons->closeBox();
					const auto weak = base::make_weak(controller);
					Api::Send({
						.method = "POST",
						.path = u"/v1/nearby/report"_q,
						.body = JsonBytes(Nearby::ReportBody(person.id, reason)),
						.contentType = "application/json",
					}, [=](Api::Response) {
						if (const auto strong = weak.get()) {
							strong->showToast(NearbyTx().reported());
						}
					}, [=](Api::Error error) {
						if (const auto strong = weak.get()) {
							strong->showToast(NearbyErrorText(error));
						}
					});
				});
			}
			reasons->addButton(rpl::single(tx.cancel()), [=] {
				reasons->closeBox();
			});
		}));
	});
	box->addLeftButton(rpl::single(tx.hide()), [=] {
		box->closeBox();
		controller->show(Ui::MakeConfirmBox({
			.text = NearbyTx().hideConfirm(person.displayName),
			.confirmed = [=](Fn<void()> close) {
				close();
				const auto weak = base::make_weak(controller);
				Api::Send({
					.method = "POST",
					.path = QString::fromLatin1(Blocks::kPath),
					.body = JsonBytes(Blocks::BlockBody(person.id)),
					.contentType = "application/json",
				}, [=](Api::Response) {
					if (const auto strong = weak.get()) {
						Luckgram::SquareUi::PutBlockedName(
							&strong->session().account(),
							person.id,
							person.displayName);
					}
					if (hidden) {
						hidden();
					}
					if (const auto strong = weak.get()) {
						strong->showToast(NearbyTx().hidden());
					}
				}, [=](Api::Error error) {
					if (const auto strong = weak.get()) {
						strong->showToast(NearbyErrorText(error));
					}
				});
			},
			.confirmText = NearbyTx().hide(),
			.confirmStyle = &st::attentionBoxButton,
		}));
	});
}

void BioBox(not_null<Ui::GenericBox*> box, Fn<void(QString)> save) {
	const auto tx = NearbyTx();
	box->setTitle(rpl::single(tx.bio()));
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine,
		rpl::single(tx.bioHint()),
		NearbyState::Bio()));
	field->setMaxLength(Nearby::kBioMax);
	box->setFocusCallback([=] { field->setFocusFast(); });
	box->addButton(rpl::single(tx.save()), [=] {
		const auto bio = field->getLastText();
		box->closeBox();
		save(bio);
	});
	box->addButton(rpl::single(tx.cancel()), [=] { box->closeBox(); });
}

} // namespace

struct LuckgramNearby::State {
	bool connecting = false;
	std::optional<bool> linked;
	bool relinkTried = false;
	bool locating = false;
	bool loadingList = false;
	bool busy = false;
	QString locationError;
	bool offerMap = false;
	std::optional<QPointF> point;
	bool manual = false;
	std::optional<std::vector<Nearby::Person>> people;
	Api::RequestPtr listRequest;
	base::weak_ptr<Ui::LocationPicker> picker;
};

LuckgramNearby::LuckgramNearby(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _controller(controller)
, _content(Ui::CreateChild<Ui::VerticalLayout>(this))
, _state(std::make_unique<State>()) {
	Ui::ResizeFitChild(this, _content);
	controller->session().user()->loadUserpic();
	NearbyState::Changes() | rpl::on_next([=] {
		rebuild();
	}, lifetime());
	Luckgram::Settings::Changes() | rpl::filter([](const QString &key) {
		return key.isEmpty()
			|| key == QString::fromLatin1(Nearby::kFeatureKey);
	}) | rpl::on_next([=] {
		rebuild();
	}, lifetime());
	rebuild();
	connectService();
}

LuckgramNearby::~LuckgramNearby() {
	if (const auto request = base::take(_state->listRequest)) {
		request->cancel();
	}
	if (const auto picker = _state->picker.get()) {
		picker->close();
	}
}

rpl::producer<QString> LuckgramNearby::title() {
	return rpl::single(NearbyTx().title());
}

void LuckgramNearby::connectService() {
	if (!NearbyState::FeatureEnabled()
		|| _state->connecting
		|| Nearby::Check(MyUsername(_controller)) != Nearby::Precondition::Ready
		|| !NearbyState::IntroDone()) {
		return;
	}
	_state->connecting = true;
	_state->linked = std::nullopt;
	rebuild();
	const auto session = &_controller->session();
	const auto show = _controller->uiShow();
	Luckgram::SilentLink::Refresh(session, crl::guard(this, [=] {
		if (Luckgram::SilentLink::Cached(session)
			== Luckgram::SilentLink::State::NotLinked) {
			NearbyState::ClearLocal(); // The card went with the link.
		}
		Luckgram::SilentLink::Ensure(session, show, crl::guard(this, [=](
				bool ok) {
			_state->connecting = false;
			_state->linked = ok;
			rebuild();
			if (!ok) {
				return;
			}
			Api::Send({
				.path = u"/v1/nearby/me"_q,
			}, crl::guard(this, [=](Api::Response response) {
				NearbyState::ApplyState(Nearby::ParseMe(
					response.json(),
					QDateTime::currentMSecsSinceEpoch()));
			}), crl::guard(this, [=](Api::Error error) {
				NearbyState::OnError(error);
			}));
			refresh();
		}));
	}));
}

void LuckgramNearby::refresh() {
	if (_state->locating || _state->linked != true) {
		return;
	}
	locate([=] { loadList(); });
}

void LuckgramNearby::locate(Fn<void()> then) {
	_state->locationError = QString();
	_state->offerMap = false;
	if (const auto manual = NearbyState::ManualLocation()) {
		_state->point = manual;
		_state->manual = true;
		then();
		return;
	}
	_state->locating = true;
	rebuild();
	Core::ResolveCurrentGeoLocation(crl::guard(this, [=](
			Core::GeoLocation location) {
		_state->locating = false;
		const auto lat = location.point.x();
		const auto lon = location.point.y();
		if (!location.exact() || !Nearby::IsValidLocation(lat, lon)) {
			// Country-wide or nothing: no use for ~1 km groups.
			_state->point = std::nullopt;
			_state->locationError = NearbyTx().systemLocationFailed();
			_state->offerMap = true;
			rebuild();
			return;
		}
		// Only the coarsened value is kept, in memory too.
		_state->point = QPointF(Nearby::Coarsen(lat), Nearby::Coarsen(lon));
		_state->manual = false;
		then();
	}));
}

void LuckgramNearby::pickOnMap() {
	const auto session = &_controller->session();
	const auto config = MapsConfig(session);
	if (!Ui::LocationPicker::Available(config)) {
		_controller->showToast(NearbyTx().mapUnavailable());
		return;
	} else if (const auto picker = _state->picker.get()) {
		picker->activate();
		return;
	}
	auto initial = Core::ResolveCurrentCountryLocation();
	if (const auto manual = NearbyState::ManualLocation()) {
		initial = Core::GeoLocation{
			.point = *manual,
			.accuracy = Core::GeoLocationAccuracy::Exact,
		};
	}
	_state->picker = base::make_weak(Ui::LocationPicker::Show({
		.parent = _controller->widget(),
		.config = config,
		.chooseLabel = rpl::single(NearbyTx().useThisLocation()),
		.initial = initial,
		.callback = crl::guard(this, [=](Data::LocationPoint point, QString) {
			NearbyState::SetManualLocation(QPointF(point.lat(), point.lon()));
			refresh();
		}),
		.quit = [] {},
		.storageId = session->local().resolveStorageIdBots(),
		.closeRequests = rpl::never<>(),
	}).get());
}

void LuckgramNearby::loadList() {
	if (!_state->point) {
		return;
	}
	_state->loadingList = true;
	rebuild();
	if (const auto request = base::take(_state->listRequest)) {
		request->cancel();
	}
	_state->listRequest = Api::Send({
		.path = Nearby::QueryPath(_state->point->x(), _state->point->y()),
	}, crl::guard(this, [=](Api::Response response) {
		_state->listRequest = nullptr;
		_state->loadingList = false;
		_state->people = Nearby::ParseList(
			response.json(),
			MyUsername(_controller));
		rebuild();
	}), crl::guard(this, [=](Api::Error error) {
		_state->listRequest = nullptr;
		_state->loadingList = false;
		NearbyState::OnError(error);
		if (error.code == u"not_linked"_q && !_state->relinkTried) {
			_state->relinkTried = true;
			_state->linked = std::nullopt;
			connectService();
			return;
		}
		_controller->showToast(NearbyErrorText(error));
		rebuild();
	}));
}

void LuckgramNearby::setVisible(bool visible) {
	if (_state->busy) {
		return;
	} else if (!visible) {
		_state->busy = true;
		NearbyState::TurnOff(crl::guard(this, [=] {
			_state->busy = false;
			rebuild();
		}), crl::guard(this, [=](Api::Error error) {
			_state->busy = false;
			_controller->showToast(NearbyErrorText(error));
			rebuild();
		}));
		return;
	}
	// A fresh position for every upload (on / renew).
	locate([=] { uploadCard(); });
}

void LuckgramNearby::uploadCard() {
	const auto user = _controller->session().user();
	const auto username = MyUsername(_controller);
	if (username.isEmpty() || !_state->point) {
		return;
	}
	const auto body = Nearby::CardBody(
		Nearby::CardName(user->firstName, user->lastName, username),
		NearbyState::Bio(),
		username,
		_state->point->x(),
		_state->point->y(),
		NearbyState::ShowPhoto() ? AvatarBase64(user) : QString());
	const auto wasVisible = NearbyState::IsVisible();
	_state->busy = true;
	rebuild();
	Api::Send({
		.method = "PUT",
		.path = u"/v1/nearby/me"_q,
		.body = JsonBytes(body),
		.contentType = "application/json",
	}, crl::guard(this, [=](Api::Response response) {
		_state->busy = false;
		auto json = response.json();
		if (!json.contains(u"visible"_q)) {
			json.insert(u"visible"_q, true);
		}
		const auto state = Nearby::ParseMe(
			json,
			QDateTime::currentMSecsSinceEpoch());
		NearbyState::ApplyState(state);
		if (!state.visible) {
			_controller->showToast(NearbyTx().underReview());
		} else if (wasVisible) {
			_controller->showToast(NearbyTx().renewed());
		}
		loadList();
	}), crl::guard(this, [=](Api::Error error) {
		_state->busy = false;
		NearbyState::OnError(error);
		_controller->showToast(NearbyErrorText(error));
		rebuild();
	}));
}

void LuckgramNearby::editBio() {
	_controller->show(Box(BioBox, crl::guard(this, [=](QString bio) {
		NearbyState::SetBio(bio);
		_controller->showToast(NearbyTx().cardSaved());
		if (NearbyState::IsVisible()) {
			setVisible(true);
		}
	})));
}

void LuckgramNearby::showPerson(const Nearby::Person &person) {
	const auto id = person.id;
	_controller->show(Box(
		PersonBox,
		_controller,
		person,
		crl::guard(this, [=] {
			if (_state->people) {
				auto &list = *_state->people;
				list.erase(std::remove_if(list.begin(), list.end(), [&](
						const Nearby::Person &p) {
					return p.id == id;
				}), list.end());
				rebuild();
			}
		})));
}

void LuckgramNearby::rebuild() {
	const auto tx = NearbyTx();
	const auto container = _content.get();
	container->clear();
	Ui::AddSkip(container);
	const auto action = [&](const QString &text, Fn<void()> callback) {
		AddButtonWithIcon(
			container,
			rpl::single(text),
			st::settingsButtonNoIcon
		)->addClickHandler(std::move(callback));
	};
	const auto finish = [&] {
		Ui::AddSkip(container);
		container->resizeToWidth(width());
	};
	if (!NearbyState::FeatureEnabled()) {
		Ui::AddDividerText(container, rpl::single(tx.disabled()));
		return finish();
	}
	if (Nearby::Check(MyUsername(_controller))
			== Nearby::Precondition::NeedUsername) {
		Ui::AddSubsectionTitle(container, rpl::single(tx.needUsernameTitle()));
		Ui::AddDividerText(container, rpl::single(tx.needUsernameText()));
		action(tx.goUsername(), [=] {
			_controller->show(Box(UsernamesBox, _controller->session().user()));
		});
		_controller->session().changes().peerUpdates(
			_controller->session().user(),
			Data::PeerUpdate::Flag::Username
		) | rpl::take(1) | rpl::on_next([=] {
			crl::on_main(this, [=] {
				rebuild();
				connectService();
			});
		}, container->lifetime());
		return finish();
	}
	if (!NearbyState::IntroDone()) {
		Ui::AddSubsectionTitle(container, rpl::single(tx.introTitle()));
		Ui::AddDividerText(container, rpl::single(tx.introText()));
		action(tx.introContinue(), [=] {
			NearbyState::SetIntroDone();
			connectService();
		});
		return finish();
	}
	if (_state->connecting || !_state->linked) {
		Ui::AddDividerText(container, rpl::single(tx.connecting()));
		return finish();
	} else if (!*_state->linked) {
		Ui::AddDividerText(container, rpl::single(tx.connectFailed()));
		action(tx.refresh(), [=] { connectService(); });
		return finish();
	}

	// The card.
	Ui::AddSubsectionTitle(container, rpl::single(tx.myCard()));
	const auto visible = NearbyState::IsVisible();
	const auto toggle = AddButtonWithIcon(
		container,
		rpl::single(tx.visibleSwitch()),
		st::settingsButtonNoIcon);
	toggle->toggleOn(rpl::single(visible));
	toggle->toggledChanges(
	) | rpl::filter([=](bool on) {
		return on != NearbyState::IsVisible();
	}) | rpl::on_next([=](bool on) {
		crl::on_main(this, [=] { setVisible(on); });
	}, toggle->lifetime());
	if (visible) {
		action(tx.renew(), [=] { setVisible(true); });
	}
	const auto bio = NearbyState::Bio();
	AddButtonWithLabel(
		container,
		rpl::single(tx.bio()),
		rpl::single(bio.isEmpty() ? tx.bioEmpty() : bio),
		st::settingsButtonNoIcon
	)->addClickHandler([=] { editBio(); });
	const auto photo = AddButtonWithIcon(
		container,
		rpl::single(tx.showPhoto()),
		st::settingsButtonNoIcon);
	photo->toggleOn(rpl::single(NearbyState::ShowPhoto()));
	photo->toggledChanges(
	) | rpl::filter([=](bool on) {
		return on != NearbyState::ShowPhoto();
	}) | rpl::on_next([=](bool on) {
		crl::on_main(this, [=] {
			NearbyState::SetShowPhoto(on);
			if (NearbyState::IsVisible()) {
				setVisible(true);
			}
		});
	}, photo->lifetime());
	action(Blocks::Texts(Luckgram::IsChinese()).title(), [=] {
		_controller->showSettings(LuckgramBlocked::Id());
	});
	Ui::AddDividerText(container, rpl::single(visible
		? tx.visibleUntil(QLocale().toString(
			QDateTime::fromMSecsSinceEpoch(NearbyState::ExpiresAt()),
			QLocale::ShortFormat))
		: tx.visibleOffInfo()));
	Ui::AddSkip(container);

	// Where the position comes from (desktop).
	if (_state->point) {
		Ui::AddDividerText(container, rpl::single(_state->manual
			? tx.sourceManual()
			: tx.sourceSystem()));
	}
	action(tx.pickOnMap(), [=] { pickOnMap(); });
	if (NearbyState::ManualLocation()) {
		action(tx.useSystemLocation(), [=] {
			NearbyState::SetManualLocation(std::nullopt);
			refresh();
		});
	}
	action(tx.refresh(), [=] { refresh(); });
	Ui::AddSkip(container);

	// The list.
	Ui::AddSubsectionTitle(container, rpl::single(tx.listHeader()));
	if (!_state->locationError.isEmpty()) {
		Ui::AddDividerText(container, rpl::single(_state->locationError));
		return finish();
	} else if (_state->locating
		|| _state->loadingList
		|| !_state->people) {
		Ui::AddDividerText(container, rpl::single(tx.loading()));
		return finish();
	} else if (_state->people->empty()) {
		Ui::AddDividerText(container, rpl::single(tx.empty()));
		return finish();
	}
	for (const auto &[bucket, people] : Nearby::Group(*_state->people)) {
		Ui::AddSubsectionTitle(container, rpl::single(tx.bucket(bucket)));
		for (const auto &person : people) {
			AddButtonWithLabel(
				container,
				rpl::single(person.displayName),
				rpl::single(person.bio),
				st::settingsButtonNoIcon
			)->addClickHandler([=] { showPerson(person); });
		}
	}
	finish();
}

struct LuckgramBlocked::State {
	QStringList ids;
	QString cursor;
	bool loading = false;
	bool loaded = false;
	bool failed = false;
	Api::RequestPtr request;
};

LuckgramBlocked::LuckgramBlocked(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _controller(controller)
, _content(Ui::CreateChild<Ui::VerticalLayout>(this))
, _state(std::make_unique<State>()) {
	Ui::ResizeFitChild(this, _content);
	rebuild();
	load();
}

LuckgramBlocked::~LuckgramBlocked() {
	if (const auto request = base::take(_state->request)) {
		request->cancel();
	}
}

rpl::producer<QString> LuckgramBlocked::title() {
	return rpl::single(Blocks::Texts(Luckgram::IsChinese()).title());
}

void LuckgramBlocked::load() {
	if (_state->loading) {
		return;
	}
	_state->loading = true;
	_state->failed = false;
	_state->request = Api::Send({
		.path = Blocks::ListPath(_state->cursor),
	}, crl::guard(this, [=](Api::Response response) {
		const auto json = response.json();
		for (const auto &id : Blocks::ParseIds(json)) {
			if (!_state->ids.contains(id)) {
				_state->ids.push_back(id);
			}
		}
		_state->cursor = Blocks::NextCursor(json);
		_state->loading = false;
		_state->loaded = true;
		_state->request = nullptr;
		rebuild();
	}), crl::guard(this, [=](Api::Error) {
		_state->loading = false;
		_state->failed = true;
		_state->request = nullptr;
		rebuild();
	}));
}

void LuckgramBlocked::rebuild() {
	const auto tx = Blocks::Texts(Luckgram::IsChinese());
	const auto container = _content.get();
	container->clear();
	Ui::AddSkip(container);
	const auto cache = Luckgram::SquareUi::BlockedNames(
		&_controller->session().account());
	for (const auto &id : _state->ids) {
		const auto known = Blocks::Name(cache, id);
		const auto name = known.isEmpty() ? tx.unknownUser() : known;
		AddButtonWithLabel(
			container,
			rpl::single(name),
			rpl::single(tx.unblock()),
			st::settingsButtonNoIcon
		)->addClickHandler([=] {
			_controller->show(Ui::MakeConfirmBox({
				.text = Blocks::Texts(Luckgram::IsChinese()).unblockConfirm(
					name),
				.confirmed = [=](Fn<void()> close) {
					close();
					Api::Send({
						.method = "DELETE",
						.path = Blocks::DeletePath(id),
					}, crl::guard(this, [=](Api::Response) {
						Luckgram::SquareUi::RemoveBlockedName(
							&_controller->session().account(),
							id);
						_state->ids.removeAll(id);
						rebuild();
						_controller->showToast(
							Blocks::Texts(Luckgram::IsChinese()).unblocked());
					}), crl::guard(this, [=](Api::Error error) {
						_controller->showToast(error.text());
					}));
				},
				.confirmText = Blocks::Texts(Luckgram::IsChinese()).unblock(),
			}));
		});
	}
	if (_state->failed) {
		Ui::AddDividerText(container, rpl::single(tx.connectFailed()));
		AddButtonWithIcon(
			container,
			rpl::single(tx.retry()),
			st::settingsButtonNoIcon
		)->addClickHandler([=] { load(); });
	} else if (_state->loading && _state->ids.isEmpty()) {
		Ui::AddDividerText(container, rpl::single(tx.loading()));
	} else if (_state->loaded && _state->ids.isEmpty()) {
		Ui::AddDividerText(container, rpl::single(tx.empty()));
	} else if (!_state->cursor.isEmpty()) {
		AddButtonWithIcon(
			container,
			rpl::single(tx.loadMore()),
			st::settingsButtonNoIcon
		)->addClickHandler([=] { load(); });
	}
	Ui::AddDividerText(container, rpl::single(tx.info()));
	Ui::AddSkip(container);
	container->resizeToWidth(width());
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
	Luckgram::ScanUi::AddMainMenuItems(menu, controller); // 扫一扫, 代理配置.
}

} // namespace Settings
