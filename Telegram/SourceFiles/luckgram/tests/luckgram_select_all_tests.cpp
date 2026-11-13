/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/selectall/select_all_logic.h"

// D6: select all from sender (mobile 14), texts as on mobile and iOS.

namespace {

using namespace Luckgram::SelectAll;

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

} // namespace

LUCKGRAM_TEST(SelectAllMenuTitles) {
	LG_EQUAL(MenuTitle(Sender::PrivateMine, QString(), true), U("全选我的消息"));
	LG_EQUAL(
		MenuTitle(Sender::PrivateTheirs, QString(), true),
		U("全选对方的消息"));
	LG_EQUAL(MenuTitle(Sender::Named, U("Noah"), true), U("全选 Noah 的消息"));
	LG_EQUAL(
		MenuTitle(Sender::Named, U("Noah"), false),
		U("Select All from Noah"));
	LG_EQUAL(
		MenuTitle(Sender::Named, MeName(true), true),
		U("全选 我 的消息"));
	LG_EQUAL(
		MenuTitle(Sender::PrivateTheirs, QString(), false),
		U("Select All Their Messages"));
}

LUCKGRAM_TEST(SelectAllPaging) {
	LG_CHECK(LoadMore(100, 5000));
	LG_CHECK(!LoadMore(99, 5000)); // Last page.
	LG_CHECK(!LoadMore(100, 1)); // Reached the first message.
	LG_CHECK(!LoadMore(0, 0));
}

LUCKGRAM_TEST(SelectAllTexts) {
	LG_EQUAL(DoneText(300, true), U("已选中全部 300 条"));
	LG_EQUAL(DoneText(300, false), U("All 300 messages selected"));
	LG_EQUAL(FailedText(120, true), U("加载中断，已选中 120 条"));
	LG_EQUAL(CopyText(true), U("复制文字"));
}
