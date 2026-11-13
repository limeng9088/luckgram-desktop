/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/d9/luckgram_profile_info_logic.h"

#include <QtCore/QStringList>

#include <cmath>

namespace Luckgram::ProfileInfo {
namespace {

constexpr auto kFirstEstimatedMonth = 2014 * 12;

// Verbatim copy of mobile's luckgram_registration_dates.txt (keep in sync).
const char kTable[] = R"LGTABLE(# Luckgram spec 07: approximate Telegram registration month by user id.
# Each line: first user id seen at the start of that quarter, then the month (YYYY-MM).
# Built by isotonic fit of jobians/telegram-id-age dataset.json (MIT, (c) 2025 JOBIANSTECHIE),
# which includes the GetIDs points (MIT, (c) 2019 Jonas Zohren). Estimates only.
# 211 data points up to 2025-11-11. The 2026-01 row is extrapolated (mean slope of the last
# 12 months, one quarter at most); ids newer than the last row are not estimated.
2768409,2013-11
7800201,2014-01
41124506,2014-04
49339812,2014-07
57347970,2014-10
82554516,2015-01
101778655,2015-04
112800993,2015-07
131916146,2015-10
154912534,2016-01
178834078,2016-04
233377164,2016-07
282429582,2016-10
309146150,2017-01
369919047,2017-04
392669367,2017-07
435336549,2017-10
487519953,2018-01
538568935,2018-04
590185129,2018-07
642368533,2018-10
694551937,2019-01
745600920,2019-04
797217113,2019-07
916364933,2019-10
1047532007,2020-01
1177273352,2020-04
1307014697,2020-07
1438181771,2020-10
1569348845,2021-01
1697664461,2021-04
1827405806,2021-07
1958572880,2021-10
5039253424,2022-01
5271136925,2022-04
5437261716,2022-07
5594400567,2022-10
5794596902,2023-01
6047686519,2023-04
6309370298,2023-07
6437935417,2023-10
6551763330,2024-01
6994177277,2024-04
7233605530,2024-07
7450611511,2024-10
7681540974,2025-01
7843687265,2025-04
8123054466,2025-07
8379488111,2025-10
8700161147,2026-01
)LGTABLE";

} // namespace

QString DisplayId(PeerKind kind, qint64 id) {
	switch (kind) {
	case PeerKind::Channel: return QStringLiteral("-100") + QString::number(id);
	case PeerKind::LegacyGroup: return QStringLiteral("-") + QString::number(id);
	case PeerKind::User: break;
	}
	return QString::number(id);
}

QString UserLink(PeerKind kind, qint64 id) {
	return (kind == PeerKind::User)
		? (QStringLiteral("tg://user?id=") + QString::number(id))
		: QString();
}

std::vector<Point> ParseTable(const QString &text) {
	auto points = std::vector<Point>();
	const auto lines = text.split(QChar('\n'));
	for (const auto &raw : lines) {
		const auto line = raw.trimmed();
		if (line.isEmpty() || line.startsWith(QChar('#'))) {
			continue;
		}
		const auto comma = line.indexOf(QChar(','));
		if (comma <= 0) {
			continue;
		}
		auto ok = false;
		const auto id = line.left(comma).trimmed().toLongLong(&ok);
		if (!ok) {
			continue;
		}
		const auto date = line.mid(comma + 1).trimmed();
		if (date.size() < 7 || date[4] != QChar('-')) {
			continue;
		}
		auto yearOk = false;
		auto monthOk = false;
		const auto year = date.left(4).toInt(&yearOk);
		const auto month = date.mid(5, 2).toInt(&monthOk);
		if (!yearOk || !monthOk || month < 1 || month > 12) {
			continue;
		}
		const auto point = Point{ qint64(id), year * 12 + month - 1 };
		if (!points.empty()) {
			const auto &last = points.back();
			if (point.id <= last.id || point.monthIndex < last.monthIndex) {
				continue;
			}
		}
		points.push_back(point);
	}
	return points;
}

std::optional<Estimate> EstimateRegistration(
		const std::vector<Point> &points,
		qint64 userId) {
	if (points.size() < 2 || userId <= 0) {
		return std::nullopt;
	} else if (userId < points.front().id) {
		return Estimate{ .before2014 = true };
	}
	for (auto i = 0; i + 1 < int(points.size()); ++i) {
		const auto &a = points[i];
		const auto &b = points[i + 1];
		if (userId >= a.id && userId < b.id) {
			const auto t = double(userId - a.id) / double(b.id - a.id);
			const auto m = a.monthIndex
				+ int(std::floor(t * (b.monthIndex - a.monthIndex)));
			if (m < kFirstEstimatedMonth) {
				return Estimate{ .before2014 = true };
			}
			return Estimate{ .year = m / 12, .month = m % 12 + 1 };
		}
	}
	return std::nullopt;
}

QString FormatEstimate(
		const std::optional<Estimate> &estimate,
		bool chinese) {
	static const char *kMonthsEn[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
	};
	if (!estimate) {
		return QString();
	} else if (estimate->before2014) {
		return chinese
			? QString::fromUtf8("早于 2014 年")
			: QStringLiteral("before 2014");
	} else if (estimate->month < 1 || estimate->month > 12) {
		return QString();
	}
	return chinese
		? QString::fromUtf8("约 %1 年 %2 月"
			).arg(estimate->year).arg(estimate->month)
		: QStringLiteral("about %1 %2")
			.arg(QString::fromLatin1(kMonthsEn[estimate->month - 1]))
			.arg(estimate->year);
}

QString BundledTableText() {
	return QString::fromUtf8(kTable);
}

const std::vector<Point> &BundledTable() {
	static const auto result = ParseTable(BundledTableText());
	return result;
}

} // namespace Luckgram::ProfileInfo
