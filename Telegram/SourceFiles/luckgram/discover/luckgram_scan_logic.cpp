/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/discover/luckgram_scan_logic.h"

#include <QtCore/QStringList>

namespace Luckgram::Scan {
namespace {

[[nodiscard]] QString L(const char *text) {
	return QString::fromLatin1(text);
}

[[nodiscard]] int FirstOf(const QString &s, const QString &chars) {
	auto end = int(s.size());
	for (const auto c : chars) {
		const auto i = int(s.indexOf(c));
		if (i >= 0 && i < end) {
			end = i;
		}
	}
	return end;
}

// Host of http(s)://host/... or a bare t.me/...; empty otherwise.
[[nodiscard]] QString WebHost(const QString &lower) {
	auto rest = QString();
	if (lower.startsWith(L("https://"))) {
		rest = lower.mid(8);
	} else if (lower.startsWith(L("http://"))) {
		rest = lower.mid(7);
	} else if (lower.startsWith(L("t.me/"))
		|| lower.startsWith(L("telegram.me/"))
		|| lower.startsWith(L("telegram.dog/"))) {
		rest = lower;
	} else {
		return QString();
	}
	auto host = rest.left(FirstOf(rest, L("/?#")));
	if (host.startsWith(L("www."))) {
		host = host.mid(4);
	}
	return host;
}

[[nodiscard]] bool IsTelegramHost(const QString &host) {
	return (host == L("t.me"))
		|| (host == L("telegram.me"))
		|| (host == L("telegram.dog"));
}

// First path segment after the host.
[[nodiscard]] QString PathOf(const QString &lower) {
	const auto start = lower.indexOf(L("://"));
	const auto rest = (start >= 0) ? lower.mid(start + 3) : lower;
	const auto slash = rest.indexOf(QChar('/'));
	if (slash < 0) {
		return QString();
	}
	const auto path = rest.mid(slash + 1);
	return path.left(FirstOf(path, L("/?#")));
}

[[nodiscard]] bool IsProxy(const QString &lower) {
	for (const auto prefix : {
			"tg:proxy", "tg://proxy",
			"tg:socks", "tg://socks",
			"tg:webproxy", "tg://webproxy" }) {
		if (lower.startsWith(L(prefix) + QChar('?'))) {
			return true;
		}
	}
	const auto host = WebHost(lower);
	if (host.isEmpty() || !IsTelegramHost(host)) {
		return false;
	}
	const auto path = PathOf(lower);
	return (path == L("proxy")) || (path == L("socks"));
}

} // namespace

Kind KindOf(const QString &raw) {
	const auto text = raw.trimmed();
	if (text.isEmpty()
		|| text.contains(QChar('\n'))
		|| text.contains(QChar(' '))) {
		return Kind::Text;
	}
	const auto lower = text.toLower();
	if (lower.startsWith(L(kLoginPrefix))) {
		return Kind::Login;
	} else if (IsProxy(lower)) {
		return Kind::Proxy;
	} else if (lower.startsWith(L("tg:"))) {
		return Kind::Telegram;
	}
	const auto host = WebHost(lower);
	if (host.isEmpty()) {
		return Kind::Text;
	} else if (IsTelegramHost(host)) {
		return PathOf(lower).isEmpty() ? Kind::Web : Kind::Telegram;
	}
	return Kind::Web;
}

QString Url(const QString &raw) {
	const auto text = raw.trimmed();
	const auto lower = text.toLower();
	if (!lower.startsWith(L("http://"))
		&& !lower.startsWith(L("https://"))
		&& !lower.startsWith(L("tg:"))) {
		return L("https://") + text;
	}
	return text;
}

ProxyState StateOf(bool enabled, bool connected) {
	if (!enabled) {
		return ProxyState::Off;
	}
	return connected ? ProxyState::Connected : ProxyState::Connecting;
}

QString Texts::t(const char *zh, const char *en) const {
	return QString::fromUtf8(_zh ? zh : en);
}

QString Texts::plus() const { return t("更多", "More"); }
QString Texts::scan() const { return t("扫一扫", "Scan"); }
QString Texts::proxy() const { return t("代理配置", "Proxy settings"); }

QString Texts::proxyState(ProxyState state) const {
	switch (state) {
	case ProxyState::Connected: return t("已连接", "Connected");
	case ProxyState::Connecting: return t("连接中", "Connecting");
	case ProxyState::Off: break;
	}
	return t("未开启", "Off");
}

QString Texts::scanHint() const { return t("扫描 Telegram 二维码：加好友、加群、添加代理或在其他设备登录", "Scan a Telegram QR code: add a friend, join a group, add a proxy or log in on another device"); }
QString Texts::myQr() const { return t("我的二维码", "My QR code"); }
QString Texts::cameraDenied() const { return t("没有相机权限，请在系统设置里允许 Luckgram 使用相机", "No camera access. Allow Luckgram to use the camera in system settings"); }
QString Texts::openSettings() const { return t("去设置", "Settings"); }
QString Texts::notNow() const { return t("以后再说", "Not now"); }
QString Texts::loginTitle() const { return t("在其他设备登录", "Log in on another device"); }
QString Texts::loginConfirm() const { return t("确定要用当前账号登录扫码的设备吗？只确认你自己的设备。", "Log in to the scanned device with this account? Only confirm devices that are yours."); }
QString Texts::login() const { return t("登录", "Log in"); }
QString Texts::loginDone() const { return t("已在其他设备登录", "Logged in on the other device"); }
QString Texts::text() const { return t("扫描结果", "Scan result"); }
QString Texts::copy() const { return t("复制", "Copy"); }
QString Texts::copied() const { return t("已复制", "Copied"); }
QString Texts::cancel() const { return t("取消", "Cancel"); }

} // namespace Luckgram::Scan
