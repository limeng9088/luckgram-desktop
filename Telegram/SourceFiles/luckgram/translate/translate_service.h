/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "spellcheck/spellcheck_types.h"
#include "ui/widgets/menu/menu_add_action_callback.h"

class HistoryItem;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class TranslateProvider;
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Luckgram::Translate {

// Spec 01 「翻译服务」: Telegram first, Luckgram AI when Telegram fails or
// takes longer than 8 seconds, never the Google web fallback. Used by
// every upstream translation UI (Ui::CreateTranslateProvider) while the
// "translate_in" switch is on, else nullptr.
[[nodiscard]] std::unique_ptr<Ui::TranslateProvider> MaybeCreateProvider(
	not_null<Main::Session*> session);

[[nodiscard]] bool InEnabled();

// The target language: the chat's own choice, else the default, else
// empty (specs#93: the first translation asks).
[[nodiscard]] QString DefaultTarget();
void SetDefaultTarget(const QString &language);
[[nodiscard]] QString ChatTarget(not_null<PeerData*> peer);
void SetChatTarget(not_null<PeerData*> peer, const QString &language);
[[nodiscard]] QString EffectiveTarget(not_null<PeerData*> peer);

// "不翻译的语言": the upstream "Do not translate" languages.
[[nodiscard]] QStringList DoNotTranslate();

// A.1: the translation shown in the bubble below the original, or nullptr
// (HistoryItem::translatedText()). Stale after the message is edited.
[[nodiscard]] const TextWithEntities *ShownText(
	not_null<const HistoryItem*> item);
[[nodiscard]] bool IsShown(not_null<const HistoryItem*> item);

// Translates the message into its bubble, or undoes it.
void Toggle(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

// Right-click 「翻译」 / 「撤销翻译」 instead of the upstream "Translate"
// while "translate_in" is on. False when the upstream item should stay.
bool AddMessageMenuAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

// Shows the language list and calls `chosen` with the picked code.
void ChooseLanguage(
	not_null<Window::SessionController*> controller,
	const QString &current,
	Fn<void(QString)> chosen);

[[nodiscard]] QString LanguageName(const QString &code);

// specs#105: the 「🌐 中文 ▾」 link of a translation (an internal: URL,
// core/local_url_handlers.cpp): choose a language, 「仅本聊天 / 设为默认」,
// translate the message again. False when the link is not ours.
bool HandleLanguageLink(
	not_null<Window::SessionController*> controller,
	const QString &link);

// specs#105: 「翻译设置…」 in the chat's ⋮ menu, the chat's own language.
void AddChatMenuAction(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const Ui::Menu::MenuCallback &addAction);

// Forgets cached translations and shown states (logout of all accounts).
void ClearAll();

} // namespace Luckgram::Translate
