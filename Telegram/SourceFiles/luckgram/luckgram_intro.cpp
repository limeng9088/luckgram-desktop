/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_intro.h"

#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "luckgram/luckgram_onboarding_logic.h"
#include "luckgram/luckgram_strings.h"
#include "ui/painter.h"
#include "styles/style_intro.h"

namespace Luckgram {
namespace {

// Colors of the mobile welcome page (LuckgramWelcomeActivity).
constexpr auto kCoverBg = QColor(0x00, 0x00, 0x00);
constexpr auto kTileBg = QColor(0x1C, 0x1C, 0x1E);
// D5 "桌面端差异": the welcome texts are at most 480px wide.
constexpr auto kMaxTextWidth = 480;

[[nodiscard]] rpl::producer<QString> TextValue(
		QString (Onboarding::Texts::*method)() const) {
	return rpl::single(rpl::empty) | rpl::then(
		Lang::GetInstance().updated()
	) | rpl::map([=] {
		return (Onboarding::Texts(IsChinese()).*method)();
	});
}

[[nodiscard]] const QImage &Logo() {
	static const auto result = QImage(u":/gui/art/luckgram_logo_white.png"_q);
	return result;
}

} // namespace

rpl::producer<QString> WelcomeTitle() {
	return TextValue(&Onboarding::Texts::welcomeTitle);
}

rpl::producer<QString> WelcomeDescription() {
	return TextValue(&Onboarding::Texts::intro); // D5: mobile's full intro.
}

rpl::producer<QString> WelcomeButton() {
	return TextValue(&Onboarding::Texts::getStarted);
}

int WelcomeTextWidth(int outerWidth) {
	const auto padding = style::ConvertScale(24);
	return std::max(
		std::min(
			outerWidth - 2 * padding,
			style::ConvertScale(kMaxTextWidth)),
		1);
}

void PaintIntroCover(QPainter &p, int width, int top) {
	const auto height = st::introCoverHeight;
	const auto shown = top + height;
	if (shown <= 0) {
		return;
	}
	p.save();
	p.setClipRect(0, 0, width, shown);
	p.fillRect(0, 0, width, shown, kCoverBg);

	// Tiles: mobile uses 112dp tiles with 18dp gaps; scaled to the 208px
	// band so about one and a half rows are visible.
	const auto size = style::ConvertScale(76);
	const auto gap = style::ConvertScale(12);
	const auto radius = style::ConvertScale(20);
	const auto step = size + gap;
	const auto middleTop = top + (height - size) / 2
		- style::ConvertScale(24);
	const auto middleLeft = (width - size) / 2;
	const auto columns = (middleLeft + step - 1) / step;
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(kTileBg);
		for (auto y = middleTop - step; y < top + height; y += step) {
			for (auto x = middleLeft - columns * step; x < width; x += step) {
				p.drawRoundedRect(QRect(x, y, size, size), radius, radius);
			}
		}
	}

	// Fade the tiles out towards the bottom edge, like on mobile.
	const auto fadeTop = top + height / 2;
	auto fade = QLinearGradient(0, fadeTop, 0, top + height);
	auto transparent = kCoverBg;
	transparent.setAlpha(0);
	fade.setColorAt(0., transparent);
	fade.setColorAt(1., kCoverBg);
	p.fillRect(0, fadeTop, width, height / 2 + 1, fade);

	// White logo centered on the middle tile.
	const auto &logo = Logo();
	if (!logo.isNull()) {
		const auto logoWidth = size * 2 / 3;
		const auto logoHeight = logoWidth * logo.height() / logo.width();
		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(
			QRect(
				middleLeft + (size - logoWidth) / 2,
				middleTop + (size - logoHeight) / 2,
				logoWidth,
				logoHeight),
			logo);
	}
	p.restore();
}

} // namespace Luckgram
