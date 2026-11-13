/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "settings/sections/settings_luckgram_pro.h"

#include "base/timer.h"
#include "luckgram/discover/luckgram_square_ui.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/pro/luckgram_pro.h"
#include "settings/settings_common.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonDocument>
#include <QtCore/QUrl>

namespace Settings {
namespace {

namespace Pro = Luckgram::Pro;
namespace Api = Luckgram::Api;

[[nodiscard]] Pro::Texts Texts() {
	return Pro::Texts(Luckgram::IsChinese());
}

[[nodiscard]] int64 NowMs() {
	return QDateTime::currentMSecsSinceEpoch();
}

// A row that only shows a value (not clickable).
void AddValueRow(
		not_null<Ui::VerticalLayout*> container,
		const QString &title,
		const QString &value) {
	const auto button = AddButtonWithLabel(
		container,
		rpl::single(title),
		rpl::single(value),
		st::settingsButtonNoIcon);
	button->setAttribute(Qt::WA_TransparentForMouseEvents);
}

not_null<Ui::SettingsButton*> AddActionRow(
		not_null<Ui::VerticalLayout*> container,
		const QString &title,
		Fn<void()> callback,
		const style::SettingsButton &st = st::settingsButtonNoIcon) {
	const auto button = AddButtonWithIcon(container, rpl::single(title), st);
	button->addClickHandler(std::move(callback));
	return button;
}

// 29 需求 2: the live perks, one label each: bold title, 「Pro 专享」 for
// the Pro-only rows, the one-line description under it.
void AddPerks(not_null<Ui::VerticalLayout*> container) {
	const auto rows = Pro::LiveRows();
	if (rows.empty()) {
		return;
	}
	const auto texts = Texts();
	Ui::AddSkip(container);
	for (const auto &row : rows) {
		auto text = Ui::Text::Bold(row.title);
		if (row.proOnly) {
			text.append(u" · "_q + texts.proOnlyBadge());
		}
		text.append(u"\n"_q).append(row.subtitle);
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				rpl::single(text),
				st::boxLabel),
			st::boxRowPadding);
		Ui::AddSkip(container);
	}
}

