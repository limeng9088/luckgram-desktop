/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/proxy/luckgram_proxy_import.h"

#include "base/unique_qptr.h"
#include "boxes/connection_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/core_settings_proxy.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/proxy/luckgram_proxy_exit.h"
#include "luckgram/proxy/luckgram_proxy_import_logic.h"
#include "luckgram/qr/luckgram_qr_decode.h"
#include "mtproto/mtproto_proxy_data.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtGui/QClipboard>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Luckgram::ProxyImport {
namespace {

constexpr auto kSubscriptionsKey = "proxy_import.subscriptions";
constexpr auto kTimeoutMs = 20 * 1000;
constexpr auto kUserAgent = "clash.meta";

// ---- Texts (mobile ProxyImport.T, desktop words where marked).

[[nodiscard]] QString LinkRowText() {
	return Tr("输入链接或订阅地址", "Add from Link or Subscription");
}

[[nodiscard]] QString QrRowText() {
	// Mobile 「扫码添加代理」: desktop reads a picture (desktop wording).
	return Tr("从图片识别二维码", "Read QR Code from a Picture");
}

[[nodiscard]] QString InputTitle() {
	return Tr("添加代理", "Add Proxy");
}

[[nodiscard]] QString InputMessage() {
	return Tr(
		"粘贴代理链接（tg://socks、t.me/proxy、socks5://、vless://、vmess://、trojan://、ss://）或 mihomo / Clash 订阅地址。",
		"Paste a proxy link (tg://socks, t.me/proxy, socks5://, vless://, vmess://, trojan://, ss://) or a mihomo / Clash subscription URL.");
}

[[nodiscard]] QString UnknownText() {
	return Tr(
		"无法识别的代理链接。支持 tg://socks、t.me/proxy、socks5://、vless://、vmess://、trojan://、ss:// 链接和 mihomo / Clash 订阅地址。",
		"This is not a proxy link. Supported: tg://socks, t.me/proxy, socks5://, vless://, vmess://, trojan://, ss:// links and mihomo / Clash subscription URLs.");
}

[[nodiscard]] QString XrayProblemText() {
	// Desktop until D12: the core isn't in the app yet.
	return Tr(
		"VLESS、VMess、Trojan、Shadowsocks 节点需要内置 Xray 内核，当前不可用",
		"VLESS, VMess, Trojan and Shadowsocks need the built-in Xray core, which isn't available");
}

[[nodiscard]] QString ResultText(
		int total,
		int added,
		int removed,
		int skipped,
		bool xraySkipped) {
	auto result = IsChinese()
		? u"订阅里有 %1 个可用代理：新增 %2 个"_q.arg(total).arg(added)
		: u"%1 usable %2: %3 added"_q
			.arg(total)
			.arg((total == 1) ? u"proxy"_q : u"proxies"_q)
			.arg(added);
	if (removed > 0) {
		result += IsChinese()
			? u"，移除 %1 个"_q.arg(removed)
			: u", %1 removed"_q.arg(removed);
	}
	result += Tr("。", ".");
	if (skipped > 0) {
		const auto entries = (skipped == 1) ? u"entry"_q : u"entries"_q;
		result += u"\n\n"_q + (xraySkipped
			? (IsChinese()
				? u"跳过 %1 个节点：%2。"_q.arg(skipped).arg(XrayProblemText())
				: u"Skipped %1 %2: %3."_q
					.arg(skipped)
					.arg(entries)
					.arg(XrayProblemText()))
			: (IsChinese()
				? u"跳过 %1 个不支持的节点（支持 SOCKS5、MTProto、VLESS、VMess、Trojan、Shadowsocks；Hysteria、TUIC 等用不了）。"_q.arg(skipped)
				: u"Skipped %1 unsupported %2 (supported: SOCKS5, MTProto, VLESS, VMess, Trojan, Shadowsocks; not Hysteria, TUIC and others)."_q
					.arg(skipped)
					.arg(entries)));
	}
	return result;
}

// ---- Subscriptions (this device only).

struct Subscription {
	QString url;
	std::set<QString> keys; // Proxies this subscription imported.
	qint64 updated = 0; // Milliseconds since epoch.
};

[[nodiscard]] rpl::event_stream<> &SubscriptionsChanged() {
	static auto result = rpl::event_stream<>();
	return result;
}

[[nodiscard]] std::vector<Subscription> LoadSubscriptions() {
	const auto value = Settings::GetString(
		QString::fromLatin1(kSubscriptionsKey));
	auto result = std::vector<Subscription>();
	const auto array = QJsonDocument::fromJson(value.toUtf8()).array();
	for (const auto &item : array) {
		const auto object = item.toObject();
		auto subscription = Subscription{
			.url = object.value(u"url"_q).toString(),
			.updated = qint64(object.value(u"updated"_q).toDouble()),
		};
		for (const auto &key : object.value(u"keys"_q).toArray()) {
			subscription.keys.emplace(key.toString());
		}
		if (!subscription.url.isEmpty()) {
			result.push_back(std::move(subscription));
		}
	}
	return result;
}

void SaveSubscriptions(const std::vector<Subscription> &list) {
	auto array = QJsonArray();
	for (const auto &subscription : list) {
		auto keys = QJsonArray();
		for (const auto &key : subscription.keys) {
			keys.push_back(key);
		}
		array.push_back(QJsonObject{
			{ u"url"_q, subscription.url },
			{ u"keys"_q, keys },
			{ u"updated"_q, double(subscription.updated) },
		});
	}
	const auto key = QString::fromLatin1(kSubscriptionsKey);
	if (array.isEmpty()) {
		Settings::Remove(key);
	} else {
		Settings::SetString(
			key,
			QString::fromUtf8(
				QJsonDocument(array).toJson(QJsonDocument::Compact)));
	}
	SubscriptionsChanged().fire({});
}

[[nodiscard]] QString SubscriptionTitle(const Subscription &subscription) {
	const auto host = HostOf(subscription.url);
	return host.isEmpty() ? subscription.url : host;
}

// 「N 个代理 · 更新于 11-07 15:20」.
[[nodiscard]] QString SubscriptionLine(const Subscription &subscription) {
	const auto count = int(subscription.keys.size());
	const auto when = subscription.updated
		? QDateTime::fromMSecsSinceEpoch(subscription.updated).toString(
			u"MM-dd HH:mm"_q)
		: u"—"_q;
	return IsChinese()
		? u"%1 个代理 · 更新于 %2"_q.arg(count).arg(when)
		: u"%1 %2 · updated %3"_q
			.arg(count)
			.arg((count == 1) ? u"proxy"_q : u"proxies"_q)
			.arg(when);
}

// ---- Proxies.

// Telegram's proxy for an entry; nullopt for an Xray node (D12 hands the
// node to the core and gets its local SOCKS5 here) or an invalid one.
[[nodiscard]] std::optional<MTP::ProxyData> ToProxy(const Entry &entry) {
	if (entry.type == QString::fromLatin1(kTypeXray)) {
		return std::nullopt;
	}
	auto result = MTP::ProxyData();
	result.host = entry.host;
	result.port = uint32(entry.port);
	if (entry.type == QString::fromLatin1(kTypeMtproto)) {
		result.type = MTP::ProxyData::Type::Mtproto;
		result.password = entry.secret;
	} else {
		result.type = MTP::ProxyData::Type::Socks5;
		result.user = entry.user;
		result.password = entry.password;
	}
	return result.valid() ? std::make_optional(result) : std::nullopt;
}

[[nodiscard]] QString KeyOf(const MTP::ProxyData &proxy) {
	using Type = MTP::ProxyData::Type;
	if (proxy.type == Type::Socks5) {
		return MakeEntry(
			QString::fromLatin1(kTypeSocks5),
			proxy.host,
			int(proxy.port),
			proxy.user,
			proxy.password).key();
	} else if (proxy.type == Type::Mtproto) {
		return MakeEntry(
			QString::fromLatin1(kTypeMtproto),
			proxy.host,
			int(proxy.port),
			QString(),
			QString(),
			proxy.password).key();
	}
	return QString();
}

[[nodiscard]] std::set<QString> ExistingKeys(
		not_null<ProxiesBoxController*> controller) {
	auto result = std::set<QString>();
	for (const auto &proxy : controller->luckgramList()) {
		const auto key = KeyOf(proxy);
		if (!key.isEmpty()) {
			result.emplace(key);
		}
	}
	return result;
}

void RemoveKeys(
		not_null<ProxiesBoxController*> controller,
		const std::vector<QString> &keys) {
	if (keys.empty()) {
		return;
	}
	const auto set = std::set<QString>(keys.begin(), keys.end());
	for (const auto &proxy : controller->luckgramList()) {
		if (set.contains(KeyOf(proxy))) {
			controller->luckgramRemove(proxy);
		}
	}
}

void Inform(
		std::shared_ptr<Ui::Show> show,
		const QString &title,
		const QString &text) {
	show->showBox(Ui::MakeInformBox({
		.text = text,
		.title = title,
	}));
}

// ---- Download.

void Get(
		const QString &url,
		std::optional<MTP::ProxyData> via,
		Fn<void(std::optional<QByteArray>)> done) {
	const auto manager = new QNetworkAccessManager();
	if (via) {
		manager->setProxy(QNetworkProxy(
			QNetworkProxy::Socks5Proxy,
			via->host,
			quint16(via->port),
			via->user,
			via->password));
	} else {
		manager->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
	}
	auto request = QNetworkRequest(QUrl(url));
	request.setTransferTimeout(kTimeoutMs);
	request.setRawHeader("User-Agent", kUserAgent);
	const auto reply = manager->get(request);
	const auto tooBig = std::make_shared<bool>(false);
	QObject::connect(
		reply,
		&QNetworkReply::downloadProgress,
		[=](qint64 received, qint64) {
			if (received > kMaxBytes && !*tooBig) {
				*tooBig = true;
				reply->abort();
			}
		});
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto ok = !*tooBig
			&& (reply->error() == QNetworkReply::NoError)
			&& (status == 200);
		auto body = ok ? reply->readAll() : QByteArray();
		reply->deleteLater();
		manager->deleteLater();
		if (ok && body.size() <= kMaxBytes) {
			done(std::move(body));
		} else {
			done(std::nullopt);
		}
	});
}

