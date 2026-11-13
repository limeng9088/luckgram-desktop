/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/d9/luckgram_shared_activity_logic.h"

namespace Luckgram::SharedActivity {

std::vector<Dated> NewestFirst(
		const std::vector<std::vector<Dated>> &perGroup,
		int limit) {
	auto all = std::vector<Dated>();
	for (const auto &list : perGroup) {
		all.insert(end(all), begin(list), end(list));
	}
	std::stable_sort(begin(all), end(all), [](
			const Dated &a,
			const Dated &b) {
		return (a.date != b.date) ? (a.date > b.date) : (a.sortId > b.sortId);
	});
	if (limit >= 0 && int(all.size()) > limit) {
		all.resize(limit);
	}
	return all;
}

int FloodWaitSeconds(const QString &errorType) {
	for (const auto &prefix : { QStringLiteral("FLOOD_WAIT_"), QStringLiteral("FLOOD_PREMIUM_WAIT_") }) {
		if (errorType.startsWith(prefix)) {
			auto ok = false;
			const auto seconds = errorType.mid(prefix.size()).toInt(&ok);
			return ok ? std::max(seconds, 0) : -1;
		}
	}
	return -1;
}

bool CanWaitFlood(int waitSeconds, qint64 elapsedMs) {
	return (waitSeconds >= 0)
		&& (waitSeconds <= kMaxFloodWaitSeconds)
		&& (elapsedMs + waitSeconds * qint64(1000) + 500 < kTimeBudgetMs);
}

bool IsFresh(qint64 loadedAtMs, qint64 nowMs) {
	return (loadedAtMs > 0)
		&& (nowMs >= loadedAtMs)
		&& (nowMs - loadedAtMs < kCacheMs);
}

QString Header(int groupCount, bool partial, bool chinese) {
	auto result = chinese
		? QString::fromUtf8("最近消息 · %1 个共同群组").arg(groupCount)
		: QStringLiteral("Recent messages · %1 %2 in common")
			.arg(groupCount)
			.arg((groupCount == 1) ? QStringLiteral("group") : QStringLiteral("groups"));
	if (partial) {
		result += chinese
			? QString::fromUtf8("（部分群组未加载完，点击「刷新」重试）")
			: QStringLiteral(" (some groups didn't load, click Refresh to retry)");
	}
	return result;
}

} // namespace Luckgram::SharedActivity
