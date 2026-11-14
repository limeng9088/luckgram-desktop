/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_assistant.h"

#include "luckgram/ai/ai_agents.h"

#include "base/timer.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_web_page.h"
#include "history/history_item.h"
#include "luckgram/ai/ai_compose_logic.h"
#include "luckgram/ai/ai_conversation_logic.h"
#include "luckgram/ai/ai_markdown_logic.h"
#include "luckgram/ai/ai_registry.h"
#include "luckgram/ai/ai_store.h"
#include "luckgram/ai/ai_text_logic.h"
#include "luckgram/luckgram_ai.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_features.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/pro/luckgram_pro.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "settings/settings_common_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/text_entity.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QtCore/QFile>
#include <QtGui/QClipboard>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>

namespace Luckgram::AI {
namespace {

using Conversation = Conversations::Conversation;
using Message = Conversations::Message;

[[nodiscard]] int Scaled(int px) {
	return style::ConvertScale(px);
}

[[nodiscard]] int MaxInputChars() {
	return std::max(
		1000,
		int(Settings::RemoteNumber(
			u"ai.max_input_chars"_q,
			kDefaultMaxInputChars)));
}

// 18 需求 4: Markdown of a reply -> text entities of the bubble.
[[nodiscard]] TextWithEntities ToMarked(const QString &markdown) {
	const auto parsed = Markdown::Parse(markdown);
	auto result = TextWithEntities{ parsed.text };
	for (const auto &span : parsed.spans) {
		const auto length = span.end - span.start;
		if (length <= 0) {
			continue;
		}
		auto type = EntityType::Invalid;
		auto data = QString();
		switch (span.type) {
		case Markdown::Type::Bold:
		case Markdown::Type::Heading: type = EntityType::Bold; break;
		case Markdown::Type::Italic: type = EntityType::Italic; break;
		case Markdown::Type::Code: type = EntityType::Code; break;
		case Markdown::Type::CodeBlock:
			type = EntityType::Pre;
			data = span.value;
			break;
		case Markdown::Type::Link:
			type = EntityType::CustomUrl;
			data = span.value;
			break;
		case Markdown::Type::Strike: type = EntityType::StrikeOut; break;
		}
		if (type != EntityType::Invalid) {
			result.entities.push_back(
				EntityInText(type, span.start, length, data));
		}
	}
	// Inner spans are added before outer ones: sort by position.
	ranges::stable_sort(result.entities, [](
			const EntityInText &a,
			const EntityInText &b) {
		return (a.offset() != b.offset())
			? (a.offset() < b.offset())
			: (a.length() > b.length());
	});
	return result;
}

[[nodiscard]] Message NewMessage(const char *role, const QString &text) {
	auto result = Message();
	result.id = NewId();
	result.role = QString::fromLatin1(role);
	result.text = text;
	result.time = NowMs();
	return result;
}

[[nodiscard]] Conversation NewConversation() {
	auto result = Conversation();
	result.id = NewId();
	result.task = QString::fromLatin1(kTaskChat);
	result.createdAt = result.updatedAt = NowMs();
	return result;
}

// One message: the bubble (text, registered type or a placeholder) and
// its links (复制 / 重新生成 / 重试).
class MessageRow final : public Ui::RpWidget {
public:
	struct Actions {
		bool copy = false;
		bool regenerate = false;
		bool retry = false;
	};

	MessageRow(
		QWidget *parent,
		not_null<Host*> host,
		const Message &message,
		Fn<void(QString)> copy,
		Fn<void()> regenerate);

	[[nodiscard]] const Message &message() const {
		return _message;
	}
	void setMessage(const Message &message);
	void setActions(Actions actions);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	[[nodiscard]] bool isUser() const;
	[[nodiscard]] bool isTextLike() const;
	void rebuildContent();
	void updateLabel();

