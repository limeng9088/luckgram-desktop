/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "settings/sections/settings_luckgram_translate.h"

#include "boxes/translate_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/translate/translate_service.h"
#include "settings/settings_common.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

[[nodiscard]] rpl::producer<QString> TargetValue() {
	return rpl::single(
		QString()
	) | rpl::then(
		Luckgram::Settings::Changes()
	) | rpl::map([] {
		const auto to = Luckgram::Translate::DefaultTarget();
		return to.isEmpty()
			? Luckgram::Tr("未选择（第一次翻译时选）", "Not set (asked the first time)")
			: Luckgram::Translate::LanguageName(to);
	});
}

[[nodiscard]] rpl::producer<QString> DoNotTranslateValue() {
	return Core::App().settings().skipTranslationLanguagesValue(
	) | rpl::map([](const std::vector<LanguageId> &list) {
		auto names = QStringList();
		for (const auto &id : list) {
			names.push_back(Luckgram::Translate::LanguageName(
				id.twoLetterCode()));
		}
		return names.join(Luckgram::Tr("、", ", "));
	});
}

} // namespace

LuckgramTranslateIn::LuckgramTranslateIn(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> LuckgramTranslateIn::title() {
	return Luckgram::TrValue("收到消息翻译", "Translate received messages");
}

void LuckgramTranslateIn::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);

	Ui::AddSkip(container);
	AddButtonWithLabel(
		container,
		Luckgram::TrValue("翻译成", "Translate to"),
		TargetValue(),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		Luckgram::Translate::ChooseLanguage(
			controller,
			Luckgram::Translate::DefaultTarget(),
			[](QString code) {
				Luckgram::Translate::SetDefaultTarget(code);
			});
	});
	AddButtonWithLabel(
		container,
		Luckgram::TrValue("不翻译的语言", "Do not translate"),
		DoNotTranslateValue(),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		controller->show(Ui::EditSkipTranslationLanguages());
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		Luckgram::TrValue(
			"右键消息选「翻译」，或点消息旁的翻译图标，译文显示在原文下方。"
			"「不翻译的语言」的消息不显示翻译图标，右键仍可翻译。",
			"Right-click a message and choose Translate, or click the "
			"translate icon next to it; the translation appears below the "
			"original. Messages in a \"do not translate\" language get no "
			"icon but can still be translated from the menu."));

	Ui::ResizeFitChild(this, container);
}

} // namespace Settings
