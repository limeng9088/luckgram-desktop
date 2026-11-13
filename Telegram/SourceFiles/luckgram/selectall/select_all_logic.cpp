/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/selectall/select_all_logic.h"

namespace Luckgram::SelectAll {
namespace {

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

} // namespace

QString MenuTitle(Sender sender, const QString &name, bool chinese) {
	switch (sender) {
	case Sender::PrivateMine:
		return chinese ? U("全选我的消息") : U("Select All My Messages");
	case Sender::PrivateTheirs:
		return chinese ? U("全选对方的消息") : U("Select All Their Messages");
	case Sender::Named:
		break;
	}
	return chinese
		? (U("全选 ") + name + U(" 的消息"))
		: (U("Select All from ") + name);
}

QString MeName(bool chinese) {
	return chinese ? U("我") : U("Me");
}

bool LoadMore(int pageMessages, int64_t minId) {
	return (pageMessages >= kPageSize) && (minId > 1);
}

QString LoadingText(bool chinese) {
	return chinese
		? U("正在加载全部消息，退出多选可取消")
		: U("Loading all messages… Leave selection to cancel");
}

QString DoneText(int count, bool chinese) {
	return chinese
		? (U("已选中全部 ") + QString::number(count) + U(" 条"))
		: (U("All ") + QString::number(count) + U(" messages selected"));
}

QString FailedText(int count, bool chinese) {
	return chinese
		? (U("加载中断，已选中 ") + QString::number(count) + U(" 条"))
		: (U("Loading stopped, ") + QString::number(count) + U(" selected"));
}

QString CopyText(bool chinese) {
	return chinese ? U("复制文字") : U("Copy Text");
}

} // namespace Luckgram::SelectAll
