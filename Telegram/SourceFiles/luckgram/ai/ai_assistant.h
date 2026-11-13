/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

class HistoryItem;

namespace Ui {
class PopupMenu;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// D7 (mobile 18): the AI assistant. A local chat page (Settings section in
// the main column, like the Luckgram settings pages): bubbles with Markdown,
// streaming with Stop, copy, regenerate, quick buttons, history. It is not a
// Telegram chat and never creates Telegram messages.
//
// Upstream files call into here with one line each:
// - window_main_menu.cpp: the "Luckgram AI" entry of the left menu;
// - history_inner_widget.cpp / history_view_context_menu.cpp: "问 AI".
namespace Luckgram::AI {

// Switch ai_assistant (local switch and the server's).
[[nodiscard]] bool AssistantEnabled();
[[nodiscard]] rpl::producer<bool> AssistantEnabledValue();

// Opens a conversation (empty id: the most recent one, or a new one when
// there is none), optionally with text put into the input field.
void OpenAssistant(
	not_null<Window::SessionController*> controller,
	const QString &conversationId = QString(),
	const QString &inputText = QString());

// A new conversation (saved once the first message is sent), optionally
// with text in the input field ("问 AI" opens one like Android).
void OpenNewConversation(
	not_null<Window::SessionController*> controller,
	const QString &inputText = QString());

// The history list (18 需求 6).
void OpenHistory(not_null<Window::SessionController*> controller);

// D7 需求 1 (desktop): the "Luckgram AI" entry of the left menu, shown
// while ai_assistant is on (mobile: the "AI" button on the chat list).
void AddMainMenuEntry(
	not_null<Ui::VerticalLayout*> menu,
	not_null<Window::SessionController*> controller);

// 18 需求 5: "问 AI" in a message's context menu. Adds nothing when the
// assistant is off or the message has nothing to ask about. Callers skip
// it for messages that can't be copied (protected content).
void AddAskAiAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

} // namespace Luckgram::AI
