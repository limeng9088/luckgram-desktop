/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "settings/settings_common_session.h"

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

namespace Settings {

// D2 §1-2: Settings → 「Luckgram 功能」, generated from the feature registry
// (luckgram/luckgram_features.h), same groups and order as mobile.
class LuckgramFeatures : public Section<LuckgramFeatures> {
public:
	LuckgramFeatures(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

// The 「Luckgram 功能」 button of the main settings page (one line each in
// settings_main.cpp): above Telegram Premium, or above Notifications when
// Premium isn't available.
void AddLuckgramFeaturesButton(Builder::SectionBuilder &builder);

} // namespace Settings