// Direct first; then through the enabled SOCKS5 proxy (https only, as
// mobile).
void Download(const QString &url, Fn<void(std::optional<QByteArray>)> done) {
	const auto &settings = Core::App().settings().proxy();
	const auto selected = settings.selected();
	const auto via = (settings.isEnabled()
		&& selected.type == MTP::ProxyData::Type::Socks5
		&& url.startsWith(u"https://"_q, Qt::CaseInsensitive))
		? std::make_optional(selected)
		: std::nullopt;
	Get(url, std::nullopt, [=](std::optional<QByteArray> body) {
		if (body || !via) {
			done(std::move(body));
		} else {
			Get(url, via, done);
		}
	});
}

void Apply(
		std::shared_ptr<Ui::Show> show,
		not_null<ProxiesBoxController*> controller,
		const QString &url,
		bool refresh,
		const Result &result) {
	auto list = LoadSubscriptions();
	auto i = ranges::find(list, url, &Subscription::url);
	if (i == end(list)) {
		list.push_back({ .url = url });
		i = end(list) - 1;
	}
	// Xray nodes need the built-in core (D12); until then they're skipped.
	auto usable = std::vector<Entry>();
	auto skipped = result.skipped;
	auto xraySkipped = false;
	for (const auto &entry : result.proxies) {
		if (entry.type == QString::fromLatin1(kTypeXray)) {
			++skipped;
			xraySkipped = true;
		} else {
			usable.push_back(entry);
		}
	}
	auto diff = MakeDiff(i->keys, usable, ExistingKeys(controller));
	RemoveKeys(controller, diff.remove);
	auto failed = std::set<QString>();
	for (const auto &entry : diff.add) {
		if (const auto proxy = ToProxy(entry)) {
			controller->luckgramAdd(*proxy);
		} else {
			failed.emplace(entry.key());
		}
	}
	if (!failed.empty()) {
		const auto drop = [&](const Entry &entry) {
			return failed.contains(entry.key());
		};
		diff.add.erase(ranges::remove_if(diff.add, drop), end(diff.add));
		usable.erase(ranges::remove_if(usable, drop), end(usable));
		skipped += int(failed.size());
	}
	// The subscription owns what it added now or before; a proxy that was
	// in the list already (added by hand) is never removed with it.
	i->keys = Owned(i->keys, usable, diff);
	i->updated = QDateTime::currentMSecsSinceEpoch();
	SaveSubscriptions(list);
	Inform(
		show,
		refresh
			? Tr("刷新订阅", "Refresh Subscription")
			: Tr("导入订阅", "Import Subscription"),
		ResultText(
			int(usable.size()),
			int(diff.add.size()),
			int(diff.remove.size()),
			skipped,
			xraySkipped));
}

