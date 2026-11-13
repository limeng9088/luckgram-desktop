/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
// D9 unit tests: the pure rules of 05 / 06 / 07 / 08 / 16. Cases are ported
// from mobile (Android ProfileInfoLogicTest, UserNotesTextTest,
// KeywordMatcherTest, SharedActivityLogicTest). Run: luckgram_tests D9.
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/d9/luckgram_cache_logic.h"
#include "luckgram/d9/luckgram_keyword_logic.h"
#include "luckgram/d9/luckgram_notes_text.h"
#include "luckgram/d9/luckgram_profile_info_logic.h"
#include "luckgram/d9/luckgram_shared_activity_logic.h"

#include <cstdlib>
#include <optional>
#include <vector>

namespace {

// Each check names itself, so a failure says which case broke.
void Expect(bool condition, const QString &what) {
	if (!condition) {
		::Luckgram::Test::Fail(__FILE__, __LINE__, what);
	}
}

void Expect(bool condition, const char *what) {
	Expect(condition, QString::fromUtf8(what));
}

void ExpectEq(const QString &actual, const QString &expected, const char *what) {
	Expect(
		actual == expected,
		QStringLiteral("%1: got \"%2\", expected \"%3\"")
			.arg(QString::fromUtf8(what), actual, expected));
}

void ExpectEq(qint64 actual, qint64 expected, const char *what) {
	Expect(
		actual == expected,
		QStringLiteral("%1: got %2, expected %3")
			.arg(QString::fromUtf8(what))
			.arg(actual)
			.arg(expected));
}

[[nodiscard]] QString U(const char *utf8) {
	return QString::fromUtf8(utf8);
}

[[nodiscard]] QString Repeat(const QString &s, int n) {
	auto result = QString();
	for (auto i = 0; i != n; ++i) {
		result += s;
	}
	return result;
}

} // namespace

// ---- 07 profile details

// 07.ids
LUCKGRAM_TEST(D9ProfileIds) {
	using namespace Luckgram::ProfileInfo;
	ExpectEq(DisplayId(PeerKind::User, 777000), QStringLiteral("777000"), "user id");
	ExpectEq(
		DisplayId(PeerKind::Channel, 1234567890),
		QStringLiteral("-1001234567890"),
		"channel id");
	ExpectEq(DisplayId(PeerKind::LegacyGroup, 123456), QStringLiteral("-123456"), "group id");
	ExpectEq(UserLink(PeerKind::User, 42), QStringLiteral("tg://user?id=42"), "user link");
	Expect(UserLink(PeerKind::Channel, 42).isEmpty(), "channel has no link");
	Expect(UserLink(PeerKind::LegacyGroup, 42).isEmpty(), "group has no link");
}

// 07.table-coverage
LUCKGRAM_TEST(D9ProfileTableCoverage) {
	using namespace Luckgram::ProfileInfo;
	const auto &t = BundledTable();
	Expect(t.size() >= 45, QStringLiteral("rows: %1").arg(t.size()));
	if (t.size() < 2) {
		return;
	}
	ExpectEq(t.front().monthIndex / 12, 2013, "first year");
	Expect(t.back().monthIndex / 12 >= 2026, "last year >= 2026");
	for (auto i = 1; i < int(t.size()); ++i) {
		Expect(t[i].id > t[i - 1].id, QStringLiteral("ids grow at %1").arg(i));
		Expect(
			t[i].monthIndex - t[i - 1].monthIndex <= 3,
			QStringLiteral("gap > 1 quarter at row %1").arg(i));
	}
}

// 07.known-accounts-within-6-months
LUCKGRAM_TEST(D9ProfileKnownAccounts) {
	using namespace Luckgram::ProfileInfo;
	const qint64 known[][3] = {
		{ 101323197LL, 2015, 3 },
		{ 369669043LL, 2017, 3 },
		{ 805158066LL, 2019, 7 },
		{ 1974255900LL, 2021, 10 },
		{ 5144324763LL, 2022, 4 },
		{ 6872061796LL, 2024, 5 },
		{ 8325327280LL, 2025, 10 },
		{ 8559682245LL, 2025, 11 },
	};
	for (const auto &k : known) {
		const auto e = EstimateRegistration(BundledTable(), k[0]);
		Expect(e.has_value(), QStringLiteral("id %1 estimated").arg(k[0]));
		if (!e) {
			continue;
		}
		Expect(!e->before2014, QStringLiteral("id %1 not before 2014").arg(k[0]));
		const auto off = std::abs(
			(e->year * 12 + e->month) - int(k[1] * 12 + k[2]));
		Expect(
			off <= 6,
			QStringLiteral("id %1 -> %2-%3").arg(k[0]).arg(e->year).arg(e->month));
	}
}

