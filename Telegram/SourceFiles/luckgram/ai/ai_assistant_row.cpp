/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_assistant_row.h"

#include "luckgram/ai/ai_assistant.h"
#include "luckgram/luckgram_features.h"
#include "luckgram/luckgram_settings.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_dialogs.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace Luckgram::AI {

not_null<Ui::RpWidget*> AddChatListRow(
		not_null<Ui::VerticalLayout*> list,
		not_null<Window::SessionController*> controller,
		rpl::producer<bool> shown) {
	const auto wrap = list->add(
		object_ptr<Ui::SlideWrap<Ui::AbstractButton>>(
			list,
			object_ptr<Ui::AbstractButton>(list)));
	const auto row = wrap->entity();
	const auto &st = st::defaultDialogRow;
	row->resize(row->width(), st.height);
	row->paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(row);
		const auto &st = st::defaultDialogRow;
		const auto over = row->isOver() || row->isDown();
		p.fillRect(clip, over ? st::dialogsBgOver : st::dialogsBg);

		// Android: the purple round icon (IconBackgroundColors.PURPLE).
		const auto size = st.photoSize;
		const auto icon = QRect(
			st.padding.left(),
			(st.height - size) / 2,
			size,
			size);
		{
			auto hq = PainterHighQualityEnabler(p);
			auto gradient = QLinearGradient(icon.topLeft(), icon.bottomLeft());
			gradient.setStops({
				{ 0., st::historyPeer5UserpicBg->c },
				{ 1., st::historyPeer5UserpicBg2->c },
			});
			p.setPen(Qt::NoPen);
			p.setBrush(gradient);
			p.drawEllipse(icon);
		}
		st::menuIconBot.paintInCenter(p, icon, st::historyPeerUserpicFg->c);

		p.setFont(st::semiboldFont);
		p.setPen(over ? st::dialogsNameFgOver : st::dialogsNameFg);
		const auto left = st.nameLeft;
		const auto available = row->width() - left - st.padding.right();
		p.drawText(
			QRect(left, 0, std::max(available, 0), st.height),
			Qt::AlignLeft | Qt::AlignVCenter,
			st::semiboldFont->elided(u"Luckgram AI"_q, available));
	}, row->lifetime());
	row->events() | rpl::filter([](not_null<QEvent*> e) {
		const auto type = e->type();
		return (type == QEvent::Enter)
			|| (type == QEvent::Leave)
			|| (type == QEvent::MouseButtonPress)
			|| (type == QEvent::MouseButtonRelease);
	}) | rpl::on_next([=] {
		row->update();
	}, row->lifetime());
	row->setClickedCallback([=] {
		OpenAssistant(controller);
	});

	wrap->toggleOn(rpl::combine(
		AssistantEnabledValue(),
		Settings::EnabledValue(
			QString::fromLatin1(Features::kAiAssistantRow)),
		std::move(shown)
	) | rpl::map([](bool assistant, bool row, bool shown) {
		return assistant && row && shown;
	}), anim::type::instant);
	wrap->finishAnimating();
	return wrap;
}

} // namespace Luckgram::AI