void ImportSubscription(
		std::shared_ptr<Ui::Show> show,
		not_null<ProxiesBoxController*> controller,
		QPointer<QWidget> guard,
		const QString &url,
		bool refresh) {
	show->showToast(Tr("正在下载订阅…", "Downloading the subscription…"));
	Download(url, [=](std::optional<QByteArray> body) {
		if (!guard) {
			return; // The proxy box was closed meanwhile.
		} else if (!body) {
			Inform(
				show,
				Tr("导入订阅", "Import Subscription"),
				Tr(
					"下载订阅失败，请检查地址和网络后重试。",
					"Could not download the subscription. Check the URL and your connection, then try again."));
			return;
		}
		Apply(
			show,
			controller,
			url,
			refresh,
			ParseSubscription(QString::fromUtf8(*body)));
	});
}

void HandleIn(
		std::shared_ptr<Ui::Show> show,
		not_null<ProxiesBoxController*> controller,
		QPointer<QWidget> guard,
		const QString &raw) {
	const auto text = raw.trimmed();
	switch (KindOf(text)) {
	case Kind::Proxy: {
		const auto entry = ParseLink(text);
		if (!entry) {
			break;
		} else if (entry->type == QString::fromLatin1(kTypeXray)) {
			Inform(show, InputTitle(), XrayProblemText() + Tr("。", "."));
			return;
		}
		const auto proxy = ToProxy(*entry);
		if (!proxy) {
			break;
		}
		const auto existed = controller->contains(*proxy);
		if (!existed) {
			controller->addNewItem(*proxy); // Upstream: added and used.
		}
		show->showToast(existed
			? Tr("这个代理已在列表里", "This proxy is already in the list")
			: Tr("已添加代理", "Proxy added"));
		return;
	}
	case Kind::Subscription:
		ImportSubscription(show, controller, guard, text, false);
		return;
	case Kind::Unknown:
		break;
	}
	Inform(show, Tr("无法识别", "Not recognized"), UnknownText());
}

