/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "settings/sections/settings_luckgram_agents.h"

#include "luckgram/ai/ai_agents.h"
#include "luckgram/ai/ai_market.h"
#include "luckgram/pro/luckgram_pro.h"
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
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QDateTime>

#include "base/timer.h"

namespace Settings {
namespace {

namespace Agents = Luckgram::AI::Agents;
namespace Market = Luckgram::AI::Market;
using Luckgram::Tr;
using Luckgram::TrValue;

constexpr auto kRowHeight = 60;
constexpr auto kAvatarSize = 40;
constexpr auto kColorSize = 32;

[[nodiscard]] QColor Argb(quint32 value) {
	return QColor::fromRgba(QRgb(value));
}

struct RowData {
	quint32 color = 0;
	QString avatar; // Letter or emoji.
	QString title;
	QString subtitle;
};

[[nodiscard]] RowData RowOf(const Agents::Agent &agent, const QString &badge) {
	const auto chinese = Luckgram::IsChinese();
	auto title = agent.name(chinese);
	if (!badge.isEmpty()) {
		title += u"  ·  "_q + badge;
	}
	return {
		.color = agent.color,
		.avatar = agent.initial(chinese),
		.title = title,
		.subtitle = agent.description(chinese),
	};
}

// One agent: a colored round avatar with its initial (or emoji), the name
// and one line under it.
class AgentRow final : public Ui::RippleButton {
public:
	AgentRow(QWidget *parent, RowData data)
	: RippleButton(parent, st::defaultRippleAnimation)
	, _data(std::move(data)) {
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

		const auto left = st::settingsButtonNoIcon.padding.left();
		const auto top = (kRowHeight - kAvatarSize) / 2;
		{
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(Argb(_data.color));
			p.drawEllipse(left, top, kAvatarSize, kAvatarSize);
		}
		p.setPen(QColor(255, 255, 255));
		p.setFont(st::semiboldFont);
		p.drawText(
			QRect(left, top, kAvatarSize, kAvatarSize),
			Qt::AlignCenter,
			_data.avatar);

		const auto textLeft = left + kAvatarSize + left;
		const auto available = width() - textLeft - left;
		p.setPen(st::windowFg);
		p.setFont(st::semiboldFont);
		p.drawText(
			textLeft,
			top + st::semiboldFont->ascent,
			st::semiboldFont->elided(_data.title, available));
		p.setPen(st::windowSubTextFg);
		p.setFont(st::normalFont);
		p.drawText(
			textLeft,
			top + kAvatarSize - st::normalFont->descent,
			st::normalFont->elided(_data.subtitle, available));
	}

private:
	const RowData _data;

};

[[nodiscard]] not_null<AgentRow*> AddRow(
		not_null<Ui::VerticalLayout*> container,
		RowData data) {
	return container->add(object_ptr<AgentRow>(container, std::move(data)));
}

[[nodiscard]] RowData RowOf(const Market::Agent &agent, const QString &badge) {
	const auto chinese = Luckgram::IsChinese();
	auto title = agent.name.pick(chinese);
	if (agent.proOnly) {
		title += u"  ·  Pro"_q;
	}
	if (!badge.isEmpty()) {
		title += u"  ·  "_q + badge;
	}
	auto subtitle = agent.description.pick(chinese);
	if (agent.users > 0) {
		subtitle += u"  ·  "_q + Market::FormatUsers(agent.users, chinese);
	}
	return {
		.color = agent.color,
		.avatar = agent.avatarText(chinese),
		.title = title,
		.subtitle = subtitle,
	};
}

[[nodiscard]] not_null<AgentRow*> AddAgentRow(
		not_null<Ui::VerticalLayout*> container,
		const Agents::Agent &agent,
		const QString &badge = QString()) {
	return AddRow(container, RowOf(agent, badge));
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
	// Kept here: Ui::AbstractButton has no Q_OBJECT, so findChildren can't.
	const auto buttons = std::make_shared<
		std::vector<not_null<Ui::AbstractButton*>>>();
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
			for (const auto other : *buttons) {
				other->update();
			}
		});
		buttons->push_back(button);
	}
	wrap->widthValue() | rpl::on_next([=](int width) {
		auto x = 0;
		for (const auto button : *buttons) {
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
	rpl::merge(
		Agents::Mine::For(&controller->session()).changes(),
		Market::Service::For(&controller->session()).usedChanges()
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
	if (Market::Enabled()) {
		// 30 需求 1: the market (mobile: a tab next to 「我的」).
		AddButtonWithIcon(
			container,
			TrValue("浏览市场", "Browse market"),
			st::settingsButtonNoIcon
		)->addClickHandler([=] {
			_controller->showSettings(LuckgramAgentMarket::Id());
		});
		Ui::AddSkip(container);
		Ui::AddDivider(container);
		Ui::AddSkip(container);
	}
	Ui::AddSubsectionTitle(container, rpl::single(Agents::PopularText()));
	addAgents(Agents::Presets());
	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);

	Ui::AddSubsectionTitle(container, rpl::single(Agents::MineText()));
	addAgents(mine.custom());
	// 30 需求 6: market agents used before, most recent first.
	const auto session = &_controller->session();
	for (const auto &agent : Market::Service::For(session).used()) {
		const auto pinId = Agents::MarketAgentId(agent.id);
		const auto badge = mine.isPinned(pinId)
			? Tr("已在聊天列表", "In chat list")
			: Tr("来自市场", "From the market");
		const auto row = AddRow(container, RowOf(agent, badge));
		row->setClickedCallback([=] {
			Market::Open(_controller, agent);
		});
		Ui::SetupButtonContextMenu(row, &st::popupMenuExpandedSeparator, [=](
				not_null<Ui::PopupMenu*> menu) {
			const auto addAction = Ui::Menu::CreateAddActionCallback(menu);
			const auto pinned = Agents::Mine::For(session).isPinned(pinId);
			addAction(
				pinned ? RemoveFromChatListText() : AddToChatListText(),
				[=] { Agents::Mine::For(session).setPinned(pinId, !pinned); },
				pinned ? &st::menuIconUnpin : &st::menuIconPin);
			addAction(Tr("从「我的」移除", "Remove from My agents"), [=] {
				Agents::Mine::For(session).setPinned(pinId, false);
				Market::Service::For(session).removeUsed(agent.id);
			}, &st::menuIconDelete);
		});
	}
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

namespace {

// 30 需求 3: avatar, name, description, author, users, 「Pro 专享智能体」,
// three example questions (click: chat with it in the field), 开始聊天.
void ShowMarketAgentBox(
		not_null<Window::SessionController*> controller,
		const Market::Agent &shown) {
	const auto session = &controller->session();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto chinese = Luckgram::IsChinese();
		const auto agent = std::make_shared<Market::Agent>(shown);
		box->setTitle(rpl::single(shown.name.pick(chinese)));
		box->setWidth(st::boxWideWidth);
		const auto content = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			style::margins());
		const auto fill = [=] {
			content->clear();
			AddRow(content, RowOf(*agent, QString()));
			const auto addText = [&](const QString &text) {
				if (!text.isEmpty()) {
					content->add(
						object_ptr<Ui::FlatLabel>(
							content,
							rpl::single(text),
							st::boxLabel),
						st::boxRowPadding);
					Ui::AddSkip(content);
				}
			};
			addText(agent->description.pick(chinese));
			addText(Tr("Luckgram 官方", "By Luckgram"));
			if (agent->proOnly) {
				addText(Tr("Pro 专享智能体", "Pro agent"));
			}
			if (!agent->examples.empty()) {
				Ui::AddSubsectionTitle(content, TrValue("可以这样问", "Try asking"));
				for (const auto &example : agent->examples) {
					const auto text = example.pick(chinese);
					if (text.isEmpty()) {
						continue;
					}
					AddButtonWithIcon(
						content,
						rpl::single(text),
						st::settingsButtonNoIcon
					)->addClickHandler([=] {
						box->closeBox();
						Market::Open(controller, *agent, text);
					});
				}
			}
			content->resizeToWidth(box->width());
		};
		fill();
		// Fresh details (examples, Pro mark); the cached ones offline.
		const auto request = Market::Service::For(session).detail(
			shown.id,
			crl::guard(box, [=](Market::Agent fresh) {
				*agent = std::move(fresh);
				fill();
			}),
			crl::guard(box, [=](Luckgram::Api::Error error) {
				if (error.code == u"agent_unavailable"_q) {
					controller->showToast(Luckgram::Api::ErrorText(error.code));
					box->closeBox();
				}
			}));
		box->boxClosing() | rpl::on_next([=] {
			if (request) {
				request->cancel();
			}
		}, box->lifetime());
		box->addButton(TrValue("开始聊天", "Start chat"), [=] {
			box->closeBox();
			Market::Open(controller, *agent);
		});
		box->addButton(TrValue("取消", "Cancel"), [=] {
			box->closeBox();
		});
	}));
}

} // namespace

