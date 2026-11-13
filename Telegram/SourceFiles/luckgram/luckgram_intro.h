/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

class QPainter;

// Welcome page (intro start step), aligned with the Luckgram mobile apps.
namespace Luckgram {

// "欢迎使用 Luckgram" / "Welcome to Luckgram".
[[nodiscard]] rpl::producer<QString> WelcomeTitle();
// Short description until the desktop AI features ship.
[[nodiscard]] rpl::producer<QString> WelcomeDescription();
// "开始使用" / "Get Started".
[[nodiscard]] rpl::producer<QString> WelcomeButton();

// Paints the welcome cover instead of Telegram's paper plane art: a black
// band with rounded dark tiles (as on mobile) and the white Luckgram logo on
// the middle tile. `top` is <= 0 while the cover slides in or out.
void PaintIntroCover(QPainter &p, int width, int top);

} // namespace Luckgram
