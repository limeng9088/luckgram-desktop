/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "settings/sections/settings_luckgram_agents.h"

#include "luckgram/ai/ai_agents.h"
#include "luckgram/ai/ai_store.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/button_context_menu.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtCore/QDateTime>

namespace Settings {
namespace {

namespace Agents = Luckgram::AI::Agents;
using Luckgram::Tr;
using Luckgram::TrValue;

constexpr auto kRowHeight = 60;
constexpr auto kAvatarSize = 40;
constexpr auto kColorSize = 32;

[[nodiscard]] QColor Argb(quint32 value) {
	return QColor::fromRgba(QRgb(value));
}

// One agent: a colored round avatar with its initial, the name and the
// one-line description.
class AgentRow final : public Ui::RippleButton {
public:
	AgentRow(
		QWidget *parent,
		const Agents::Agent &agent,
		const QString &badge)
	: RippleButton(parent, st::defaultRippleAnimation)
	, _agent(agent)
	, _badge(badge) {
		resize(width(), kRowHeight);
	}

protected:
	int resizeGetHeight(int newWidth) override {
		return kRowHeight;
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		const auto over = isOver() || isDown();
		p.fillRect(rect(), over ? st::windowBgOver : st::windowBg);
		paintRipple(p, 0, 0);

		const auto chinese = Luckgram::IsChinese();
		const auto left = st::settingsButtonNoIcon.padding.left();
		const auto top = (kRowHeight - kAvatarSize) / 2;
		{
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(Argb(_agent.color));
			p.drawEllipse(left, top, kAvatarSize, kAvatarSize);
		}
		p.setPen(QColor(255, 255, 255));
		p.setFont(st::semiboldFont);
		p.drawText(
			QRect(left, top, kAvatarSize, kAvatarSize),
			Qt::AlignCenter,
			_agent.initial(chinese));

		const auto textLeft = left + kAvatarSize + left;
		const auto available = width() - textLeft - left;
		auto name = _agent.name(chinese);
		if (!_badge.isEmpty()) {
			name += u"  ·  "_q + _badge;
		}
		p.setPen(st::windowFg);
		p.setFont(st::semiboldFont);
		p.drawText(
			textLeft,
			top + st::semiboldFont->ascent,
			st::semiboldFont->elided(name, available));
		p.setPen(st::windowSubTextFg);
		p.setFont(st::normalFont);
		p.drawText(
			textLeft,
			top + kAvatarSize - st::normalFont->descent,
			st::normalFont->elided(_agent.description(chinese), available));
	}

private:
	const Agents::Agent _agent;
	const QString _badge;

};

[[nodiscard]] not_null<AgentRow*> AddAgentRow(
		not_null<Ui::VerticalLayout*> container,
		const Agents::Agent &agent,
		const QString &badge = QString()) {
	return container->add(object_ptr<AgentRow>(container, agent, badge));
}

[[nodiscard]] QString AddToChatListText() {
	return Tr("添加到聊天列表", "Add to chat list");
}

[[nodiscard]] QString RemoveFromChatListText() {
	return Tr("从聊天列表移除", "Remove from chat list");
}

void ConfirmDelete(
		not_null<Window::SessionController*> controller,
		const Agents::Agent &agent,
		Fn<void()> done = nullptr) {
	const auto name = agent.name(Luckgram::IsChinese());
	const auto session = &controller->session();
	const auto id = agent.id;
	controller->show(Ui::MakeConfirmBox({
		.text = Luckgram::IsChinese()
			? (u"「"_q + name + u"」和它的对话都会删除。"_q)
			: (u"\""_q + name + u"\" and its chat will be deleted."_q),
		.confirmed = [=](Fn<void()> close) {
			Agents::Mine::For(session).remove(id);
			close();
			if (done) {
				done();
			}
		},
		.confirmText = Tr("删除", "Delete"),
		.confirmStyle = &st::attentionBoxButton,
		.title = Tr("删除智能体", "Delete agent"),
	}));
}

// Right-click (mobile: long press): pin, and for the user's own agents
// edit / delete.
void SetupAgentMenu(
		not_null<AgentRow*> row,
		not_null<Window::SessionController*> controller,
		const Agents::Agent &agent) {
	const auto session = &controller->session();
	Ui::SetupButtonContextMenu(row, &st::popupMenuExpandedSeparator, [=](
			not_null<Ui::PopupMenu*> menu) {
		const auto addAction = Ui::Menu::CreateAddActionCallback(menu);
		auto &mine = Agents::Mine::For(session);
		const auto pinned = mine.isPinned(agent.id);
		addAction(
			pinned ? RemoveFromChatListText() : AddToChatListText(),
			[=] { Agents::Mine::For(session).setPinned(agent.id, !pinned); },
			pinned ? &st::menuIconUnpin : &st::menuIconPin);
		if (!agent.preset) {
			addAction(Tr("编辑", "Edit"), [=] {
				ShowAgentEditBox(controller, agent.id);
			}, &st::menuIconEdit);
			addAction(Tr("删除", "Delete"), [=] {
				ConfirmDelete(controller, agent);
			}, &st::menuIconDelete);
		}
	});
}

// The avatar colors as a row of round buttons; the chosen one has a ring.
not_null<Ui::RpWidget*> AddColorPicker(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<quint32> chosen) {
	const auto &colors = Agents::Colors();
	const auto wrap = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::boxRowPadding);
	wrap->resize(wrap->width(), kColorSize + 8);
	auto buttons = std::vector<not_null<Ui::AbstractButton*>>();
	for (const auto color : colors) {
		const auto button = Ui::CreateChild<Ui::AbstractButton>(wrap);
		button->resize(kColorSize, kColorSize);
		button->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(button);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(Argb(color));
			const auto selected = (*chosen == color);
			const auto inset = selected ? 5 : 2;
			p.drawEllipse(QRect(0, 0, kColorSize, kColorSize).marginsRemoved(
				{ inset, inset, inset, inset }));
			if (selected) {
				auto pen = QPen(Argb(color));
				pen.setWidthF(2.);
				p.setPen(pen);
				p.setBrush(Qt::NoBrush);
				p.drawEllipse(QRectF(1, 1, kColorSize - 2, kColorSize - 2));
			}
		}, button->lifetime());
		button->setClickedCallback([=] {
			*chosen = color;
			for (const auto child : wrap->findChildren<Ui::AbstractButton*>()) {
				child->update();
			}
		});
		buttons.push_back(button);
	}
	wrap->widthValue() | rpl::on_next([=](int width) {
		auto x = 0;
		for (const auto button : buttons) {
			button->moveToLeft(x, 4, width);
			x += kColorSize + 8;
		}
	}, wrap->lifetime());
	return wrap;
}

} // namespace

