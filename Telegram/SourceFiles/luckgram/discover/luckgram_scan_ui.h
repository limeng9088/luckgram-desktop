/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// D10 (mobile specs#92 / #99, 扫一扫): scan a QR code with the computer's
// camera, or read it from a picture file or a screenshot in the clipboard
// (Luckgram::Qr::DecodeQr, D3). The text goes to Telegram's own flows: log
// in on another device (asks first), proxy, Telegram link, web link, plain
// text. Nothing is written to disk.
namespace Luckgram::ScanUi {

[[nodiscard]] bool FeatureEnabled();

void ShowScan(not_null<Window::SessionController*> controller);

// Main menu: 「扫一扫」 and 「代理配置」 with the proxy state, while the
// chat_plus_menu switch is on.
void AddMainMenuItems(
	not_null<Ui::VerticalLayout*> menu,
	not_null<Window::SessionController*> controller);

} // namespace Luckgram::ScanUi
