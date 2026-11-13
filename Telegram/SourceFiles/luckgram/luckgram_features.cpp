/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_features.h"

#include "luckgram/discover/luckgram_nearby_logic.h"
#include "luckgram/discover/luckgram_square_logic.h"
#include "luckgram/media/luckgram_round_video_logic.h"
#include "luckgram/luckgram_double_bottom.h"
#include "luckgram/luckgram_hidden_chats.h"
#include "luckgram/luckgram_store_restrictions.h"
#include "luckgram/luckgram_strings.h"
#include "settings/sections/settings_luckgram_discover.h"
#include "settings/sections/settings_luckgram_privacy.h"
#include "settings/sections/settings_privacy_security.h"
#include "window/window_session_controller.h"

namespace Luckgram::Features {
namespace {

[[nodiscard]] std::vector<Feature> Build() {
	auto result = std::vector<Feature>();

	// ---- 账号与隐私 / Accounts & Privacy
	// D8: 13 受限频道 (mobile StoreRestrictionsFeature).
	result.push_back({
		.key = QString::fromLatin1(kStoreRestrictionsKey),
		.group = Group::AccountPrivacy,
		.titleZh = "受限频道",
		.titleEn = "Store-restricted channels",
		.aboutZh = "查看只在商店版 Telegram 中被隐藏的频道和群组；点进去可调整「显示 18+ 内容」",
		.aboutEn = "View channels and groups hidden only in store versions of Telegram; click for the 18+ content setting",
		// On by default on desktop (w-1109, 2026-11-13): no app store
		// review here, so never stricter than upstream Telegram Desktop.
		.defaultValue = true,
		.subpage = [] { return ::Settings::PrivacySecurityId(); },
		.confirmEnable = [](
				not_null<Window::SessionController*> controller,
				Fn<void(bool)> done) {
			ConfirmStoreRestrictionsOff(controller->uiShow(), std::move(done));
		},
	});
	// D8: 11 双重空间 (mobile LuckgramDoubleBottom).
	result.push_back({
		.key = QString::fromLatin1(DoubleBottom::kFeatureKey),
		.group = Group::AccountPrivacy,
		.titleZh = "双重空间",
		.titleEn = "Double space",
		.aboutZh = "用第二个密码解锁时只显示选定的账号",
		.aboutEn = "A second passcode that shows only the accounts you pick",
		.defaultValue = false,
		.subpage = [] { return ::Settings::LuckgramDoubleBottomId(); },
		.confirmEnable = ::Settings::ConfirmDoubleBottomEnable,
		.confirmDisable = ::Settings::ConfirmDoubleBottomDisable,
		.hiddenWhen = [] { return DoubleBottom::ActiveValue(); },
	});
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
	// D8: 12 隐藏聊天 (mobile LuckgramHiddenChats).
	result.push_back({
		.key = QString::fromLatin1(HiddenChats::kFeatureKey),
		.group = Group::ProfileOrganize,
		.titleZh = "隐藏聊天",
		.titleEn = "Hidden chats",
		.aboutZh = "把聊天从列表里藏起来，在左上角菜单里找回",
		.aboutEn = "Hide chats from the list; find them in the top-left menu",
		.defaultValue = true,
		.subpage = [] { return ::Settings::LuckgramHiddenChatsId(); },
		.onToggled = HiddenChats::OnToggled,
	});
	// D11: 03 (mobile ROUND_VIDEO_FROM_GALLERY, 媒体、通话与存储).
	result.push_back({
		.key = QString::fromLatin1(RoundVideo::kFeatureKey),
		.group = Group::MediaCallsStorage,
		.titleZh = "视频文件发成圆形视频",
		.titleEn = "Video file as round video",
		.aboutZh = "发送文件时选一个视频，在「…」菜单里可作为圆形视频发送",
		.aboutEn = "Pick one video to send, then send it as a round video from the \"…\" menu",
		.defaultValue = true,
	});
	// ---- 发现 / Discover (D10)
	result.push_back({
		.key = QString::fromLatin1(Nearby::kFeatureKey),
		.group = Group::Discover,
		.titleZh = "附近的人",
		.titleEn = "People Nearby",
		.aboutZh = "「发现」里的入口：找到附近同样开启的 Luckgram 用户",
		.aboutEn = "Entry in Discover: find Luckgram users nearby who turned it on",
		.defaultValue = true,
	});
	result.push_back({
		.key = QString::fromLatin1(Square::kFeatureKey),
		.group = Group::Discover,
		.titleZh = "广场",
		.titleEn = "Square",
		.aboutZh = "「发现」里的入口：所有 Luckgram 用户的公开动态，发图文短帖、点赞、评论",
		.aboutEn = "Entry in Discover: a public feed for all Luckgram users with short posts, likes and comments",
		.defaultValue = true,
		.subpage = [] { return ::Settings::LuckgramSquare::Id(); },
	});
	// ---- AI. D7 (mobile 18-24, 30): same keys, order, texts and defaults
	// as Android LuckgramFeatures; the consent row is always there. D6's
	// voice switches (free_transcribe, dictation...) go in this group too.
	result.push_back({
		.key = QString::fromLatin1(kAiAssistant),
		.group = Group::AI,
		.titleZh = "AI 助手",
		.titleEn = "AI assistant",
		.aboutZh = "聊天列表顶部的「AI」按钮：提问、总结、改写、讲解、解题",
		.aboutEn = "The \"AI\" button on the chat list: ask, summarize, rewrite, explain, solve",
		.defaultValue = true,
	});
	result.push_back({
		.key = QString::fromLatin1(kAiImage),
		.group = Group::AI,
		.titleZh = "AI 画图",
		.titleEn = "AI images",
		.aboutZh = "在 AI 助手里描述画面生成图片，可发送、保存、重画",
		.aboutEn = "Describe a picture in the AI assistant; send, save or redraw it",
		.defaultValue = true,
	});
	result.push_back({
		.key = QString::fromLatin1(kAiAgents),
		.group = Group::AI,
		.titleZh = "AI 智能体",
		.titleEn = "AI agents",
		.aboutZh = "AI 助手里的旅行规划师、文案写手等，也能自己创建",
		.aboutEn = "Travel planner, copywriter and more in the AI assistant, or make your own",
		.defaultValue = true,
	});
	result.push_back({
		.key = QString::fromLatin1(kAiAgentMarket),
		.group = Group::AI,
		.titleZh = "智能体市场",
		.titleEn = "Agent market",
		.aboutZh = "「智能体」里的「市场」：按分类浏览、搜索更多智能体，部分 Pro 专享",
		.aboutEn = "\"Market\" in Agents: browse and search more agents by category, some for Pro",
		.defaultValue = true,
	});
	result.push_back({
		.key = QString::fromLatin1(kAiAssistantRow),
		.group = Group::AI,
		.titleZh = "聊天列表置顶「Luckgram AI」",
		.titleEn = "Pin \"Luckgram AI\" in the chat list",
		.aboutZh = "在聊天列表顶部显示一行 AI 助手入口",
		.aboutEn = "Show an AI assistant row at the top of the chat list",
		.defaultValue = true,
	});
	result.push_back({
		.key = QString::fromLatin1(kAiWriting),
		.group = Group::AI,
		.titleZh = "AI 写作",
		.titleEn = "AI writing",
		.aboutZh = "输入框里的 ✦ 按钮：润色、更正式、更友好、更简短、修正语法",
		.aboutEn = "The ✦ button in the message field: polish, formal, friendly, shorter, grammar",
		.defaultValue = true,
	});
	result.push_back({
		.key = QString::fromLatin1(kAiPost),
		.group = Group::AI,
		.titleZh = "AI 写帖子",
		.titleEn = "AI posts",
		.aboutZh = "有发帖权限的频道里，✦ 菜单的「写帖子」：按频道风格生成帖子",
		.aboutEn = "\"Write a post\" in the ✦ menu of channels you can post in, in the channel's style",
		.defaultValue = true,
	});
	result.push_back({
		.key = QString::fromLatin1(kAiReply),
		.group = Group::AI,
		.titleZh = "AI 回复",
		.titleEn = "AI reply",
		.aboutZh = "✦ 菜单里的「帮我回复」：根据对话和草稿给出 3 条候选",
		.aboutEn = "\"Help me reply\" in the ✦ menu: 3 suggestions from the chat and your draft",
		.defaultValue = true,
	});
	result.push_back({
		.key = QString::fromLatin1(kAiSummary),
		.group = Group::AI,
		.titleZh = "聊天总结",
		.titleEn = "Chat summary",
		.aboutZh = "未读多时一键总结要点，点引用跳回原消息，还能就这些消息提问",
		.aboutEn = "Summarize many unread messages, jump to the source, ask about them",
		.defaultValue = true,
	});

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
		Group::Discover,
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
	case Group::Discover: return Tr("发现", "Discover");
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