LuckgramAgents::LuckgramAgents(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _controller(controller)
, _content(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	Ui::ResizeFitChild(this, _content);
	Agents::Mine::For(&controller->session()).changes(
	) | rpl::on_next([=] {
		rebuild();
	}, lifetime());
	rebuild();
}

rpl::producer<QString> LuckgramAgents::title() {
	return rpl::single(Agents::TitleText());
}

void LuckgramAgents::rebuild() {
	const auto container = _content.get();
	container->clear();
	auto &mine = Agents::Mine::For(&_controller->session());
	const auto pinBadge = [&](const Agents::Agent &agent) {
		return mine.isPinned(agent.id)
			? Tr("已在聊天列表", "In chat list")
			: QString();
	};
	const auto addAgents = [&](const std::vector<Agents::Agent> &list) {
		for (const auto &agent : list) {
			const auto row = AddAgentRow(container, agent, pinBadge(agent));
			row->setClickedCallback([=] {
				Agents::Open(_controller, agent);
			});
			SetupAgentMenu(row, _controller, agent);
		}
	};

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rpl::single(Agents::PopularText()));
	addAgents(Agents::Presets());
	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);

	Ui::AddSubsectionTitle(container, rpl::single(Agents::MineText()));
	addAgents(mine.custom());
	AddButtonWithIcon(
		container,
		rpl::single(Agents::CreateOwnText()),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		ShowAgentEditBox(_controller);
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		TrValue(
			"点击智能体开始对话；右键可以添加到聊天列表、编辑或删除你自己的智能体。",
			"Click an agent to chat. Right-click to add it to the chat list, or to edit or delete your own agents."));
	Ui::AddSkip(container);
	container->resizeToWidth(width());
}