void ShowLinkBox(
		std::shared_ptr<Ui::Show> show,
		not_null<ProxiesBoxController*> controller,
		QPointer<QWidget> guard) {
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(InputTitle()));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			InputMessage(),
			st::boxLabel));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Tr("链接或订阅地址", "Link or subscription URL"))));
		const auto submit = [=] {
			const auto text = field->getLastText().trimmed();
			if (text.isEmpty()) {
				field->showError();
				return;
			}
			box->closeBox();
			HandleIn(show, controller, guard, text);
		};
		field->submits(
		) | rpl::on_next([=](Qt::KeyboardModifiers) {
			submit();
		}, field->lifetime());
		box->setFocusCallback([=] { field->setFocusFast(); });
		box->addButton(rpl::single(Tr("添加", "Add")), submit);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void HandleImage(
		std::shared_ptr<Ui::Show> show,
		not_null<ProxiesBoxController*> controller,
		QPointer<QWidget> guard,
		const QImage &image) {
	if (const auto text = Qr::DecodeQr(image)) {
		HandleIn(show, controller, guard, *text);
	} else {
		show->showToast(Tr(
			"图片里没有找到二维码",
			"No QR code found in the picture"));
	}
}

void ReadQrFromFile(
		std::shared_ptr<Ui::Show> show,
		not_null<ProxiesBoxController*> controller,
		QPointer<QWidget> guard) {
	FileDialog::GetOpenPath(
		guard,
		QrRowText(),
		u"Images (*.png *.jpg *.jpeg *.bmp *.webp *.gif)"_q,
		[=](FileDialog::OpenResult &&result) {
			if (!guard) {
				return;
			}
			auto image = QImage();
			if (!result.remoteContent.isEmpty()) {
				image.loadFromData(result.remoteContent);
			} else if (!result.paths.isEmpty()) {
				image.load(result.paths.front());
			} else {
				return;
			}
			HandleImage(show, controller, guard, image);
		});
}

void ReadQrFromClipboard(
		std::shared_ptr<Ui::Show> show,
		not_null<ProxiesBoxController*> controller,
		QPointer<QWidget> guard) {
	const auto image = QGuiApplication::clipboard()->image();
	if (image.isNull()) {
		show->showToast(Tr(
			"剪贴板里没有图片",
			"There's no picture in the clipboard"));
		return;
	}
	HandleImage(show, controller, guard, image);
}

