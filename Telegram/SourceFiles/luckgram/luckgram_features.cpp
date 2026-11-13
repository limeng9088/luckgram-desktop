/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_features.h"

#include "luckgram/luckgram_strings.h"

namespace Luckgram::Features {
namespace {

[[nodiscard]] std::vector<Feature> Build() {
	auto result = std::vector<Feature>();

	// ---- 账号与隐私 / Accounts & Privacy
	// ---- 消息与语言 / Messages & Language
	result.push_back({
		.key = u"save_to_saved"_q, // spec 15
		.group = Group::MessagesLanguage,
		.titleZh = "一键收藏",
		.titleEn = "Save in one tap",
		.aboutZh = "消息菜单里一点就存到收藏夹",
		.aboutEn = "Save a message to Saved Messages straight from its menu",
		.defaultValue = true,
	});
	// ---- 媒体、通话与存储 / Media, Calls & Storage
	// ---- 资料与整理 / Profiles & Organization
	// ---- AI (switches come with D5-D12; the consent row is always there)

	// ---- 调试 / Debug (dev builds only)
	if (IsDevBuild()) {
		result.push_back({
			.key = QString::fromLatin1(kDebugDemo),
			.group = Group::Debug,
			.titleZh = "调试示例开关",
			.titleEn = "Debug demo switch",
			.aboutZh = "只用来验证开关框架，打开后重启仍保持",
			.aboutEn = "Only checks the switch framework; stays on after a restart",
			.defaultValue = false,
		});
	}
	return result;
}

} // namespace

const std::vector<Feature> &All() {
	static const auto result = Build();
	return result;
}

const Feature *Find(const QString &key) {
	for (const auto &feature : All()) {
		if (feature.key == key) {
			return &feature;
		}
	}
	return nullptr;
}

std::vector<Group> Groups() {
	auto result = std::vector<Group>{
		Group::AccountPrivacy,
		Group::MessagesLanguage,
		Group::MediaCallsStorage,
		Group::ProfileOrganize,
		Group::AI,
	};
	if (IsDevBuild()) {
		result.push_back(Group::Debug);
	}
	return result;
}

QString GroupTitle(Group group) {
	switch (group) {
	case Group::AccountPrivacy: return Tr("账号与隐私", "Accounts & Privacy");
	case Group::MessagesLanguage: return Tr("消息与语言", "Messages & Language");
	case Group::MediaCallsStorage:
		return Tr("媒体、通话与存储", "Media, Calls & Storage");
	case Group::ProfileOrganize:
		return Tr("资料与整理", "Profiles & Organization");
	case Group::AI: return u"AI"_q;
	case Group::Debug: return Tr("调试", "Debug");
	}
	Unexpected("Luckgram feature group.");
}

bool IsDevBuild() {
#if defined _DEBUG || defined LUCKGRAM_DEV_BUILD
	return true;
#else // _DEBUG || LUCKGRAM_DEV_BUILD
	return false;
#endif // _DEBUG || LUCKGRAM_DEV_BUILD
}

} // namespace Luckgram::Features
