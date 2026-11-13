/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QString>

// D10 (mobile specs#92 / #99, "扫一扫"): what a scanned QR code is, so it
// goes to Telegram's own flow: login token, proxy, Telegram link, another
// web link or plain text. Ported from Android ScanLogic (unit tested with
// the same cases).
namespace Luckgram::Scan {

// Feature switch (default on): scan and proxy settings in the menu.
inline constexpr auto kFeatureKey = "chat_plus_menu";
inline constexpr auto kLoginPrefix = "tg://login?token=";

enum class Kind {
	Text,
	Login,
	Proxy,
	Telegram,
	Web,
};

enum class ProxyState {
	Off,
	Connecting,
	Connected,
};

[[nodiscard]] Kind KindOf(const QString &raw);
// The url to open: adds https:// to a bare t.me/... link.
[[nodiscard]] QString Url(const QString &raw);
[[nodiscard]] ProxyState StateOf(bool enabled, bool connected);

class Texts final {
public:
	explicit Texts(bool zh) : _zh(zh) {
	}

	[[nodiscard]] QString plus() const;
	[[nodiscard]] QString scan() const;
	[[nodiscard]] QString proxy() const;
	[[nodiscard]] QString proxyState(ProxyState state) const;
	[[nodiscard]] QString scanHint() const;
	[[nodiscard]] QString myQr() const;
	[[nodiscard]] QString cameraDenied() const;
	[[nodiscard]] QString openSettings() const;
	[[nodiscard]] QString notNow() const;
	[[nodiscard]] QString loginTitle() const;
	[[nodiscard]] QString loginConfirm() const;
	[[nodiscard]] QString login() const;
	[[nodiscard]] QString loginDone() const;
	// Mobile LuckgramScan: AUTH_TOKEN_EXCEPTION is "already logged in",
	// anything else "An error occurred." and the code on the next line.
	[[nodiscard]] QString loginError(const QString &type) const;
	[[nodiscard]] QString text() const;
	[[nodiscard]] QString copy() const;
	[[nodiscard]] QString copied() const;
	[[nodiscard]] QString cancel() const;

	// Desktop only (D10 "桌面端差异": camera, picture file, clipboard).
	[[nodiscard]] QString fromImage() const;
	[[nodiscard]] QString fromClipboard() const;
	[[nodiscard]] QString noCamera() const;
	[[nodiscard]] QString noCode() const;
	[[nodiscard]] QString clipboardEmpty() const;
	[[nodiscard]] QString cameraHint() const;
	[[nodiscard]] QString menuAbout() const;

private:
	[[nodiscard]] QString t(const char *zh, const char *en) const;

	bool _zh = false;

};

} // namespace Luckgram::Scan
