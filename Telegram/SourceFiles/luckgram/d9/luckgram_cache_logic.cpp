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

constexpr auto kGigabyte = qint64(1024) * 1024 * 1024;

template <typename Values>
[[nodiscard]] int ClosestIndex(const Values &values, qint64 value, qint64 unit) {
	const auto unlimited = int(values.size()) - 1; // The last option is 0.
	if (value <= 0) {
		return unlimited;
	}
	auto best = unlimited;
	auto bestDistance = std::numeric_limits<qint64>::max();
	for (auto i = 0; i != unlimited; ++i) {
		const auto distance = std::abs(qint64(values[i]) * unit - value);
		if (distance < bestDistance) {
			best = i;
			bestDistance = distance;
		}
	}
	// Far above the largest option: treat as unlimited.
	if (value > qint64(values[unlimited - 1]) * unit * 2) {
		return unlimited;
	}
	return best;
}

[[nodiscard]] int Clamp(int index, int size) {
	return std::clamp(index, 0, size - 1);
}

} // namespace

qint64 LimitBytes(int index) {
	return qint64(kLimitOptionsGb[Clamp(index, int(kLimitOptionsGb.size()))])
		* kGigabyte;
}

int KeepSeconds(int index) {
	return kKeepOptionsSeconds[Clamp(index, int(kKeepOptionsSeconds.size()))];
}

int LimitIndex(qint64 bytes) {
	return ClosestIndex(kLimitOptionsGb, bytes, kGigabyte);
}

int KeepIndex(qint64 seconds) {
	return ClosestIndex(kKeepOptionsSeconds, seconds, 1);
}

QString LimitLabel(int index, bool chinese) {
	const auto gb = kLimitOptionsGb[Clamp(index, int(kLimitOptionsGb.size()))];
	if (!gb) {
		return chinese ? QString::fromUtf8("不限") : QStringLiteral("No limit");
	}
	return QString::number(gb) + QStringLiteral(" GB");
}

QString KeepLabel(int index, bool chinese) {
	switch (Clamp(index, int(kKeepOptionsSeconds.size()))) {
	case 0: return chinese ? QString::fromUtf8("1 小时") : QStringLiteral("1 hour");
	case 1: return chinese ? QString::fromUtf8("1 天") : QStringLiteral("1 day");
	case 2: return chinese ? QString::fromUtf8("3 天") : QStringLiteral("3 days");
	case 3: return chinese ? QString::fromUtf8("1 周") : QStringLiteral("1 week");
	case 4: return chinese ? QString::fromUtf8("1 个月") : QStringLiteral("1 month");
	}
	return chinese ? QString::fromUtf8("永久") : QStringLiteral("Forever");
}

} // namespace Luckgram::Cache
