/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/translate/translate_out.h"

#include "chat_helpers/compose/compose_show.h"
#include "history/history.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/translate/translate_logic.h"
#include "luckgram/translate/translate_service.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"

#include <QtCore/QPointer>
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

namespace Luckgram::Translate {
namespace {

struct State {
	TextWithTags before; // For 「撤销」.
	TextWithTags after;
	bool busy = false;
};

[[nodiscard]] QString TargetFor(not_null<History*> history) {
	return OutLanguage(
		OutChatTarget(history->peer),
		LastIncomingLanguage(history));
}

void ShowUndoToast(
		std::shared_ptr<ChatHelpers::Show> show,
		const QString &to,
		Fn<void()> undo) {
	const auto chinese = IsChinese();
	auto text = TextWithEntities{ OutHint(LanguageName(to), chinese) };
	text.append(Ui::Text::Link(UndoLabel(chinese)));
	show->showToast({
		.text = std::move(text),
		.filter = [=](const ClickHandlerPtr &, Qt::MouseButton) {
			undo();
			return true;
		},
	});
}

void TranslateField(
		not_null<Ui::InputField*> field,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<History*> history,
		const QString &to,
		std::shared_ptr<State> state) {
	const auto original = field->getTextWithAppliedMarkdown();
	if (state->busy || original.text.trimmed().isEmpty() || to.isEmpty()) {
		return;
	}
	state->busy = true;
	const auto weak = QPointer<Ui::InputField>(field.get());
	const auto alive = std::weak_ptr<ChatHelpers::Show>(show);
	TranslateText(
		&history->session(),
		TextWithEntities{
			original.text,
			TextUtilities::ConvertTextTagsToEntities(original.tags),
		},
		to,
		[=](TextWithEntities result) {
			state->busy = false;
			const auto strong = weak.data();
			if (!strong) {
				return;
			}
			// Typed on meanwhile: keep the user's text (01 C, 「原文不变」).
			if (!(strong->getTextWithAppliedMarkdown() == original)) {
				return;
			}
			const auto after = TextWithTags{
				result.text,
				TextUtilities::ConvertEntitiesToTextTags(result.entities),
			};
			strong->setTextWithTags(after);
			strong->setCursorPosition(after.text.size());
			state->before = original;
			state->after = after;
			if (const auto show = alive.lock()) {
				ShowUndoToast(show, to, [=] {
					const auto field = weak.data();
					if (field && field->getTextWithTags() == state->after) {
						field->setTextWithTags(state->before);
						field->setCursorPosition(state->before.text.size());
					}
				});
			}
		},
		[=] {
			state->busy = false;
		});
}

} // namespace

bool OutEnabled() {
	return Settings::IsEnabled(QString::fromLatin1(kFeatureOut));
}

Compose::Slot CreateSendSlot(const Compose::SlotContext &context) {
	auto button = object_ptr<Ui::IconButton>(
		context.parent,
		st::historyLuckgramTranslate);
	const auto raw = button.data();
	const auto field = context.field;
	const auto show = context.show;
	const auto history = context.history;
	const auto state = std::make_shared<State>(); // Outlives late answers.

	const auto hasText = [=] {
		return !field->getLastText().trimmed().isEmpty();
	};
	const auto translate = [=](QString to) {
		const auto current = history ? history() : nullptr;
		if (!current || !OutEnabled()) {
			return;
		}
		TranslateField(
			field,
			show,
			current,
			to.isEmpty() ? TargetFor(current) : to,
			state);
	};
	raw->setClickedCallback([=] { translate(QString()); });

	// 01 C.2: right-click on the send button.
	if (const auto send = context.send) {
		Compose::AddSendMenuItems(send, [=](not_null<Ui::PopupMenu*> menu) {
			const auto current = history ? history() : nullptr;
			if (!current || !OutEnabled() || !hasText()) {
				return;
			}
			menu->addSeparator();
			menu->addAction(Tr("翻译成…", "Translate to…"), [=] {
				const auto controller = show->resolveWindow();
				const auto chat = history ? history() : nullptr;
				if (!controller || !chat) {
					return;
				}
				const auto peer = chat->peer;
				ChooseLanguage(
					controller,
					TargetFor(chat),
					[=](QString code) {
						SetOutChatTarget(peer, code); // 01 C.3: per chat.
						translate(code);
					});
			}, &st::menuIconTranslate);
			menu->addAction(Tr("翻译", "Translate"), [=] {
				translate(QString());
			}, &st::menuIconTranslate);
		}, raw->lifetime());
	}

	auto wanted = rpl::combine(
		Settings::EnabledValue(QString::fromLatin1(kFeatureOut)),
		rpl::single(rpl::empty) | rpl::then(field->changes())
	) | rpl::map([=](bool enabled, auto&&) {
		return enabled && hasText();
	}) | rpl::distinct_until_changed();
	// specs#94: 「翻译」 in the 「+」 menu (compact bar).
	auto plus = [=](not_null<Ui::DropdownMenu*> menu) {
		if (!OutEnabled()) {
			return;
		}
		menu->addAction(Tr("翻译", "Translate"), [=] {
			if (!hasText()) {
				show->showToast(Tr(
					"先输入要翻译的文字",
					"Type the text to translate first"));
				return;
			}
			translate(QString());
		}, &st::menuIconTranslate);
	};
	return {
		.widget = std::move(button),
		.wanted = std::move(wanted),
		.plus = std::move(plus),
	};
}

} // namespace Luckgram::Translate