	const not_null<Host*> _host;
	Message _message;
	Fn<void(QString)> _copy;
	Fn<void()> _regenerate;
	Actions _actions;
	object_ptr<Ui::RpWidget> _content = { nullptr };
	Ui::FlatLabel *_label = nullptr;
	std::vector<object_ptr<Ui::LinkButton>> _links;
	QRect _bubble;

};

MessageRow::MessageRow(
	QWidget *parent,
	not_null<Host*> host,
	const Message &message,
	Fn<void(QString)> copy,
	Fn<void()> regenerate)
: RpWidget(parent)
, _host(host)
, _message(message)
, _copy(std::move(copy))
, _regenerate(std::move(regenerate)) {
	rebuildContent();
}

bool MessageRow::isUser() const {
	return _message.isUser();
}

bool MessageRow::isTextLike() const {
	return (_message.type == QLatin1String(Conversations::kTypeText))
		|| !FindMessageType(_message.type);
}

void MessageRow::setMessage(const Message &message) {
	const auto typeChanged = (message.type != _message.type);
	_message = message;
	if (typeChanged || !_label || !isTextLike()) {
		rebuildContent();
	} else {
		updateLabel();
	}
	resizeToWidth(width());
	update();
}

void MessageRow::rebuildContent() {
	_label = nullptr;
	if (isTextLike()) {
		auto label = object_ptr<Ui::FlatLabel>(this, st::luckgramAiTextLabel);
		_label = label.data();
		_label->setSelectable(true);
		_content = std::move(label);
		updateLabel();
	} else {
		const auto registered = FindMessageType(_message.type);
		_content = registered->create(this, _host, _message);
		if (!_content) {
			// A type that failed to build: the placeholder.
			auto label = object_ptr<Ui::FlatLabel>(this, st::luckgramAiTextLabel);
			_label = label.data();
			_content = std::move(label);
			updateLabel();
		}
	}
	_content->show();
}

void MessageRow::updateLabel() {
	if (!_label) {
		return;
	}
	const auto known = (_message.type
		== QLatin1String(Conversations::kTypeText));
	if (!known) {
		// 18 需求 8: unknown types show a placeholder, never crash.
		_label->setText(Tr("此消息类型暂不支持显示", "This message can't be shown"));
		_label->setTextColorOverride(st::windowSubTextFg->c);
		return;
	}
	const auto text = _message.text.value_or(QString());
	if (_message.errorCode) {
		// 18 需求 7: 00's error texts in the bubble.
		const auto error = Api::ErrorText(*_message.errorCode);
		_label->setText(text.isEmpty() ? error : (text + u"\n\n"_q + error));
		_label->setTextColorOverride(st::attentionButtonFg->c);
		return;
	}
	_label->setTextColorOverride(std::nullopt);
	if (_message.pending && text.isEmpty()) {
		_label->setText(QString::fromUtf8("…"));
	} else if (isUser()) {
		_label->setMarkedText(TextWithEntities{ text });
	} else {
		_label->setMarkedText(ToMarked(text));
	}
}

void MessageRow::setActions(Actions actions) {
	if (_actions.copy == actions.copy
		&& _actions.regenerate == actions.regenerate
		&& _actions.retry == actions.retry
		&& !_links.empty() == (actions.copy || actions.regenerate || actions.retry)) {
		return;
	}
	_actions = actions;
	_links.clear();
	const auto add = [&](QString text, Fn<void()> callback) {
		auto link = object_ptr<Ui::LinkButton>(this, text);
		link->setClickedCallback(std::move(callback));
		link->show();
		_links.push_back(std::move(link));
	};
	if (_actions.copy) {
		add(Tr("复制", "Copy"), [=] {
			_copy(_message.text.value_or(QString()));
		});
	}
	if (_actions.retry) {
		add(Tr("重试", "Retry"), [=] { _regenerate(); });
	} else if (_actions.regenerate) {
		// D7 需求 1 (desktop): 重新生成 the last reply.
		add(Tr("重新生成", "Regenerate"), [=] { _regenerate(); });
	}
	resizeToWidth(width());
}

int MessageRow::resizeGetHeight(int newWidth) {
	if (!_content || newWidth <= 0) {
		return 0;
	}
	const auto padding = QMargins(
		Scaled(12),
		Scaled(8),
		Scaled(12),
		Scaled(8));
	const auto side = Scaled(16);
	const auto skip = Scaled(4);
	const auto maxBubble = std::max(
		Scaled(120),
		(newWidth - 2 * side) * 85 / 100);
	const auto maxContent = maxBubble - padding.left() - padding.right();
	auto contentWidth = maxContent;
	if (_label) {
		contentWidth = std::clamp(_label->textMaxWidth(), Scaled(16), maxContent);
	}
	_content->resizeToWidth(contentWidth);
	const auto bubbleWidth = contentWidth + padding.left() + padding.right();
	const auto left = isUser() ? (newWidth - side - bubbleWidth) : side;
	_bubble = QRect(
		left,
		skip,
		bubbleWidth,
		_content->height() + padding.top() + padding.bottom());
	_content->moveToLeft(left + padding.left(), skip + padding.top(), newWidth);
	auto bottom = _bubble.y() + _bubble.height();
	if (!_links.empty()) {
		const auto top = bottom + skip;
		auto x = left + Scaled(4);
		auto height = 0;
		for (const auto &link : _links) {
			link->moveToLeft(x, top, newWidth);
			x += link->width() + Scaled(16);
			height = std::max(height, link->height());
		}
		bottom = top + height;
	}
	return bottom + skip;
}

void MessageRow::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(isUser() ? st::msgOutBg : st::windowBgOver);
	const auto radius = Scaled(12);
	p.drawRoundedRect(_bubble, radius, radius);
}

// The assistant page (18 需求 2): a local chat, not a Telegram one.
class ChatSection final : public ::Settings::AbstractSection, public Host {
public:
	ChatSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		QString conversationId,
		QString inputText);
	~ChatSection();