void ShowAgentEditBox(
		not_null<Window::SessionController*> controller,
		const QString &agentId) {
	const auto session = &controller->session();
	const auto existing = agentId.isEmpty()
		? std::optional<Agents::Agent>()
		: Agents::Mine::For(session).find(agentId);
	if (!agentId.isEmpty() && (!existing || existing->preset)) {
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto chinese = Luckgram::IsChinese();
		box->setTitle(existing
			? TrValue("编辑智能体", "Edit agent")
			: TrValue("创建智能体", "New agent"));
		box->setWidth(st::boxWideWidth);

		const auto addField = [&](
				const QString &label,
				const QString &placeholder,
				const QString &value,
				bool multiline,
				int maxLength) {
			Ui::AddSubsectionTitle(
				box->verticalLayout(),
				rpl::single(label));
			const auto field = box->addRow(object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				multiline
					? Ui::InputField::Mode::MultiLine
					: Ui::InputField::Mode::SingleLine,
				rpl::single(placeholder),
				value));
			if (maxLength > 0) {
				field->setMaxLength(maxLength);
			}
			return field;
		};
		const auto name = addField(
			Tr("名称", "Name"),
			Tr("例如：只用英文回答的翻译官", "e.g. English-only translator"),
			existing ? existing->name(chinese) : QString(),
			false,
			Agents::kMaxName);
		const auto description = addField(
			Tr("简介", "Description"),
			Tr("一句话说明它做什么", "One line about what it does"),
			existing ? existing->description(chinese) : QString(),
			false,
			0);
		const auto prompt = addField(
			Tr("指令", "Instructions"),
			Tr(
				"告诉它是谁、怎么回答，例如：你是翻译官，无论用户说什么语言，都只用英文回答。",
				"Who it is and how to answer, e.g. You are a translator. Always answer in English only."),
			existing ? existing->prompt : QString(),
			true,
			Agents::kMaxPrompt);
		Ui::AddSubsectionTitle(
			box->verticalLayout(),
			TrValue("头像颜色", "Avatar color"));
		const auto color = std::make_shared<quint32>(existing
			? existing->color
			: Agents::Colors().front());
		AddColorPicker(box->verticalLayout(), color);
		box->setFocusCallback([=] { name->setFocusFast(); });

		box->addButton(TrValue("保存", "Save"), [=] {
			const auto nameText = name->getLastText().trimmed();
			const auto promptText = prompt->getLastText().trimmed();
			if (const auto invalid = Agents::Validate(nameText, promptText)) {
				const auto onName = (*invalid == Agents::Invalid::EmptyName
					|| *invalid == Agents::Invalid::NameTooLong);
				(onName ? name : prompt)->showError();
				controller->showToast(onName
					? Tr("请填写名称", "Please enter a name")
					: Tr("请填写指令", "Please enter instructions"));
				return;
			}
			const auto id = existing
				? existing->id
				: (u"custom_"_q + QString::number(
					QDateTime::currentMSecsSinceEpoch()));
			Agents::Mine::For(session).save(Agents::Custom(
				id,
				nameText,
				description->getLastText().trimmed(),
				promptText,
				*color));
			box->closeBox();
		});
		box->addButton(TrValue("取消", "Cancel"), [=] {
			box->closeBox();
		});
		if (existing) {
			const auto agent = *existing;
			box->addLeftButton(TrValue("删除智能体", "Delete agent"), [=] {
				ConfirmDelete(controller, agent, [=] { box->closeBox(); });
			}, st::attentionBoxButton);
		}
	}));
}

} // namespace Settings