// 07.old-and-new-ids
LUCKGRAM_TEST(D9ProfileOldAndNew) {
	using namespace Luckgram::ProfileInfo;
	const auto old = EstimateRegistration(BundledTable(), 1000);
	Expect(old.has_value() && old->before2014, "1000 is before 2014");
	ExpectEq(FormatEstimate(old, true), U("早于 2014 年"), "before zh");
	ExpectEq(FormatEstimate(old, false), QStringLiteral("before 2014"), "before en");
	Expect(
		!EstimateRegistration(BundledTable(), 9'900'000'000LL),
		"newer than table");
	Expect(!EstimateRegistration(BundledTable(), 0), "zero id");
	Expect(!EstimateRegistration({}, 805158066LL), "no table");
	Expect(FormatEstimate(std::nullopt, true).isEmpty(), "nullopt is empty");
}

// 07.monotonic
LUCKGRAM_TEST(D9ProfileMonotonic) {
	using namespace Luckgram::ProfileInfo;
	auto last = 0;
	for (auto id = qint64(10'000'000); id < qint64(8'300'000'000LL); id += 25'000'000) {
		const auto e = EstimateRegistration(BundledTable(), id);
		if (!e || e->before2014) {
			continue;
		}
		const auto m = e->year * 12 + e->month;
		Expect(m >= last, QStringLiteral("monotonic at %1").arg(id));
		last = m;
	}
}

// 07.format-says-about
LUCKGRAM_TEST(D9ProfileFormat) {
	using namespace Luckgram::ProfileInfo;
	const auto e = EstimateRegistration(
		ParseTable(QStringLiteral("100,2019-01\n200,2020-01\n")),
		175);
	ExpectEq(FormatEstimate(e, true), U("约 2019 年 10 月"), "zh");
	ExpectEq(FormatEstimate(e, false), QStringLiteral("about Oct 2019"), "en");
}

// 07.malformed-lines
LUCKGRAM_TEST(D9ProfileMalformed) {
	using namespace Luckgram::ProfileInfo;
	const auto t = ParseTable(
		QStringLiteral("# c\n\n100,2019-01\nx,2019-02\n90,2019-03\n150,2018-01\n"
		"200,2019-13\n300,2020-01\n"));
	ExpectEq(qint64(t.size()), 2, "kept rows");
}

// ---- 05 private notes

const auto kNoah = U("柏林设计周认识的。欠我 40 欧元");
const auto kFamily = U("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7");
const auto kFlagCn = U("\xF0\x9F\x87\xA8\xF0\x9F\x87\xB3");
const auto kThumbDark = U("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBF");
const auto kKeycap1 = U("1\xEF\xB8\x8F\xE2\x83\xA3");
const auto kGrin = U("\xF0\x9F\x98\x80");

// 05.normalize
LUCKGRAM_TEST(D9NotesNormalize) {
	using namespace Luckgram::NotesText;
	ExpectEq(
		Normalize(U("  第一行\n第二行\n\n ")),
		U("第一行\n第二行"),
		"trim, keep inner line break");
	ExpectEq(Normalize(QString()), QString(), "null");
	ExpectEq(Normalize(QStringLiteral(" \n\t ")), QString(), "blank");
	const auto exact = Repeat(U("字"), 2000);
	ExpectEq(Normalize(exact), exact, "2000 kept");
	ExpectEq(GraphemeCount(Normalize(Repeat(U("字"), 2500))), 2000, "cut 2500");
	const auto emoji = Normalize(Repeat(kGrin, 2001));
	ExpectEq(GraphemeCount(emoji), 2000, "2001 emoji -> 2000");
	ExpectEq(qint64(emoji.size()), 4000, "surrogate pairs kept whole");
}

// 05.graphemes
LUCKGRAM_TEST(D9NotesGraphemes) {
	using namespace Luckgram::NotesText;
	ExpectEq(GraphemeCount(kFamily), 1, "zwj family");
	ExpectEq(GraphemeCount(kFlagCn), 1, "flag");
	ExpectEq(GraphemeCount(kThumbDark), 1, "skin tone");
	ExpectEq(GraphemeCount(kKeycap1), 1, "keycap");
	ExpectEq(GraphemeCount(U("e\xCC\x81")), 1, "combining accent");
	ExpectEq(GraphemeCount(kFlagCn + kFlagCn), 2, "two flags");
	ExpectEq(GraphemeCount(QStringLiteral("\r\n")), 1, "crlf");
	const auto families = Repeat(kFamily, 2000);
	ExpectEq(Normalize(families), families, "2000 families fit");
	ExpectEq(Remaining(families), 0, "nothing left");
	ExpectEq(
		Normalize(Repeat(kFamily, 2001)),
		families,
		"cut never splits a zwj sequence");
	ExpectEq(OffsetOfGrapheme(kFamily + kFamily, 1), qint64(kFamily.size()), "offset");
	ExpectEq(OffsetOfGrapheme(QStringLiteral("ab"), 5), 2, "offset past end");
}

// 05.lines-and-search
LUCKGRAM_TEST(D9NotesLinesAndSearch) {
	using namespace Luckgram::NotesText;
	ExpectEq(
		FirstLine(U("\n  \n柏林设计周认识的。\n欠我 40 欧元")),
		U("柏林设计周认识的。"),
		"first line skips empty");
	ExpectEq(FirstLine(kNoah), kNoah, "single line");
	ExpectEq(FirstLine(QString()), QString(), "null first line");
	Expect(Matches(U("欧元"), kNoah, QStringLiteral("Noah")), "find by note text");
	Expect(Matches(QStringLiteral("noah"), kNoah, QStringLiteral("Noah")), "find by name");
	Expect(Matches(QStringLiteral(" NOAH "), QString(), QStringLiteral("Noah Schmidt")), "trim + case");
	Expect(!Matches(U("美元"), kNoah, QStringLiteral("Noah")), "miss");
	Expect(Matches(QString(), kNoah, QStringLiteral("Noah")), "empty query");
	const auto note = U("柏林设计周认识的\n欠我 40 欧元");
	ExpectEq(Snippet(note, U("欧元")), U("欠我 40 欧元"), "matching line");
	ExpectEq(Snippet(note, QStringLiteral("Noah")), U("柏林设计周认识的"), "name match");
	ExpectEq(Snippet(note, QString()), U("柏林设计周认识的"), "no query");
}

// ---- 06 keyword folders

// 06.chinese-substring
LUCKGRAM_TEST(D9KeywordsChinese) {
	using namespace Luckgram::Keywords;
	Expect(Matches(U("请把电子发票发我"), U("发票")), "substring");
	Expect(Matches(U("发票"), U("发票")), "whole");
	Expect(Matches(U("增值税发票号码"), U("发票")), "inside");
}

// 06.english-whole-word
LUCKGRAM_TEST(D9KeywordsEnglish) {
	using namespace Luckgram::Keywords;
	Expect(Matches(QStringLiteral("Invoice #12 attached"), QStringLiteral("invoice")), "word");
	Expect(Matches(QStringLiteral("see the invoice."), QStringLiteral("invoice")), "dot");
	Expect(Matches(QStringLiteral("(invoice)"), QStringLiteral("invoice")), "parens");
	Expect(!Matches(QStringLiteral("two invoices"), QStringLiteral("invoice")), "plural");
	Expect(!Matches(QStringLiteral("reinvoice"), QStringLiteral("invoice")), "prefix");
	Expect(!Matches(QStringLiteral("invoice_id"), QStringLiteral("invoice")), "underscore");
	Expect(Matches(U("这是invoice号"), QStringLiteral("invoice")), "next to chinese");
	Expect(Matches(QStringLiteral("INVOICE"), QStringLiteral("invoice")), "case 1");
	Expect(Matches(QStringLiteral("invoice"), QStringLiteral("INVOICE")), "case 2");
	ExpectEq(IndexOf(QStringLiteral("invoices, invoice"), QStringLiteral("invoice"), 0), 10, "skip rejected");
	ExpectEq(IndexOf(QStringLiteral("invoice"), QStringLiteral("invoice"), 1), -1, "from");
	Expect(!Matches(QStringLiteral("anything"), QString()), "empty keyword");
}

// 06.folder-order
LUCKGRAM_TEST(D9KeywordsFolderOrder) {
	using namespace Luckgram::Keywords;
	const auto invoice = QStringList{ U("发票"), QStringLiteral("invoice") };
	ExpectEq(FirstMatch(U("invoice 和 发票"), invoice), U("发票"), "order");
	ExpectEq(FirstMatch(QStringLiteral("Invoice only"), invoice), QStringLiteral("invoice"), "second");
	Expect(FirstMatch(QStringLiteral("nothing here"), invoice).isEmpty(), "none");
	Expect(!MatchesAny(QString(), invoice), "empty text");
}

// 06.highlights
LUCKGRAM_TEST(D9KeywordsHighlights) {
	using namespace Luckgram::Keywords;
	const auto text = U("发票 invoice 发票发票");
	const auto ranges = Highlights(text, { U("发票"), QStringLiteral("invoice") });
	// 发票 at 0, invoice at 3, 发票发票 at 11 (merged).
	ExpectEq(qint64(ranges.size()), 3, "ranges");
	if (ranges.size() == 3) {
		ExpectEq(ranges[0].from, 0, "r0");
		ExpectEq(ranges[0].length, 2, "r0 len");
		ExpectEq(ranges[1].from, 3, "r1");
		ExpectEq(ranges[1].length, 7, "r1 len");
		ExpectEq(ranges[2].from, 11, "r2");
		ExpectEq(ranges[2].length, 4, "r2 len");
	}
	Expect(Highlights(QStringLiteral("none"), { QStringLiteral("invoice") }).empty(), "no hits");
}

// 06.folder-model
LUCKGRAM_TEST(D9KeywordsModel) {
	using namespace Luckgram::Keywords;
	const auto clean = CleanKeywords({ QStringLiteral(" invoice "), QStringLiteral(""), QStringLiteral("Invoice"), U("发票") });
	ExpectEq(clean.join(QStringLiteral(",")), U("invoice,发票"), "clean");
	auto many = QStringList();
	for (auto i = 0; i != 15; ++i) {
		many.push_back(QStringLiteral("k%1").arg(i));
	}
	ExpectEq(qint64(CleanKeywords(many).size()), kMaxKeywords, "max keywords");

	Expect(Validate({ .name = QStringLiteral(" "), .keywords = { QStringLiteral("a") } }) == Invalid::Name, "name");
	Expect(Validate({ .name = QStringLiteral("n"), .keywords = { QStringLiteral(" ") } }) == Invalid::Keywords, "keywords");
	Expect(Validate({ .name = QStringLiteral("n"), .keywords = { QStringLiteral("a") }, .scope = 0 }) == Invalid::Scope, "scope");
	Expect(Validate({ .name = QStringLiteral("n"), .keywords = { QStringLiteral("a") } }) == Invalid::None, "valid");

	Expect(InScope(kScopeAll, ChatKind::Channel), "all");
	Expect(InScope(kScopePrivate | kScopeGroups, ChatKind::Group), "groups");
	Expect(!InScope(kScopePrivate | kScopeGroups, ChatKind::Channel), "no channels");

	const auto folders = std::vector<Folder>{
		{ .id = 1, .name = U("发票"), .keywords = { U("发票"), QStringLiteral("invoice") }, .scope = kScopeAll, .order = 3 },
		{ .id = 4, .name = QStringLiteral("Work"), .keywords = { QStringLiteral("deadline") }, .scope = kScopeGroups, .order = 7 },
	};
	const auto back = Deserialize(Serialize(folders));
	Expect(back == folders, "json round trip");
	ExpectEq(NextId(folders), 5, "next id");
	ExpectEq(NextId({}), 1, "first id");
	const auto bad = Deserialize(
		R"([{"id":0,"name":"x","keywords":["a"]},)"
		R"({"id":2,"name":"y","keywords":[]},)"
		R"({"id":3,"name":"z","keywords":["b"],"scope":0},)"
		R"({"id":3,"name":"dup","keywords":["c"]}])");
	ExpectEq(qint64(bad.size()), 1, "bad entries dropped");
	if (!bad.empty()) {
		ExpectEq(bad[0].scope, kScopeAll, "scope 0 -> all");
	}
	Expect(Deserialize("not json").empty(), "garbage");
}

// ---- 16 activity in shared groups

// 16.merge
LUCKGRAM_TEST(D9ActivityMerge) {
	using namespace Luckgram::SharedActivity;
	const auto merged = NewestFirst({
		{ { 100, 1 }, { 300, 2 } },
		{ { 250, 3 }, { 50, 4 } },
		{ { 400, 5 } },
	});
	auto dates = QStringList();
	for (const auto &item : merged) {
		dates.push_back(QString::number(item.date));
	}
	ExpectEq(dates.join(QStringLiteral(",")), QStringLiteral("400,300,250,100,50"), "newest first");

	auto perGroup = std::vector<std::vector<Dated>>();
	for (auto g = 0; g != 7; ++g) {
		auto list = std::vector<Dated>();
		for (auto i = 0; i != kPerGroup; ++i) {
			list.push_back({ 7000 + g * 3 + i % 20, qint64(g * 100 + i) });
		}
		perGroup.push_back(std::move(list));
	}
	const auto limited = NewestFirst(perGroup);
	ExpectEq(qint64(limited.size()), kMaxItems, "at most 100");
	for (auto i = 1; i < int(limited.size()); ++i) {
		Expect(limited[i - 1].date >= limited[i].date, "sorted");
	}
	const auto ties = NewestFirst({ { { 10, 1 } }, {}, { { 10, 2 } } });
	ExpectEq(ties.front().sortId, 2, "same date: higher id first");
}

// 16.flood-wait
LUCKGRAM_TEST(D9ActivityFlood) {
	using namespace Luckgram::SharedActivity;
	ExpectEq(FloodWaitSeconds(QStringLiteral("FLOOD_WAIT_12")), 12, "flood");
	ExpectEq(FloodWaitSeconds(QStringLiteral("FLOOD_PREMIUM_WAIT_3")), 3, "premium flood");
	ExpectEq(FloodWaitSeconds(QStringLiteral("PEER_ID_INVALID")), -1, "other");
	ExpectEq(FloodWaitSeconds(QStringLiteral("FLOOD_WAIT_x")), -1, "bad number");
	ExpectEq(FloodWaitSeconds(QString()), -1, "empty");
	Expect(CanWaitFlood(3, 0), "short wait");
	Expect(!CanWaitFlood(30, 0), "beyond budget");
	Expect(!CanWaitFlood(3, kTimeBudgetMs - 1000), "late");
	Expect(!CanWaitFlood(-1, 0), "not a flood");
	Expect(!CanWaitFlood(kMaxFloodWaitSeconds + 1, 0), "too long");
}

// 16.cache-and-header
LUCKGRAM_TEST(D9ActivityCacheAndHeader) {
	using namespace Luckgram::SharedActivity;
	const auto t = qint64(1'700'000'000'000LL);
	Expect(IsFresh(t, t + 9 * 60 * 1000), "9 minutes fresh");
	Expect(!IsFresh(t, t + 10 * 60 * 1000), "10 minutes stale");
	Expect(!IsFresh(0, t), "never loaded");
	ExpectEq(Header(3, false, true), U("最近消息 · 3 个共同群组"), "zh");
	ExpectEq(
		Header(3, false, false),
		QStringLiteral("Recent messages · 3 groups in common"),
		"en");
	ExpectEq(
		Header(1, false, false),
		QStringLiteral("Recent messages · 1 group in common"),
		"en singular");
	ExpectEq(
		Header(3, true, true),
		U("最近消息 · 3 个共同群组（部分群组未加载完，点击「刷新」重试）"),
		"zh partial");
	ExpectEq(
		Header(1, true, false),
		QStringLiteral("Recent messages · 1 group in common (some groups didn't load, click Refresh to retry)"),
		"en partial");
}

// ---- 08 storage

// 08.options
LUCKGRAM_TEST(D9CacheOptions) {
	using namespace Luckgram::Cache;
	const auto gb = qint64(1024) * 1024 * 1024;
	ExpectEq(LimitBytes(0), gb, "1 GB");
	ExpectEq(LimitBytes(6), 0, "no limit");
	ExpectEq(LimitLabel(0, true), QStringLiteral("1 GB"), "1 GB label");
	ExpectEq(LimitLabel(6, true), U("不限"), "no limit zh");
	ExpectEq(LimitLabel(6, false), QStringLiteral("No limit"), "no limit en");
	ExpectEq(LimitIndex(0), 6, "0 is unlimited");
	ExpectEq(LimitIndex(gb), 0, "1 GB index");
	ExpectEq(LimitIndex(8 * gb), 3, "8 GB -> 10 GB");
	ExpectEq(LimitIndex(500 * gb), 6, "huge -> unlimited");
	ExpectEq(KeepSeconds(0), 3600, "1 hour");
	ExpectEq(KeepSeconds(5), 0, "forever");
	ExpectEq(KeepIndex(0), 5, "0 is forever");
	ExpectEq(KeepIndex(86400), 1, "1 day");
	ExpectEq(KeepIndex(2 * 86400), 1, "2 days -> 1 day (closest, ties to shorter)");
	ExpectEq(KeepIndex(365 * 86400), 5, "1 year -> forever");
	ExpectEq(KeepLabel(0, true), U("1 小时"), "keep zh");
	ExpectEq(KeepLabel(4, false), QStringLiteral("1 month"), "keep en");
	ExpectEq(KeepLabel(5, true), U("永久"), "forever zh");
}