struct LuckgramAgentMarket::State {
	QString query;
	QString category; // "" = all.
	std::vector<Market::Category> categories;
	std::vector<Market::Agent> items;
	QString nextCursor;
	bool loading = false;
	bool offline = false;
	QString error;
	Luckgram::Api::RequestPtr request;
	Luckgram::Api::RequestPtr categoriesRequest;
	base::Timer searchTimer;
	rpl::variable<QString> categoryText;
	base::unique_qptr<Ui::PopupMenu> menu;
};

LuckgramAgentMarket::LuckgramAgentMarket(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _controller(controller)
, _content(Ui::CreateChild<Ui::VerticalLayout>(this))
, _list(_content->add(object_ptr<Ui::VerticalLayout>(_content)))
, _state(std::make_unique<State>()) {
	Ui::ResizeFitChild(this, _content);

	// Search field and category picker stay; only the list is rebuilt.
	const auto top = _content->insert(
		0,
		object_ptr<Ui::VerticalLayout>(_content));
	Ui::AddSkip(top);
	const auto search = top->add(
		object_ptr<Ui::InputField>(
			top,
			st::defaultInputField,
			Ui::InputField::Mode::SingleLine,
			TrValue("搜索智能体", "Search agents"),
			QString()),
		st::boxRowPadding);
	_state->searchTimer.setCallback([=] {
		_state->query = search->getLastText().trimmed();
		load(false);
	});
	search->changes() | rpl::on_next([=] {
		_state->searchTimer.callOnce(400);
	}, search->lifetime());
	Ui::AddSkip(top);
	_state->categoryText = Tr("全部", "All");
	AddButtonWithLabel(
		top,
		TrValue("分类", "Category"),
		_state->categoryText.value(),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		_state->menu = base::make_unique_q<Ui::PopupMenu>(
			this,
			st::popupMenuWithIcons);
		const auto menu = _state->menu.get();
		const auto pick = [=](const QString &id) {
			_state->category = id;
			load(false);
		};
		menu->addAction(Tr("全部", "All"), [=] { pick(QString()); });
		for (const auto &category : _state->categories) {
			const auto id = category.id;
			menu->addAction(
				category.name.pick(Luckgram::IsChinese()),
				[=] { pick(id); });
		}
		menu->popup(QCursor::pos());
	});
	Ui::AddSkip(top);
	Ui::AddDivider(top);

	const auto session = &controller->session();
	_state->categoriesRequest = Market::Service::For(session).categories(
		crl::guard(this, [=](std::vector<Market::Category> list) {
			_state->categories = std::move(list);
		}),
		[](Luckgram::Api::Error) {});
	load(false);
}