[[nodiscard]] QByteArray JsonBody(const QJsonObject &object) {
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

// --- Luckgram Pro.

struct LuckgramPro::State {
	std::optional<Pro::Billing> billing;
	bool loading = true;
	bool busy = false;
	QString expiresBefore;
	int refreshesLeft = 0;
	Api::RequestPtr request;
	base::Timer refreshTimer;
};

LuckgramPro::LuckgramPro(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _controller(controller)
, _content(Ui::CreateChild<Ui::VerticalLayout>(this))
, _state(std::make_unique<State>()) {
	Ui::ResizeFitChild(this, _content);
	_state->refreshTimer.setCallback([=] { refreshAfterPurchase(); });
	Pro::Changes() | rpl::on_next([=] {
		rebuild(); // Credits and perks follow the Pro state.
	}, lifetime());
	rebuild();
	load();
	Pro::Refresh(); // 29: same state everywhere.
}

LuckgramPro::~LuckgramPro() {
	if (const auto request = base::take(_state->request)) {
		request->cancel();
	}
}

rpl::producer<QString> LuckgramPro::title() {
	return rpl::single(Texts().proTitle());
}

void LuckgramPro::load(Fn<void()> then) {
	if (const auto request = base::take(_state->request)) {
		request->cancel();
	}
	_state->request = Api::Send({
		.path = u"/v1/billing/plans"_q,
	}, crl::guard(this, [=](Api::Response response) {
		_state->request = nullptr;
		_state->billing = Pro::ParseBilling(response.json());
		_state->loading = false;
		rebuild();
		if (then) {
			then();
		}
	}), crl::guard(this, [=](Api::Error error) {
		_state->request = nullptr;
		_state->loading = false;
		rebuild();
		if (error.code != QString::fromLatin1(Api::kCancelled)) {
			_controller->showToast(Texts().error(error.code));
		}
	}));
}

void LuckgramPro::rebuild() {
	const auto texts = Texts();
	const auto container = _content.get();
	container->clear();

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rpl::single(texts.proTitle()));
	if (!_state->billing) {
		AddValueRow(
			container,
			texts.proTitle(),
			_state->loading ? u"…"_q : u"—"_q);
		Ui::AddSkip(container);
		container->resizeToWidth(width());
		return;
	}
	const auto &billing = *_state->billing;
	AddValueRow(
		container,
		texts.proTitle(),
		billing.pro
			? texts.proUntil(Pro::Date(
				Pro::ParseTime(billing.proExpiresAt),
				QTimeZone::systemTimeZone()))
			: texts.freeTier());
	const auto &pro = Pro::Current();
	if (pro.isPro(NowMs())) {
		const auto total = (pro.creditsTotal > 0)
			? pro.creditsTotal
			: Pro::ConfigNumbers().monthlyCredits;
		AddValueRow(
			container,
			texts.creditsTitle(),
			texts.creditsValue(
				pro.creditsRemaining,
				total,
				pro.creditsResetsAtMs,
				QTimeZone::systemTimeZone()));
	}
	AddPerks(container);
	Ui::AddSkip(container);
	if (!billing.enabled) {
		Ui::AddDividerText(container, rpl::single(texts.notOnSale()));
	} else if (!billing.linked) {
		AddActionRow(container, texts.goLink(), [=] {
			_controller->showSettings(LuckgramAccount::Id());
		}, st::settingsButtonNoIcon);
		Ui::AddDividerText(container, rpl::single(texts.linkFirst()));
	} else {
		if (_state->refreshesLeft > 0) {
			AddValueRow(container, texts.checking(), QString());
		}
		for (auto i = 0; i != int(billing.plans.size()); ++i) {
			const auto &plan = billing.plans[i];
			const auto button = AddButtonWithLabel(
				container,
				rpl::single(texts.plan(plan)),
				rpl::single(texts.stars(plan.stars)),
				st::settingsButtonNoIcon);
			button->addClickHandler([=] { checkout(i); });
		}
		Ui::AddDividerText(container, rpl::single(texts.proInfo()));
	}
	Ui::AddSkip(container);
	container->resizeToWidth(width());
}

void LuckgramPro::checkout(int planIndex) {
	if (_state->busy
		|| !_state->billing
		|| planIndex < 0
		|| planIndex >= int(_state->billing->plans.size())) {
		return;
	}
	const auto planId = _state->billing->plans[planIndex].id;
	_state->busy = true;
	if (const auto request = base::take(_state->request)) {
		request->cancel();
	}
	auto body = QJsonObject();
	body.insert(u"plan"_q, planId);
	_state->request = Api::Send({
		.method = "POST",
		.path = u"/v1/billing/checkout"_q,
		.body = JsonBody(body),
		.contentType = "application/json",
	}, crl::guard(this, [=](Api::Response response) {
		_state->request = nullptr;
		_state->busy = false;
		const auto url = response.json().value(u"url"_q).toString();
		if (!Pro::IsTelegramLink(url)) {
			_controller->showToast(Texts().error(QString()));
			return;
		}
		// The bot credits the payment asynchronously: re-read the
		// membership until the end date moves.
		_state->expiresBefore = _state->billing
			? _state->billing->proExpiresAt
			: QString();
		_state->refreshesLeft = Pro::kProRefreshTimes;
		rebuild();
		UrlClickHandler::Open(url); // t.me/$invoice opens in the app.
		_state->refreshTimer.callOnce(Pro::kProRefreshIntervalMs);
	}), crl::guard(this, [=](Api::Error error) {
		_state->request = nullptr;
		_state->busy = false;
		if (error.code == QString::fromLatin1(Api::kCancelled)) {
			return;
		}
		_controller->showToast(Texts().error(error.code));
		if (error.code == u"not_linked"_q
			|| error.code == u"billing_disabled"_q) {
			load();
		}
	}));
}

void LuckgramPro::refreshAfterPurchase() {
	if (_state->refreshesLeft <= 0) {
		return;
	}
	--_state->refreshesLeft;
	load([=] {
		const auto now = _state->billing
			? _state->billing->proExpiresAt
			: QString();
		if (!now.isEmpty() && now != _state->expiresBefore) {
			_state->refreshesLeft = 0;
			rebuild();
			Pro::Refresh(); // 29: the payment went through.
			_controller->showToast(Texts().proActive());
		} else if (_state->refreshesLeft > 0) {
			_state->refreshTimer.callOnce(Pro::kProRefreshIntervalMs);
		} else {
			rebuild();
		}
	});
}

// --- Account (S6).

struct LuckgramAccount::State {
	std::optional<Pro::LinkStatus> status;
	bool loading = true;
	bool waiting = false;
	QString nonce;
	int64 expiresAtMs = 0;
	Api::RequestPtr request;
	base::Timer pollTimer;
};

LuckgramAccount::LuckgramAccount(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _controller(controller)
, _content(Ui::CreateChild<Ui::VerticalLayout>(this))
, _state(std::make_unique<State>()) {
	Ui::ResizeFitChild(this, _content);
	_state->pollTimer.setCallback([=] { poll(); });
	rebuild();
	load();
}

LuckgramAccount::~LuckgramAccount() {
	if (const auto request = base::take(_state->request)) {
		request->cancel();
	}
}

rpl::producer<QString> LuckgramAccount::title() {
	return rpl::single(Texts().accountTitle());
}

void LuckgramAccount::load() {
	if (const auto request = base::take(_state->request)) {
		request->cancel();
	}
	_state->request = Api::Send({
		.path = u"/v1/link"_q,
	}, crl::guard(this, [=](Api::Response response) {
		_state->request = nullptr;
		_state->status = Pro::ParseLink(response.json());
		_state->loading = false;
		rebuild();
	}), crl::guard(this, [=](Api::Error error) {
		_state->request = nullptr;
		_state->loading = false;
		rebuild();
		if (error.code != QString::fromLatin1(Api::kCancelled)) {
			_controller->showToast(Texts().error(error.code));
		}
	}));
}

void LuckgramAccount::rebuild() {
	const auto texts = Texts();
	const auto container = _content.get();
	container->clear();

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rpl::single(texts.accountTitle()));
	const auto linked = _state->status && _state->status->linked;
	AddValueRow(
		container,
		texts.telegramAccount(),
		!_state->status
			? (_state->loading ? u"…"_q : u"—"_q)
			: linked
			? texts.linked()
			: texts.notLinked());
	if (linked && _state->status->maxDevices > 0) {
		AddValueRow(
			container,
			texts.devices(
				_state->status->devices,
				_state->status->maxDevices),
			QString());
	}
	if (_state->status) {
		if (linked) {
			// D10 (26 需求 8): 「删除我的广场数据」 while Square is on.
			if (Luckgram::SquareUi::FeatureEnabled()) {
				AddActionRow(container, Luckgram::SquareUi::Tx().deleteMyData(), [=] {
					Luckgram::SquareUi::DeleteMyData(_controller);
				}, st::settingsAttentionButton);
			}
			AddActionRow(container, texts.unlink(), [=] {
				unlink();
			}, st::settingsAttentionButton);
		} else if (_state->waiting) {
			AddValueRow(container, texts.waiting(), QString());
		} else {
			AddActionRow(container, texts.link(), [=] {
				startLink();
			}, st::settingsButtonNoIcon);
		}
	}
	Ui::AddSkip(container);
	Ui::AddDividerText(container, rpl::single(texts.linkInfo()));
	Ui::AddSkip(container);
	container->resizeToWidth(width());
}

void LuckgramAccount::startLink() {
	if (_state->waiting || _state->request) {
		return;
	}
	_state->request = Api::Send({
		.method = "POST",
		.path = u"/v1/link/start"_q,
	}, crl::guard(this, [=](Api::Response response) {
		_state->request = nullptr;
		const auto start = Pro::ParseStart(response.json());
		if (start.nonce.isEmpty() || !Pro::IsTelegramLink(start.url)) {
			_controller->showToast(Texts().error(QString()));
			return;
		}
		_state->nonce = start.nonce;
		_state->expiresAtMs = start.expiresAtMs;
		_state->waiting = true;
		rebuild();
		UrlClickHandler::Open(start.url); // The bot opens in the app.
		_state->pollTimer.callOnce(Pro::kLinkPollIntervalMs);
	}), crl::guard(this, [=](Api::Error error) {
		_state->request = nullptr;
		if (error.code == QString::fromLatin1(Api::kCancelled)) {
			return;
		}
		_controller->showToast(Texts().error(error.code));
		if (error.code == u"already_linked"_q) {
			load();
		}
	}));
}

void LuckgramAccount::poll() {
	if (!_state->waiting) {
		return;
	}
	if (!Pro::ShouldPoll(QString(), NowMs(), _state->expiresAtMs)) {
		_state->waiting = false;
		rebuild();
		_controller->showToast(Texts().linkResult(u"expired"_q, 0));
		return;
	}
	_state->request = Api::Send({
		.path = u"/v1/link?nonce="_q
			+ QString::fromLatin1(QUrl::toPercentEncoding(_state->nonce)),
	}, crl::guard(this, [=](Api::Response response) {
		_state->request = nullptr;
		const auto status = Pro::ParseLink(response.json());
		if (status.linked || status.request == u"linked"_q) {
			_state->waiting = false;
			_state->status = status;
			_state->status->linked = true;
			rebuild();
			_controller->showToast(Texts().linkedNow());
			Pro::Refresh(); // Pro now follows the Telegram account.
			load(); // Devices count.
		} else if (Pro::ShouldPoll(
				status.request,
				NowMs(),
				_state->expiresAtMs)) {
			_state->pollTimer.callOnce(Pro::kLinkPollIntervalMs);
		} else {
			_state->waiting = false;
			rebuild();
			_controller->showToast(Texts().linkResult(
				status.request,
				status.maxDevices));
		}
	}), crl::guard(this, [=](Api::Error error) {
		_state->request = nullptr;
		if (error.code == QString::fromLatin1(Api::kCancelled)) {
			return;
		}
		// Offline for a moment: keep trying until the nonce expires.
		_state->pollTimer.callOnce(Pro::kLinkPollIntervalMs);
	}));
}

void LuckgramAccount::unlink() {
	const auto texts = Texts();
	_controller->show(Ui::MakeConfirmBox({
		.text = texts.unlinkConfirmText(),
		.confirmed = Fn<void(Fn<void()>)>(crl::guard(this, [=](
				Fn<void()> close) {
			close();
			if (const auto request = base::take(_state->request)) {
				request->cancel();
			}
			_state->request = Api::Send({
				.method = "DELETE",
				.path = u"/v1/link"_q,
			}, crl::guard(this, [=](Api::Response) {
				_state->request = nullptr;
				_controller->showToast(Texts().unlinkedNow());
				Pro::Refresh();
				load();
			}), crl::guard(this, [=](Api::Error error) {
				_state->request = nullptr;
				if (error.code != QString::fromLatin1(Api::kCancelled)) {
					_controller->showToast(Texts().error(error.code));
				}
			}));
		})),
		.confirmText = texts.unlink(),
		.confirmStyle = &st::attentionBoxButton,
		.title = texts.unlinkConfirmTitle(),
	}));
}

// --- Rows on the Luckgram Features page.

void AddLuckgramAccountRows(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	const auto texts = Texts();
	AddButtonWithIcon(
		container,
		rpl::single(texts.accountTitle()),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		controller->showSettings(LuckgramAccount::Id());
	});
	// S7: Pro only while billing.enabled (remote config, default off).
	if (Pro::BillingEnabled()) {
		AddButtonWithIcon(
			container,
			rpl::single(texts.proTitle()),
			st::settingsButtonNoIcon
		)->addClickHandler([=] {
			controller->showSettings(LuckgramPro::Id());
		});
	}
}

} // namespace Settings
