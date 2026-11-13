/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/ai/ai_conversation_logic.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// D7 (mobile 18 需求 8): extension points of the AI assistant. Other AI
// features (22 agents, 23 images, 30 market) only register here and never
// change the assistant's code. Registration: one line each in
// RegisterAll() (ai_registry.cpp), like Android LuckgramAiRegistry.
//
// 1. Quick buttons above the input field (23 "画图").
// 2. Input interceptors for messages starting with a prefix (23 "画",
//    "/image").
// 3. Entries at the top of the assistant page (22 "智能体").
// 4. Conversation properties: task / system / key / params of a
//    conversation, see Store::create / findOrCreate (22, 30).
// 5. Message types: a bubble for a registered type and what it tells the
//    model; unknown types show a placeholder and never crash (23 images).
namespace Luckgram::AI {

// What the open assistant page offers to registered features.
class Host {
public:
	virtual ~Host() = default;

	[[nodiscard]] virtual not_null<Window::SessionController*> sessionController() = 0;
	[[nodiscard]] virtual Conversations::Conversation conversation() = 0;

	// The input field.
	[[nodiscard]] virtual QString inputText() = 0;
	virtual void setInputText(const QString &text) = 0;

	// Adds a message to the open conversation (saved, shown) and returns
	// its id; updateMessage replaces it by id (e.g. an image arrived).
	virtual QString addMessage(Conversations::Message message) = 0;
	virtual void updateMessage(const Conversations::Message &message) = 0;

	// Sends text as the user would (after the interceptors).
	virtual void send(const QString &text) = 0;
};

struct QuickButton {
	QString id;
	const char *titleZh = "";
	const char *titleEn = "";
	// Built-in buttons put a prompt prefix into the field (18 需求 3).
	QString prefixZh;
	QString prefixEn;
	// Or a custom action (registered features).
	Fn<void(not_null<Host*>)> handler;
	Fn<bool()> visible; // nullptr = always.
	int order = 0;
};

struct InputInterceptor {
	QString id;
	// True when it takes this text (e.g. starts with "/image").
	Fn<bool(const QString&)> matches;
	Fn<void(not_null<Host*>, const QString&)> handle;
	Fn<bool()> enabled; // nullptr = always.
};

struct TopEntry {
	QString id;
	const char *titleZh = "";
	const char *titleEn = "";
	Fn<void(not_null<Window::SessionController*>)> open;
	Fn<bool()> visible; // nullptr = always.
	int order = 0;
};

struct MessageType {
	QString type;
	// The bubble content; the assistant puts it into the standard bubble.
	Fn<object_ptr<Ui::RpWidget>(
		not_null<QWidget*> parent,
		not_null<Host*> host,
		const Conversations::Message &message)> create;
	// What the model is told about it ("" = nothing).
	Fn<QString(const Conversations::Message&)> contextText;
};

void RegisterQuickButton(QuickButton button);
void RegisterInputInterceptor(InputInterceptor interceptor);
void RegisterTopEntry(TopEntry entry);
void RegisterMessageType(MessageType type);

// Registered items, sorted (visible ones only for buttons / entries).
[[nodiscard]] std::vector<QuickButton> QuickButtons();
[[nodiscard]] std::vector<TopEntry> TopEntries();
[[nodiscard]] const InputInterceptor *FindInterceptor(const QString &text);
[[nodiscard]] const MessageType *FindMessageType(const QString &type);

// Context text of a message: text type as is, registered types through
// their contextText, unknown types nothing.
[[nodiscard]] QString ContextTextOf(const Conversations::Message &message);

// Called once before the first use.
void RegisterAll();

} // namespace Luckgram::AI