LuckgramAgentMarket::~LuckgramAgentMarket() {
	for (const auto &request : { _state->request, _state->categoriesRequest }) {
		if (request) {
			request->cancel();
		}
	}
}

rpl::producer<QString> LuckgramAgentMarket::title() {
	return rpl::single(Market::MarketText());
}

void LuckgramAgentMarket::load(bool more) {
	if (const auto request = base::take(_state->request)) {
		request->cancel();
	}
	if (!more) {
		_state->items.clear();
		_state->nextCursor = QString();
	}
	_state->loading = true;
	_state->error = QString();
	rebuildList();
	_state->request = Market::Service::For(&_controller->session()).list(
		_state->category,
		_state->query,
		more ? _state->nextCursor : QString(),
		crl::guard(this, [=](Market::Service::Loaded loaded) {
			_state->request = nullptr;
			_state->loading = false;
			_state->offline = loaded.offline;
			for (auto &agent : loaded.page.items) {
				_state->items.push_back(std::move(agent));
			}
			_state->nextCursor = loaded.page.nextCursor;
			rebuildList();
		}),
		crl::guard(this, [=](Luckgram::Api::Error error) {
			_state->request = nullptr;
			_state->loading = false;
			if (error.code != QString::fromLatin1(Luckgram::Api::kCancelled)) {
				_state->error = Luckgram::Api::ErrorText(error.code);
			}
			rebuildList();
		}));
}

void LuckgramAgentMarket::rebuildList() {
	const auto container = _list.get();
	container->clear();
	{
		auto name = Tr("全部", "All");
		for (const auto &category : _state->categories) {
			if (category.id == _state->category) {
				name = category.name.pick(Luckgram::IsChinese());
			}
		}
		_state->categoryText = name;
	}
	Ui::AddSkip(container);
	if (_state->offline) {
		AddButtonWithIcon(
			container,
			TrValue(
				"离线，显示的是缓存。点此重试",
				"Offline: showing saved results. Click to retry"),
			st::settingsButtonNoIcon
		)->addClickHandler([=] { load(false); });
	}
	for (const auto &agent : _state->items) {
		const auto row = AddRow(container, RowOf(agent, QString()));
		row->setClickedCallback([=] {
			ShowMarketAgentBox(_controller, agent);
		});
	}
	if (_state->loading) {
		Ui::AddDividerText(container, TrValue("加载中…", "Loading…"));
	} else if (!_state->error.isEmpty()) {
		AddButtonWithIcon(
			container,
			rpl::single(_state->error + u" "_q + Tr("点此重试", "Click to retry")),
			st::settingsButtonNoIcon
		)->addClickHandler([=] { load(false); });
	} else if (_state->items.empty()) {
		Ui::AddDividerText(container, _state->query.isEmpty()
			? TrValue("这里还没有智能体", "No agents here yet")
			: TrValue("没有找到相关智能体", "No matching agents"));
	} else if (!_state->nextCursor.isEmpty()) {
		AddButtonWithIcon(
			container,
			TrValue("更多", "More"),
			st::settingsButtonNoIcon
		)->addClickHandler([=] { load(true); });
	}
	Ui::AddSkip(container);
	container->resizeToWidth(width());
}

} // namespace Settings
