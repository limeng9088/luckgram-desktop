/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_writing_registry.h"

#include "luckgram/ai/ai_post.h"
#include "luckgram/ai/ai_writing.h"

namespace Luckgram::AI {
namespace {

[[nodiscard]] std::vector<WritingItem> &Items() {
	static auto result = std::vector<WritingItem>();
	return result;
}

[[nodiscard]] bool Visible(
		const WritingItem &item,
		not_null<WritingHost*> host,
		bool hasText) {
	if (!hasText && !item.allowEmptyText) {
		return false;
	}
	return !item.visible || item.visible(host);
}

} // namespace

void RegisterWritingItem(WritingItem item) {
	UnregisterWritingItem(item.id);
	Items().push_back(std::move(item));
}

void UnregisterWritingItem(const QString &id) {
	auto &items = Items();
	items.erase(ranges::remove(items, id, &WritingItem::id), end(items));
}

std::vector<WritingItem> VisibleWritingItems(
		not_null<WritingHost*> host,
		bool hasText) {
	RegisterWritingFeatures();
	auto result = std::vector<WritingItem>();
	for (const auto &item : Items()) {
		if (Visible(item, host, hasText)) {
			result.push_back(item);
		}
	}
	ranges::stable_sort(result, ranges::less(), &WritingItem::order);
	return result;
}

bool HasWritingItemsForEmptyText(not_null<WritingHost*> host) {
	return !VisibleWritingItems(host, false).empty();
}

void RegisterWritingFeatures() {
	static auto done = false;
	if (std::exchange(done, true)) {
		return;
	}
	RegisterWritingEffects(); // 21: orders 100..140.
	// Features add one line each below:
	// 20 AI reply (order 0, Reply::kMenuOrder)
	// 24 AI post (order 10, Post::kMenuOrder)
	Post::Register();
}

} // namespace Luckgram::AI