void ShowSubscriptionMenu(
		std::shared_ptr<Ui::Show> show,
		not_null<ProxiesBoxController*> controller,
		not_null<QWidget*> parent,
		base::unique_qptr<Ui::PopupMenu> &holder,
		const Subscription &subscription) {
	const auto url = subscription.url;
	const auto guard = QPointer<QWidget>(parent.get());
	holder = base::make_unique_q<Ui::PopupMenu>(
		parent.get(),
		st::popupMenuWithIcons);
	const auto menu = holder.get();
	menu->addAction(Tr("刷新", "Refresh"), [=] {
		ImportSubscription(show, controller, guard, url, true);
	}, &st::menuIconRestore);
	menu->addAction(Tr("删除订阅", "Delete Subscription"), [=] {
		const auto list = LoadSubscriptions();
		const auto i = ranges::find(list, url, &Subscription::url);
		const auto count = (i == end(list)) ? 0 : int(i->keys.size());
		show->showBox(Ui::MakeConfirmBox({
			.text = IsChinese()
				? u"将同时删除由这个订阅导入的 %1 个代理。手动添加的代理不受影响。"_q
					.arg(count)
				: u"The %1 proxies imported from it will be removed too. Proxies you added yourself stay."_q
					.arg(count),
			.confirmed = [=](Fn<void()> close) {
				auto list = LoadSubscriptions();
				const auto i = ranges::find(list, url, &Subscription::url);
				if (i != end(list)) {
					if (guard) {
						RemoveKeys(
							controller,
							std::vector<QString>(
								i->keys.begin(),
								i->keys.end()));
					}
					list.erase(i);
					SaveSubscriptions(list);
				}
				close();
			},
			.confirmText = Tr("删除订阅", "Delete Subscription"),
			.confirmStyle = &st::attentionBoxButton,
			.title = Tr("删除订阅？", "Delete subscription?"),
		}));
	}, &st::menuIconDeleteAttention);
	menu->popup(QCursor::pos());
}

void FillSubscriptions(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Ui::Show> show,
		not_null<ProxiesBoxController*> controller) {
	const auto list = LoadSubscriptions();
	if (list.empty()) {
		return;
	}
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		rpl::single(Tr("订阅", "Subscriptions")));
	const auto menu = container->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();
	for (const auto &subscription : list) {
		const auto button = ::Settings::AddButtonWithLabel(
			container,
			rpl::single(SubscriptionTitle(subscription)),
			rpl::single(SubscriptionLine(subscription)),
			st::settingsButtonNoIcon);
		button->addClickHandler([=] {
			ShowSubscriptionMenu(
				show,
				controller,
				button,
				*menu,
				subscription);
		});
	}
}

} // namespace

void SetupTop(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Ui::Show> show,
		not_null<ProxiesBoxController*> controller) {
	const auto guard = QPointer<QWidget>(container.get());

	// Mobile #117: the direct exit; clicking it checks every exit again.
	const auto direct = ::Settings::AddButtonWithLabel(
		container,
		rpl::single(ProxyExit::DirectTitle()),
		rpl::single(rpl::empty) | rpl::then(
			ProxyExit::Updates()
		) | rpl::map([] {
			return ProxyExit::Text(nullptr);
		}),
		st::settingsButtonNoIcon);
	direct->addClickHandler([=] {
		controller->luckgramCheckExits(true);
	});

	::Settings::AddButtonWithIcon(
		container,
		rpl::single(LinkRowText()),
		st::settingsButton,
		{ &st::menuIconLink }
	)->addClickHandler([=] {
		ShowLinkBox(show, controller, guard);
	});

	const auto qr = ::Settings::AddButtonWithIcon(
		container,
		rpl::single(QrRowText()),
		st::settingsButton,
		{ &st::menuIconQrCode });
	const auto qrMenu = qr->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();
	qr->addClickHandler([=] {
		*qrMenu = base::make_unique_q<Ui::PopupMenu>(
			qr.get(),
			st::popupMenuWithIcons);
		const auto menu = qrMenu->get();
		menu->addAction(Tr("选择图片…", "Choose a Picture…"), [=] {
			ReadQrFromFile(show, controller, guard);
		}, &st::menuIconPhoto);
		menu->addAction(
			Tr("识别剪贴板里的截图", "Read a Screenshot from the Clipboard"),
			[=] { ReadQrFromClipboard(show, controller, guard); },
			&st::menuIconImportTheme);
		menu->popup(QCursor::pos());
	});
}

void SetupSubscriptions(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Ui::Show> show,
		not_null<ProxiesBoxController*> controller) {
	const auto inner = container->add(
		object_ptr<Ui::VerticalLayout>(container));
	const auto fill = [=] {
		inner->clear();
		FillSubscriptions(inner, show, controller);
		inner->resizeToWidth(container->width());
	};
	fill();
	SubscriptionsChanged().events(
	) | rpl::on_next([=] {
		crl::on_main(inner, fill);
	}, inner->lifetime());
}

} // namespace Luckgram::ProxyImport