	[[nodiscard]] ::Settings::Type id() const override;
	[[nodiscard]] rpl::producer<QString> title() override;
	void fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) override;
	base::weak_qptr<Ui::RpWidget> createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) override;
	void setInnerFocus() override;

	// Host (18 需求 8).
	not_null<Window::SessionController*> sessionController() override;
	Conversation conversation() override;
	QString inputText() override;
	void setInputText(const QString &text) override;
	QString addMessage(Message message) override;
	void updateMessage(const Message &message) override;
	void send(const QString &text) override;

private:
	void setupTop();
	void addRow(const Message &message);
	void refreshRows();
	void updateActions();
	void save();
	void sendConfirmed(const QString &text);
	void request();
	void finish(const QString &text, const QString &errorCode);
	void stop();
	void regenerate();
	void copy(const QString &text);
	void scrollToBottomLater();
	[[nodiscard]] Message *findMessage(const QString &id);
	[[nodiscard]] MessageRow *findRow(const QString &id) const;
	void layoutInput(int width);
	void showStreamed();

	const not_null<Ui::ScrollArea*> _scroll;
	Conversation _conversation;
	bool _saved = false;
	QString _initialInput;
	const not_null<Ui::VerticalLayout*> _list;
	Ui::RpWidget *_welcome = nullptr;
	std::vector<MessageRow*> _rows;
	Ui::RpWidget *_inputRow = nullptr;
	Ui::InputField *_field = nullptr;
	Ui::RoundButton *_send = nullptr;
	Api::RequestPtr _request;
	QString _pendingId;
	// Streaming re-lays out the whole reply, so redraw at most once
	// per kStreamRedrawMs instead of on every piece.
	base::Timer _streamTimer;
	rpl::variable<bool> _busy = false;
	bool _stickToBottom = true;

};

struct ChatFactory final : ::Settings::AbstractSectionFactory {
	ChatFactory(QString conversationId, QString inputText)
	: conversationId(std::move(conversationId))
	, inputText(std::move(inputText)) {
	}

	object_ptr<::Settings::AbstractSection> create(
			not_null<QWidget*> parent,
			not_null<Window::SessionController*> controller,
			not_null<Ui::ScrollArea*> scroll,
			rpl::producer<::Settings::Container> containerValue
	) const final override {
		return object_ptr<ChatSection>(
			parent,
			controller,
			scroll,
			conversationId,
			inputText);
	}

	const QString conversationId;
	const QString inputText;
};

// The conversation of OpenNewConversation (an id no stored one has).
constexpr auto kNewConversation = "*new*";
constexpr auto kStreamRedrawMs = crl::time(80);

ChatSection::ChatSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Ui::ScrollArea*> scroll,
	QString conversationId,
	QString inputText)
