/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QString>

#include <cstdint>

// D6 (spec 14): select all messages from one sender, like mobile
// SelectAllFromSender. Pure logic, QtCore only.
namespace Luckgram::SelectAll {

inline constexpr auto kFeature = "select_all_from_sender";
inline constexpr auto kPageSize = 100; // messages.search page.

// Menu title: 「全选 Noah 的消息」; in private chats 「全选对方的消息」 /
// 「全选我的消息」. `name` is the sender's first name or chat title,
// 「我」 / "Me" for yourself in groups.
enum class Sender {
	PrivateMine,
	PrivateTheirs,
	Named,
};
[[nodiscard]] QString MenuTitle(
	Sender sender,
	const QString &name,
	bool chinese);
[[nodiscard]] QString MeName(bool chinese);

// Another page is needed: a full page came and older messages remain.
[[nodiscard]] bool LoadMore(int pageMessages, int64_t minId);

[[nodiscard]] QString LoadingText(bool chinese);
[[nodiscard]] QString DoneText(int count, bool chinese);
[[nodiscard]] QString FailedText(int count, bool chinese);
[[nodiscard]] QString CopyText(bool chinese);

} // namespace Luckgram::SelectAll
