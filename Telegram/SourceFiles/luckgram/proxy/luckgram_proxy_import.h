/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

class ProxiesBoxController;

namespace Ui {
class Show;
class VerticalLayout;
} // namespace Ui

// D3 (mobile specs#118): the proxy settings box (also the one on the login
// screen) gets 「输入链接或订阅地址」, 「从图片识别二维码」 (a picture file or
// a screenshot in the clipboard) and a 「订阅」 section. Links are added at
// once; other http(s) URLs are mihomo / Clash subscriptions: downloaded
// (direct first, then through the enabled SOCKS5 proxy; 20 s, 2 MB,
// User-Agent clash.meta), their usable proxies added, the URL kept on this
// device only (Luckgram settings) for a manual refresh that only adds /
// removes what the subscription itself imported.
//
// Desktop differences: 扫码 (camera) → a QR code read from a picture or the
// clipboard (the camera is D10 扫一扫); tapping a subscription row → a click
// opening a menu (刷新 / 删除订阅). VLESS / VMess / Trojan / Shadowsocks
// entries go to the built-in Xray core (D12) and are skipped while it is
// off or missing.
namespace Luckgram::ProxyImport {

// Above the proxy list: 「直连（不使用代理）」 with the direct exit (click:
// check every exit again, mobile #117), 「输入链接或订阅地址」 and
// 「从图片识别二维码」.
void SetupTop(
	not_null<Ui::VerticalLayout*> container,
	std::shared_ptr<Ui::Show> show,
	not_null<ProxiesBoxController*> controller);

// Below the proxy list: the 「订阅」 section, one row per subscription.
void SetupSubscriptions(
	not_null<Ui::VerticalLayout*> container,
	std::shared_ptr<Ui::Show> show,
	not_null<ProxiesBoxController*> controller);

} // namespace Luckgram::ProxyImport