: AbstractSection(parent, controller)
, _scroll(scroll)
, _initialInput(std::move(inputText))
, _list(Ui::CreateChild<Ui::VerticalLayout>(this))
, _streamTimer([=] { showStreamed(); }) {
	RegisterAll();
	AddOpenHost(this);
	const auto &store = Store::For(&controller->session());
	auto found = std::optional<Conversation>();
	if (conversationId == QLatin1String(kNewConversation)) {
	} else if (!conversationId.isEmpty()) {
		found = store.find(conversationId);
	} else {
		const auto list = store.list();
		if (!list.empty()) {
			found = list.front();
		}
	}
	_saved = found.has_value();
	_conversation = found ? std::move(*found) : NewConversation();

	Ui::ResizeFitChild(this, _list);
	setupTop();
	for (const auto &message : _conversation.messages) {
		addRow(message);
	}
	updateActions();

	_scroll->scrolls() | rpl::on_next([=] {
		_stickToBottom = (_scroll->scrollTop()
			>= _scroll->scrollTopMax() - Scaled(40));
	}, lifetime());
	_list->heightValue() | rpl::on_next([=] {
		if (_stickToBottom) {
			scrollToBottomLater();
		}
	}, lifetime());

	// Removed elsewhere (history page): show a new conversation.
	Store::For(&controller->session()).changes(
	) | rpl::on_next([=](const QString &id) {
		if ((id.isEmpty() || id == _conversation.id)
			&& _saved
			&& !Store::For(&this->controller()->session()).find(
				_conversation.id)) {
			stop();
			_conversation = NewConversation();
			_saved = false;
			refreshRows();
		}
	}, lifetime());
}

ChatSection::~ChatSection() {
	RemoveOpenHost(this);
	if (const auto request = base::take(_request)) {
		request->cancel();
	}
}

::Settings::Type ChatSection::id() const {
	return std::make_shared<ChatFactory>(_conversation.id, QString());
}

rpl::producer<QString> ChatSection::title() {
	// 18 需求 8 "对话属性": a feature's conversation (agent:<id>, ...) shows
	// its own name; ordinary chats are "Luckgram AI".
	const auto &title = _conversation.title;
	return rpl::single((_conversation.key && title && !title->isEmpty())
		? *title
		: u"Luckgram AI"_q);
}

void ChatSection::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	const auto controller = this->controller();
	addAction(Tr("新对话", "New chat"), [=] {
		OpenNewConversation(controller);
	}, &st::menuIconDiscussion);
	addAction(Tr("历史对话", "Chat history"), [=] {
		OpenHistory(controller);
	}, &st::menuIconRestore);
	// 18 需求 8 "页面顶部入口" (22 智能体...), also as links on the page.
	for (const auto &entry : TopEntries()) {
		const auto open = entry.open;
		addAction(Tr(entry.titleZh, entry.titleEn), [=] {
			open(controller);
		}, &st::menuIconBot);
	}
}

void ChatSection::setupTop() {
	const auto entries = TopEntries();
	if (!entries.empty()) {
		const auto row = _list->add(
			object_ptr<Ui::RpWidget>(_list),
			QMargins(Scaled(16), Scaled(8), Scaled(16), 0));
		auto links = std::vector<not_null<Ui::LinkButton*>>();
		const auto controller = this->controller();
		for (const auto &entry : entries) {
			const auto link = Ui::CreateChild<Ui::LinkButton>(
				row,
				Tr(entry.titleZh, entry.titleEn));
			const auto open = entry.open;
			link->setClickedCallback([=] { open(controller); });
			links.push_back(link);
		}
		row->widthValue() | rpl::on_next([=](int width) {
			auto x = 0;
			auto height = 0;
			for (const auto &link : links) {
				link->moveToLeft(x, 0, width);
				x += link->width() + Scaled(16);
				height = std::max(height, link->height());
			}
			row->resize(width, height);
		}, row->lifetime());
	}

	// Empty state (mobile: "有什么可以帮你？").
	const auto welcome = _list->add(
		object_ptr<Ui::VerticalLayout>(_list),
		QMargins(Scaled(16), Scaled(24), Scaled(16), Scaled(8)));
	welcome->add(object_ptr<Ui::FlatLabel>(
		welcome,
		Tr("有什么可以帮你？", "How can I help?"),
		st::boxTitle));
	welcome->add(
		object_ptr<Ui::FlatLabel>(
			welcome,
			Tr(
				"可以提问、总结或改写长文、讲解、解题。",
				"Ask anything, summarize or rewrite text, explain, solve problems."),
			st::luckgramAiSubTextLabel),
		QMargins(0, Scaled(6), 0, 0));
	_welcome = welcome;
	_welcome->setVisible(_conversation.messages.empty());
}

void ChatSection::addRow(const Message &message) {
	const auto row = _list->add(object_ptr<MessageRow>(
		_list,
		this,
		message,
		[=](QString text) { copy(text); },
		[=] { regenerate(); }));
	_rows.push_back(row);
	if (_welcome) {
		_welcome->hide();
	}
	_list->resizeToWidth(width());
}

void ChatSection::refreshRows() {
	for (const auto row : base::take(_rows)) {
		delete row;
	}
	for (const auto &message : _conversation.messages) {
		addRow(message);
	}
	if (_welcome) {
		_welcome->setVisible(_conversation.messages.empty());
	}
	updateActions();
	_list->resizeToWidth(width());
}

