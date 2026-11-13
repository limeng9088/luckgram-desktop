/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/d9/luckgram_cache_logic.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace Luckgram::Cache {
namespace {

constexpr auto kGigabyte = int64(1024) * 1024 * 1024;

template <typename Values>
[[nodiscard]] int ClosestIndex(const Values &values, int64 value, int64 unit) {
	const auto unlimited = int(values.size()) - 1; // The last option is 0.
	if (value <= 0) {
		return unlimited;
	}
	auto best = unlimited;
	auto bestDistance = std::numeric_limits<int64>::max();
	for (auto i = 0; i != unlimited; ++i) {
		const auto distance = std::abs(int64(values[i]) * unit - value);
		if (distance < bestDistance) {
			best = i;
			bestDistance = distance;
		}
	}
	// Far above the largest option: treat as unlimited.
	if (value > int64(values[unlimited - 1]) * unit * 2) {
		return unlimited;
	}
	return best;
}

[[nodiscard]] int Clamp(int index, int size) {
	return std::clamp(index, 0, size - 1);
}

} // namespace

int64 LimitBytes(int index) {
	return int64(kLimitOptionsGb[Clamp(index, int(kLimitOptionsGb.size()))])
		* kGigabyte;
}

int KeepSeconds(int index) {
	return kKeepOptionsSeconds[Clamp(index, int(kKeepOptionsSeconds.size()))];
}

int LimitIndex(int64 bytes) {
	return ClosestIndex(kLimitOptionsGb, bytes, kGigabyte);
}

int KeepIndex(int64 seconds) {
	return ClosestIndex(kKeepOptionsSeconds, seconds, 1);
}

QString LimitLabel(int index, bool chinese) {
	const auto gb = kLimitOptionsGb[Clamp(index, int(kLimitOptionsGb.size()))];
	if (!gb) {
		return chinese ? QString::fromUtf8("不限") : u"No limit"_q;
	}
	return QString::number(gb) + u" GB"_q;
}

QString KeepLabel(int index, bool chinese) {
	switch (Clamp(index, int(kKeepOptionsSeconds.size()))) {
	case 0: return chinese ? QString::fromUtf8("1 小时") : u"1 hour"_q;
	case 1: return chinese ? QString::fromUtf8("1 天") : u"1 day"_q;
	case 2: return chinese ? QString::fromUtf8("3 天") : u"3 days"_q;
	case 3: return chinese ? QString::fromUtf8("1 周") : u"1 week"_q;
	case 4: return chinese ? QString::fromUtf8("1 个月") : u"1 month"_q;
	}
	return chinese ? QString::fromUtf8("永久") : u"Forever"_q;
}

} // namespace Luckgram::Cache
