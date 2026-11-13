/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

class HistoryItem;

// D6 (spec 14): 「全选 X 的消息」 under 「选择」 in a message's right-click
// menu, like mobile SelectAllFromSender: pages through messages.search
// (from_id = sender, every message in broadcast channels) and hands every
// page to the chat's selection, with no 100 cap (forward and delete are
// sent in batches of 100, apiwrap.cpp / data_histories.cpp).
namespace Luckgram::SelectAll {

[[nodiscard]] bool Enabled();

// The menu title for this message, empty when not offered.
[[nodiscard]] QString MenuTitleFor(not_null<HistoryItem*> item);

struct Delegate {
	// One page, newest first. False when the selection was left (cancel).
	Fn<bool(std::vector<not_null<HistoryItem*>>)> page;
	// Loading stopped: complete = every message was loaded.
	Fn<void(bool complete, int total)> finished;
};
void Start(not_null<HistoryItem*> item, Delegate delegate);

} // namespace Luckgram::SelectAll