void ChatSection::updateActions() {
	const auto busy = _busy.current();
	auto lastAssistant = -1;
	for (auto i = 0; i != int(_rows.size()); ++i) {
		if (!_rows[i]->message().isUser()) {
			lastAssistant = i;
		}
	}
	for (auto i = 0; i != int(_rows.size()); ++i) {
		const auto &message = _rows[i]->message();
		const auto user = message.isUser();
		const auto text = (message.type
			== QLatin1String(Conversations::kTypeText));
		const auto last = (i == lastAssistant) && (i + 1 == int(_rows.size()));
		_rows[i]->setActions({
			.copy = !user
				&& text
				&& !message.pending
				&& !message.text.value_or(QString()).isEmpty(),
			.regenerate = last && !busy && !message.pending,
			.retry = last && !busy && message.errorCode.has_value(),
		});
	}
}

void ChatSection::save() {
	_conversation.updatedAt = NowMs();
	Store::For(&controller()->session()).save(_conversation);
	_saved = true;
}

base::weak_qptr<Ui::RpWidget> ChatSection::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(parent.get());
	content->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(content).fillRect(clip, st::windowBg);
	}, content->lifetime());

	// 18 需求 3: quick buttons above the field.
	const auto buttons = content->add(
		object_ptr<Ui::RpWidget>(content),
		QMargins(Scaled(12), Scaled(8), Scaled(12), 0));
	auto list = std::vector<not_null<Ui::RoundButton*>>();
	for (const auto &button : QuickButtons()) {
		const auto raw = Ui::CreateChild<Ui::RoundButton>(
			buttons,
			rpl::single(Tr(button.titleZh, button.titleEn)),
			st::defaultLightButton);
		const auto handler = button.handler;
		const auto prefix = IsChinese() ? button.prefixZh : button.prefixEn;
		raw->setClickedCallback([=] {
			if (handler) {
				handler(this);
			} else if (_field) {
				setInputText(Text::FillPrefix(_field->getLastText(), prefix));
				_field->setFocusFast();
			}
		});
		list.push_back(raw);
	}
	buttons->widthValue() | rpl::on_next([=](int width) {
		auto x = 0;
		auto y = 0;
		auto line = 0;
		for (const auto &raw : list) {
			if (x > 0 && x + raw->width() > width) {
				x = 0;
				y += line + Scaled(6);
				line = 0;
			}
			raw->moveToLeft(x, y, width);
			x += raw->width() + Scaled(6);
			line = std::max(line, raw->height());
		}
		buttons->resize(width, list.empty() ? 0 : (y + line));
	}, buttons->lifetime());

	// The field and 发送 / 停止.
	_inputRow = content->add(
		object_ptr<Ui::RpWidget>(content),
		QMargins(Scaled(12), Scaled(8), Scaled(12), Scaled(10)));
	_field = Ui::CreateChild<Ui::InputField>(
		_inputRow,
		st::historyComposeField,
		Ui::InputField::Mode::MultiLine,
		TrValue("给 Luckgram AI 发消息", "Message Luckgram AI"),
		_initialInput);
	_field->setMaxHeight(st::historyComposeFieldMaxHeight);
	_field->setSubmitSettings(Ui::InputField::SubmitSettings::Enter);
	_send = Ui::CreateChild<Ui::RoundButton>(
		_inputRow,
		_busy.value() | rpl::map([](bool busy) {
			return busy ? Tr("停止", "Stop") : Tr("发送", "Send");
		}),
		st::defaultActiveButton);
	_send->setClickedCallback([=] {
		if (_busy.current()) {
			stop();
		} else {
			send(_field->getLastText());
		}
	});
	_field->submits() | rpl::on_next([=] {
		if (!_busy.current()) {
			send(_field->getLastText());
		}
	}, _field->lifetime());
	rpl::combine(
		_inputRow->widthValue(),
		_field->heightValue(),
		_send->widthValue()
	) | rpl::on_next([=](int width, int, int) {
		layoutInput(width);
	}, _inputRow->lifetime());

	if (!_initialInput.isEmpty()) {
		_field->setCursorPosition(int(_initialInput.size()));
	}
	return base::make_weak(content);
}

