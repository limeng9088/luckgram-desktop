/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "settings/settings_type.h"

// D2 (mobile 00): the one registry of Luckgram switches. The settings page
// ("Luckgram 功能") is generated from it: adding an entry here is all it
// takes for a new switch to appear. Keys are the same as mobile
// (features.<key> in GET /v1/config); features the desktop app doesn't have
// yet are not listed.
namespace Luckgram::Features {

// Same groups and order as mobile (LuckgramFeature.Group).
enum class Group {
	AccountPrivacy,
	MessagesLanguage,
	MediaCallsStorage,
	ProfileOrganize,
	Discover, // D10: People Nearby, Square (mobile LuckgramFeature.Group.DISCOVER).
	AI,
	Debug, // Only in dev builds.
};

struct Feature {
	QString key;
	Group group = Group::Debug;
	const char *titleZh = "";
	const char *titleEn = "";
	const char *aboutZh = "";
	const char *aboutEn = "";
	bool defaultValue = false;
	// Own settings page ("有二级设置的点进去是自己的设置页"), or nullptr.
	Fn<::Settings::Type()> subpage;
};

[[nodiscard]] const std::vector<Feature> &All();
[[nodiscard]] const Feature *Find(const QString &key);
[[nodiscard]] std::vector<Group> Groups(); // In display order.
[[nodiscard]] QString GroupTitle(Group group);

// Debug build (or -D LUCKGRAM_DEV=ON): api-dev, debug switches and page.
[[nodiscard]] bool IsDevBuild();

inline constexpr auto kDebugDemo = "debug_demo";

} // namespace Luckgram::Features