void ChatSection::layoutInput(int width) {
	if (!_inputRow || !_field || !_send || width <= 0) {
		return;
	}
	const auto skip = Scaled(8);
	const auto fieldWidth = std::max(
		Scaled(40),
		width - _send->width() - skip);
	_field->resizeToWidth(fieldWidth);
	const auto height = std::max(_field->height(), _send->height());
	_field->moveToLeft(0, (height - _field->height()) / 2, width);
	_send->moveToLeft(
		fieldWidth + skip,
		height - _send->height(),
		width);
	_inputRow->resize(width, height);
}

void ChatSection::setInnerFocus() {
	if (_field) {
		_field->setFocusFast();
	} else {
		setFocus();
	}
}

not_null<Window::SessionController*> ChatSection::sessionController() {
	return controller();
}

Conversation ChatSection::conversation() {
	return _conversation;
}

QString ChatSection::inputText() {
	return _field ? _field->getLastText() : _initialInput;
}

void ChatSection::setInputText(const QString &text) {
	if (_field) {
		_field->setText(text);
		_field->setCursorPosition(int(text.size()));
	} else {
		_initialInput = text;
	}
}

QString ChatSection::addMessage(Message message) {
	if (message.id.isEmpty()) {
		message.id = NewId();
	}
	if (!message.time) {
		message.time = NowMs();
	}
	if (!_conversation.title
		&& message.isUser()
		&& message.text
		&& !message.text->trimmed().isEmpty()) {
		_conversation.title = Conversations::AutoTitle(*message.text);
	}
	_conversation.messages.push_back(message);
	addRow(message);
	if (!message.pending) {
		save();
	}
	updateActions();
	return message.id;
}

void ChatSection::updateMessage(const Message &message) {
	if (const auto existing = findMessage(message.id)) {
		*existing = message;
		if (const auto row = findRow(message.id)) {
			row->setMessage(message);
		}
		if (!message.pending) {
			save();
		}
		updateActions();
	}
}

Message *ChatSection::findMessage(const QString &id) {
	for (auto &message : _conversation.messages) {
		if (message.id == id) {
			return &message;
		}
	}
	return nullptr;
}

MessageRow *ChatSection::findRow(const QString &id) const {
	for (const auto row : _rows) {
		if (row->message().id == id) {
			return row;
		}
	}
	return nullptr;
}

void ChatSection::send(const QString &text) {
	const auto trimmed = text.trimmed();
	if (trimmed.isEmpty() || _busy.current()) {
		return;
	}
	// 18 需求 8: input interceptors (23 "画" / "/image").
	if (const auto interceptor = FindInterceptor(trimmed)) {
		if (_field) {
			_field->clear();
		}
		interceptor->handle(this, trimmed);
		return;
	}
	// 00: the consent prompt before anything is sent.
	WithConsent(controller()->uiShow(), crl::guard(this, [=] {
		sendConfirmed(trimmed);
	}));
}

void ChatSection::sendConfirmed(const QString &text) {
	if (_busy.current()) {
		return;
	}
	if (_field) {
		_field->clear();
	}
	addMessage(NewMessage(Conversations::kRoleUser, text));
	request();
}

void ChatSection::request() {
	auto reply = NewMessage(Conversations::kRoleAssistant, QString());
	reply.pending = true;
	_pendingId = reply.id;
	_busy = true;
	addMessage(std::move(reply));

	auto args = ChatArgs();
	args.task = _conversation.task;
	args.extra = _conversation.params;
	args.stream = true;
	const auto messages = Conversations::RequestMessages(
		_conversation,
		MaxInputChars(),
		ContextTextOf);
	for (const auto &message : messages) {
		args.messages.push_back({ message.role, message.content });
	}
	_request = Chat(
		std::move(args),
		crl::guard(this, [=](QString, QString soFar) {
			if (const auto pending = findMessage(_pendingId)) {
				pending->text = soFar;
				if (!_streamTimer.isActive()) {
					_streamTimer.callOnce(kStreamRedrawMs);
				}
			}
		}),
		crl::guard(this, [=](QString text) {
			finish(text, QString());
		}),
		crl::guard(this, [=](Api::Error error) {
			// 29 需求 6: not_pro / quota_exceeded / credits_exhausted open
			// the upgrade box; the bubble still shows the error.
			Pro::HandleError(controller(), error);
			const auto pending = findMessage(_pendingId);
			finish(
				pending ? pending->text.value_or(QString()) : QString(),
				error.code);
		}));
}

void ChatSection::showStreamed() {
	if (const auto pending = findMessage(_pendingId)) {
		if (const auto row = findRow(_pendingId)) {
			row->setMessage(*pending);
		}
	}
}

void ChatSection::finish(const QString &text, const QString &errorCode) {
	_request = nullptr;
	_streamTimer.cancel();
	const auto id = base::take(_pendingId);
	_busy = false;
	auto message = findMessage(id);
	if (!message) {
		updateActions();
		return;
	}
	message->pending = false;
	message->text = text;
	if (!errorCode.isEmpty()
		&& errorCode != QLatin1String(Api::kCancelled)) {
		message->errorCode = errorCode;
	}
	if (!message->errorCode && text.isEmpty()) {
		// Stopped before anything arrived: nothing to keep.
		_conversation.messages.erase(
			ranges::remove(_conversation.messages, id, &Message::id),
			end(_conversation.messages));
		if (const auto row = findRow(id)) {
			_rows.erase(ranges::remove(_rows, row), end(_rows));
			delete row;
		}
		save();
		updateActions();
		return;
	}
	updateMessage(*message);
}

void ChatSection::stop() {
	if (const auto request = base::take(_request)) {
		request->cancel();
		const auto pending = findMessage(_pendingId);
		finish(
			pending ? pending->text.value_or(QString()) : QString(),
			QString::fromLatin1(Api::kCancelled));
	}
}

void ChatSection::regenerate() {
	if (_busy.current() || _rows.empty()) {
		return;
	}
	const auto last = _rows.back();
	if (last->message().isUser()) {
		return;
	}
	const auto id = last->message().id;
	_conversation.messages.erase(
		ranges::remove(_conversation.messages, id, &Message::id),
		end(_conversation.messages));
	_rows.pop_back();
	delete last;
	save();
	WithConsent(controller()->uiShow(), crl::guard(this, [=] {
		request();
	}));
}

void ChatSection::copy(const QString &text) {
	QGuiApplication::clipboard()->setText(text);
	controller()->showToast(Tr("已复制", "Copied"));
}

void ChatSection::scrollToBottomLater() {
	crl::on_main(this, [=] {
		_scroll->scrollToY(_scroll->scrollTopMax());
	});
}

// 18 需求 6: the history list.
class HistorySection final : public ::Settings::AbstractSection {
public:
	HistorySection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] ::Settings::Type id() const override;
	[[nodiscard]] rpl::producer<QString> title() override;
	void fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) override;

private:
	void rebuild();
	void showMenu(const QString &id);

	const not_null<Ui::VerticalLayout*> _list;
	base::unique_qptr<Ui::PopupMenu> _menu;

};

struct HistoryFactory final : ::Settings::AbstractSectionFactory {
	object_ptr<::Settings::AbstractSection> create(
			not_null<QWidget*> parent,
			not_null<Window::SessionController*> controller,
			not_null<Ui::ScrollArea*> scroll,
			rpl::producer<::Settings::Container> containerValue
	) const final override {
		return object_ptr<HistorySection>(parent, controller);
	}
};

HistorySection::HistorySection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: AbstractSection(parent, controller)
, _list(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	Ui::ResizeFitChild(this, _list);
	rebuild();
	Store::For(&controller->session()).changes(
	) | rpl::on_next([=] {
		rebuild();
	}, lifetime());
}

::Settings::Type HistorySection::id() const {
	return std::make_shared<HistoryFactory>();
}

rpl::producer<QString> HistorySection::title() {
	return TrValue("历史对话", "Chat history");
}

void HistorySection::fillTopBarMenu(
		const Ui::Menu::MenuCallback &addAction) {
	const auto controller = this->controller();
	addAction(Tr("新对话", "New chat"), [=] {
		OpenNewConversation(controller);
	}, &st::menuIconDiscussion);
}

void HistorySection::rebuild() {
	_list->clear();
	const auto controller = this->controller();
	const auto list = Store::For(&controller->session()).list();
	if (list.empty()) {
		_list->add(
			object_ptr<Ui::FlatLabel>(
				_list,
				Tr("还没有对话", "No chats yet"),
				st::defaultSubTextLabel),
			QMargins(Scaled(22), Scaled(16), Scaled(22), Scaled(16)));
	}
	for (const auto &conversation : list) {
		const auto title = (conversation.title && !conversation.title->isEmpty())
			? *conversation.title
			: Tr("新对话", "New chat");
		const auto preview = Conversations::PreviewText(conversation);
		const auto button = ::Settings::AddButtonWithLabel(
			_list,
			rpl::single(title),
			rpl::single(Text::SafeLeft(preview, 60)),
			st::settingsButtonNoIcon);
		const auto id = conversation.id;
		button->setAcceptBoth(true); // Right click too.
		button->addClickHandler([=](Qt::MouseButton which) {
			if (which == Qt::LeftButton) {
				OpenAssistant(controller, id);
			} else if (which == Qt::RightButton) {
				// Mobile: long press. Desktop: right click → 删除对话.
				showMenu(id);
			}
		});
	}
	_list->resizeToWidth(width());
}

void HistorySection::showMenu(const QString &id) {
	const auto controller = this->controller();
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	_menu->addAction(Tr("删除对话", "Delete chat"), [=] {
		controller->show(Ui::MakeConfirmBox({
			.text = Tr("删除后无法恢复。", "This can't be undone."),
			.confirmed = [=](Fn<void()> close) {
				Store::For(&controller->session()).remove(id);
				close();
			},
			.confirmText = Tr("删除", "Delete"),
			.confirmStyle = &st::attentionBoxButton,
			.title = Tr("删除对话", "Delete chat"),
		}));
	}, &st::menuIconDelete);
	_menu->popup(QCursor::pos());
}

[[nodiscard]] Ask::Part PartOf(not_null<HistoryItem*> item) {
	auto result = Ask::Part();
	const auto &text = item->originalText();
	result.text = text.text;
	for (const auto &entity : text.entities) {
		if (entity.type() == EntityType::CustomUrl && !entity.data().isEmpty()) {
			result.links.push_back(entity.data());
		}
	}
	const auto media = item->media();
	if (!media) {
		return result;
	}
	if (const auto webpage = media->webpage()) {
		result.webpageUrl = webpage->url;
	}
	const auto document = media->document();
	if (!document
		|| document->isVoiceMessage()
		|| document->isVideoMessage()
		|| document->sticker()) {
		return result;
	}
	result.fileName = document->filename();
	if (document->size > 0
		&& document->size <= Ask::kMaxTextFile
		&& Ask::IsTextFile(document->mimeString(), result.fileName)) {
		const auto path = document->filepath(true);
		auto file = QFile(path);
		if (!path.isEmpty()
			&& file.size() <= Ask::kMaxTextFile
			&& file.open(QIODevice::ReadOnly)) {
			result.fileContent = QString::fromUtf8(file.readAll());
		}
	}
	return result;
}

} // namespace

bool AssistantEnabled() {
	return Settings::IsEnabled(QString::fromLatin1(Features::kAiAssistant));
}

rpl::producer<bool> AssistantEnabledValue() {
	return Settings::EnabledValue(QString::fromLatin1(Features::kAiAssistant));
}

void OpenAssistant(
		not_null<Window::SessionController*> controller,
		const QString &conversationId,
		const QString &inputText) {
	if (!AssistantEnabled()) {
		return;
	}
	controller->showSettings(
		std::make_shared<ChatFactory>(conversationId, inputText));
}

void OpenNewConversation(
		not_null<Window::SessionController*> controller,
		const QString &inputText) {
	OpenAssistant(
		controller,
		QString::fromLatin1(kNewConversation),
		inputText);
}

void OpenHistory(not_null<Window::SessionController*> controller) {
	if (!AssistantEnabled()) {
		return;
	}
	controller->showSettings(std::make_shared<HistoryFactory>());
}

void AddMainMenuEntry(
		not_null<Ui::VerticalLayout*> menu,
		not_null<Window::SessionController*> controller) {
	const auto wrap = menu->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			menu,
			::Settings::CreateButtonWithIcon(
				menu,
				rpl::single(u"Luckgram AI"_q),
				st::mainMenuButton,
				{ &st::menuIconBot })));
	wrap->toggleOn(AssistantEnabledValue());
	wrap->finishAnimating();
	wrap->entity()->setClickedCallback([=] {
		OpenAssistant(controller);
	});
	Agents::AddPinnedMenuEntries(menu, controller); // 22 需求 5.
}

void AddAskAiAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	if (!AssistantEnabled()) {
		return;
	}
	const auto prompt = Ask::BuildPrompt({ PartOf(item) }, IsChinese());
	if (prompt.isEmpty()) {
		return;
	}
	menu->addAction(Tr("问 AI", "Ask AI"), [=] {
		OpenNewConversation(controller, prompt);
	}, &st::menuIconBot);
}

} // namespace Luckgram::AI
