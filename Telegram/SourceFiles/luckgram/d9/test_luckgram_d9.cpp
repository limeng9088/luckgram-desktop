/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
// D9 unit tests: the pure rules of 05 / 06 / 07 / 08 / 16. Cases are ported
// from mobile (Android ProfileInfoLogicTest, UserNotesTextTest,
// KeywordMatcherTest, SharedActivityLogicTest). Run: test_luckgram_d9.
#include "luckgram/d9/luckgram_cache_logic.h"
#include "luckgram/d9/luckgram_keyword_logic.h"
#include "luckgram/d9/luckgram_notes_text.h"
#include "luckgram/d9/luckgram_profile_info_logic.h"
#include "luckgram/d9/luckgram_shared_activity_logic.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

namespace {

struct Check {
	const char *name = nullptr;
	std::function<QString()> run;
};

QString Failure;

void Expect(bool condition, const QString &what) {
	if (!condition && Failure.isEmpty()) {
		Failure = what;
	}
}

void ExpectEq(const QString &actual, const QString &expected, const char *what) {
	Expect(
		actual == expected,
		u"%1: got \"%2\", expected \"%3\""_q
			.arg(QString::fromUtf8(what), actual, expected));
}

void ExpectEq(int64 actual, int64 expected, const char *what) {
	Expect(
		actual == expected,
		u"%1: got %2, expected %3"_q
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

// ---- 07 profile details

void ProfileIds() {
	using namespace Luckgram::ProfileInfo;
	ExpectEq(DisplayId(PeerKind::User, 777000), u"777000"_q, "user id");
	ExpectEq(
		DisplayId(PeerKind::Channel, 1234567890),
		u"-1001234567890"_q,
		"channel id");
	ExpectEq(DisplayId(PeerKind::LegacyGroup, 123456), u"-123456"_q, "group id");
	ExpectEq(UserLink(PeerKind::User, 42), u"tg://user?id=42"_q, "user link");
	Expect(UserLink(PeerKind::Channel, 42).isEmpty(), "channel has no link");
	Expect(UserLink(PeerKind::LegacyGroup, 42).isEmpty(), "group has no link");
}

void ProfileTableCoverage() {
	using namespace Luckgram::ProfileInfo;
	const auto &t = BundledTable();
	Expect(t.size() >= 45, u"rows: %1"_q.arg(t.size()));
	if (t.size() < 2) {
		return;
	}
	ExpectEq(t.front().monthIndex / 12, 2013, "first year");
	Expect(t.back().monthIndex / 12 >= 2026, "last year >= 2026");
	for (auto i = 1; i < int(t.size()); ++i) {
		Expect(t[i].id > t[i - 1].id, u"ids grow at %1"_q.arg(i));
		Expect(
			t[i].monthIndex - t[i - 1].monthIndex <= 3,
			u"gap > 1 quarter at row %1"_q.arg(i));
	}
}

void ProfileKnownAccounts() {
	using namespace Luckgram::ProfileInfo;
	const int64 known[][3] = {
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
		Expect(e.has_value(), u"id %1 estimated"_q.arg(k[0]));
		if (!e) {
			continue;
		}
		Expect(!e->before2014, u"id %1 not before 2014"_q.arg(k[0]));
		const auto off = std::abs(
			(e->year * 12 + e->month) - int(k[1] * 12 + k[2]));
		Expect(
			off <= 6,
			u"id %1 -> %2-%3"_q.arg(k[0]).arg(e->year).arg(e->month));
	}
}

void ProfileOldAndNew() {
	using namespace Luckgram::ProfileInfo;
	const auto old = EstimateRegistration(BundledTable(), 1000);
	Expect(old.has_value() && old->before2014, "1000 is before 2014");
	ExpectEq(FormatEstimate(old, true), U("早于 2014 年"), "before zh");
	ExpectEq(FormatEstimate(old, false), u"before 2014"_q, "before en");
	Expect(
		!EstimateRegistration(BundledTable(), 9'900'000'000LL),
		"newer than table");
	Expect(!EstimateRegistration(BundledTable(), 0), "zero id");
	Expect(!EstimateRegistration({}, 805158066LL), "no table");
	Expect(FormatEstimate(std::nullopt, true).isEmpty(), "nullopt is empty");
}

void ProfileMonotonic() {
	using namespace Luckgram::ProfileInfo;
	auto last = 0;
	for (auto id = int64(10'000'000); id < int64(8'300'000'000LL); id += 25'000'000) {
		const auto e = EstimateRegistration(BundledTable(), id);
		if (!e || e->before2014) {
			continue;
		}
		const auto m = e->year * 12 + e->month;
		Expect(m >= last, u"monotonic at %1"_q.arg(id));
		last = m;
	}
}

void ProfileFormat() {
	using namespace Luckgram::ProfileInfo;
	const auto e = EstimateRegistration(
		ParseTable(u"100,2019-01\n200,2020-01\n"_q),
		175);
	ExpectEq(FormatEstimate(e, true), U("约 2019 年 10 月"), "zh");
	ExpectEq(FormatEstimate(e, false), u"about Oct 2019"_q, "en");
}

void ProfileMalformed() {
	using namespace Luckgram::ProfileInfo;
	const auto t = ParseTable(
		u"# c\n\n100,2019-01\nx,2019-02\n90,2019-03\n150,2018-01\n"
		"200,2019-13\n300,2020-01\n"_q);
	ExpectEq(int64(t.size()), 2, "kept rows");
}

// ---- 05 private notes

const auto kNoah = U("柏林设计周认识的。欠我 40 欧元");
const auto kFamily = U("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7");
const auto kFlagCn = U("\xF0\x9F\x87\xA8\xF0\x9F\x87\xB3");
const auto kThumbDark = U("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBF");
const auto kKeycap1 = U("1\xEF\xB8\x8F\xE2\x83\xA3");
const auto kGrin = U("\xF0\x9F\x98\x80");

void NotesNormalize() {
	using namespace Luckgram::NotesText;
	ExpectEq(
		Normalize(U("  第一行\n第二行\n\n ")),
		U("第一行\n第二行"),
		"trim, keep inner line break");
	ExpectEq(Normalize(QString()), QString(), "null");
	ExpectEq(Normalize(u" \n\t "_q), QString(), "blank");
	const auto exact = Repeat(U("字"), 2000);
	ExpectEq(Normalize(exact), exact, "2000 kept");
	ExpectEq(GraphemeCount(Normalize(Repeat(U("字"), 2500))), 2000, "cut 2500");
	const auto emoji = Normalize(Repeat(kGrin, 2001));
	ExpectEq(GraphemeCount(emoji), 2000, "2001 emoji -> 2000");
	ExpectEq(int64(emoji.size()), 4000, "surrogate pairs kept whole");
}

void NotesGraphemes() {
	using namespace Luckgram::NotesText;
	ExpectEq(GraphemeCount(kFamily), 1, "zwj family");
	ExpectEq(GraphemeCount(kFlagCn), 1, "flag");
	ExpectEq(GraphemeCount(kThumbDark), 1, "skin tone");
	ExpectEq(GraphemeCount(kKeycap1), 1, "keycap");
	ExpectEq(GraphemeCount(U("e\xCC\x81")), 1, "combining accent");
	ExpectEq(GraphemeCount(kFlagCn + kFlagCn), 2, "two flags");
	ExpectEq(GraphemeCount(u"\r\n"_q), 1, "crlf");
	const auto families = Repeat(kFamily, 2000);
	ExpectEq(Normalize(families), families, "2000 families fit");
	ExpectEq(Remaining(families), 0, "nothing left");
	ExpectEq(
		Normalize(Repeat(kFamily, 2001)),
		families,
		"cut never splits a zwj sequence");
	ExpectEq(OffsetOfGrapheme(kFamily + kFamily, 1), int64(kFamily.size()), "offset");
	ExpectEq(OffsetOfGrapheme(u"ab"_q, 5), 2, "offset past end");
}

void NotesLinesAndSearch() {
	using namespace Luckgram::NotesText;
	ExpectEq(
		FirstLine(U("\n  \n柏林设计周认识的。\n欠我 40 欧元")),
		U("柏林设计周认识的。"),
		"first line skips empty");
	ExpectEq(FirstLine(kNoah), kNoah, "single line");
	ExpectEq(FirstLine(QString()), QString(), "null first line");
	Expect(Matches(U("欧元"), kNoah, u"Noah"_q), "find by note text");
	Expect(Matches(u"noah"_q, kNoah, u"Noah"_q), "find by name");
	Expect(Matches(u" NOAH "_q, QString(), u"Noah Schmidt"_q), "trim + case");
	Expect(!Matches(U("美元"), kNoah, u"Noah"_q), "miss");
	Expect(Matches(QString(), kNoah, u"Noah"_q), "empty query");
	const auto note = U("柏林设计周认识的\n欠我 40 欧元");
	ExpectEq(Snippet(note, U("欧元")), U("欠我 40 欧元"), "matching line");
	ExpectEq(Snippet(note, u"Noah"_q), U("柏林设计周认识的"), "name match");
	ExpectEq(Snippet(note, QString()), U("柏林设计周认识的"), "no query");
}

// ---- 06 keyword folders

void KeywordsChinese() {
	using namespace Luckgram::Keywords;
	Expect(Matches(U("请把电子发票发我"), U("发票")), "substring");
	Expect(Matches(U("发票"), U("发票")), "whole");
	Expect(Matches(U("增值税发票号码"), U("发票")), "inside");
}

void KeywordsEnglish() {
	using namespace Luckgram::Keywords;
	Expect(Matches(u"Invoice #12 attached"_q, u"invoice"_q), "word");
	Expect(Matches(u"see the invoice."_q, u"invoice"_q), "dot");
	Expect(Matches(u"(invoice)"_q, u"invoice"_q), "parens");
	Expect(!Matches(u"two invoices"_q, u"invoice"_q), "plural");
	Expect(!Matches(u"reinvoice"_q, u"invoice"_q), "prefix");
	Expect(!Matches(u"invoice_id"_q, u"invoice"_q), "underscore");
	Expect(Matches(U("这是invoice号"), u"invoice"_q), "next to chinese");
	Expect(Matches(u"INVOICE"_q, u"invoice"_q), "case 1");
	Expect(Matches(u"invoice"_q, u"INVOICE"_q), "case 2");
	ExpectEq(IndexOf(u"invoices, invoice"_q, u"invoice"_q, 0), 10, "skip rejected");
	ExpectEq(IndexOf(u"invoice"_q, u"invoice"_q, 1), -1, "from");
	Expect(!Matches(u"anything"_q, QString()), "empty keyword");
}

void KeywordsFolderOrder() {
	using namespace Luckgram::Keywords;
	const auto invoice = QStringList{ U("发票"), u"invoice"_q };
	ExpectEq(FirstMatch(U("invoice 和 发票"), invoice), U("发票"), "order");
	ExpectEq(FirstMatch(u"Invoice only"_q, invoice), u"invoice"_q, "second");
	Expect(FirstMatch(u"nothing here"_q, invoice).isEmpty(), "none");
	Expect(!MatchesAny(QString(), invoice), "empty text");
}

void KeywordsHighlights() {
	using namespace Luckgram::Keywords;
	const auto text = U("发票 invoice 发票发票");
	const auto ranges = Highlights(text, { U("发票"), u"invoice"_q });
	// 发票 at 0, invoice at 3, 发票发票 at 11 (merged).
	ExpectEq(int64(ranges.size()), 3, "ranges");
	if (ranges.size() == 3) {
		ExpectEq(ranges[0].from, 0, "r0");
		ExpectEq(ranges[0].length, 2, "r0 len");
		ExpectEq(ranges[1].from, 3, "r1");
		ExpectEq(ranges[1].length, 7, "r1 len");
		ExpectEq(ranges[2].from, 11, "r2");
		ExpectEq(ranges[2].length, 4, "r2 len");
	}
	Expect(Highlights(u"none"_q, { u"invoice"_q }).empty(), "no hits");
}

void KeywordsModel() {
	using namespace Luckgram::Keywords;
	const auto clean = CleanKeywords({ u" invoice "_q, u""_q, u"Invoice"_q, U("发票") });
	ExpectEq(clean.join(u","_q), U("invoice,发票"), "clean");
	auto many = QStringList();
	for (auto i = 0; i != 15; ++i) {
		many.push_back(u"k%1"_q.arg(i));
	}
	ExpectEq(int64(CleanKeywords(many).size()), kMaxKeywords, "max keywords");

	Expect(Validate({ .name = u" "_q, .keywords = { u"a"_q } }) == Invalid::Name, "name");
	Expect(Validate({ .name = u"n"_q, .keywords = { u" "_q } }) == Invalid::Keywords, "keywords");
	Expect(Validate({ .name = u"n"_q, .keywords = { u"a"_q }, .scope = 0 }) == Invalid::Scope, "scope");
	Expect(Validate({ .name = u"n"_q, .keywords = { u"a"_q } }) == Invalid::None, "valid");

	Expect(InScope(kScopeAll, ChatKind::Channel), "all");
	Expect(InScope(kScopePrivate | kScopeGroups, ChatKind::Group), "groups");
	Expect(!InScope(kScopePrivate | kScopeGroups, ChatKind::Channel), "no channels");

	const auto folders = std::vector<Folder>{
		{ .id = 1, .name = U("发票"), .keywords = { U("发票"), u"invoice"_q }, .scope = kScopeAll, .order = 3 },
		{ .id = 4, .name = u"Work"_q, .keywords = { u"deadline"_q }, .scope = kScopeGroups, .order = 7 },
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
	ExpectEq(int64(bad.size()), 1, "bad entries dropped");
	if (!bad.empty()) {
		ExpectEq(bad[0].scope, kScopeAll, "scope 0 -> all");
	}
	Expect(Deserialize("not json").empty(), "garbage");
}

// ---- 16 activity in shared groups

void ActivityMerge() {
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
	ExpectEq(dates.join(u","_q), u"400,300,250,100,50"_q, "newest first");

	auto perGroup = std::vector<std::vector<Dated>>();
	for (auto g = 0; g != 7; ++g) {
		auto list = std::vector<Dated>();
		for (auto i = 0; i != kPerGroup; ++i) {
			list.push_back({ 7000 + g * 3 + i % 20, int64(g * 100 + i) });
		}
		perGroup.push_back(std::move(list));
	}
	const auto limited = NewestFirst(perGroup);
	ExpectEq(int64(limited.size()), kMaxItems, "at most 100");
	for (auto i = 1; i < int(limited.size()); ++i) {
		Expect(limited[i - 1].date >= limited[i].date, "sorted");
	}
	const auto ties = NewestFirst({ { { 10, 1 } }, {}, { { 10, 2 } } });
	ExpectEq(ties.front().sortId, 2, "same date: higher id first");
}

void ActivityFlood() {
	using namespace Luckgram::SharedActivity;
	ExpectEq(FloodWaitSeconds(u"FLOOD_WAIT_12"_q), 12, "flood");
	ExpectEq(FloodWaitSeconds(u"FLOOD_PREMIUM_WAIT_3"_q), 3, "premium flood");
	ExpectEq(FloodWaitSeconds(u"PEER_ID_INVALID"_q), -1, "other");
	ExpectEq(FloodWaitSeconds(u"FLOOD_WAIT_x"_q), -1, "bad number");
	ExpectEq(FloodWaitSeconds(QString()), -1, "empty");
	Expect(CanWaitFlood(3, 0), "short wait");
	Expect(!CanWaitFlood(30, 0), "beyond budget");
	Expect(!CanWaitFlood(3, kTimeBudgetMs - 1000), "late");
	Expect(!CanWaitFlood(-1, 0), "not a flood");
	Expect(!CanWaitFlood(kMaxFloodWaitSeconds + 1, 0), "too long");
}

void ActivityCacheAndHeader() {
	using namespace Luckgram::SharedActivity;
	const auto t = int64(1'700'000'000'000LL);
	Expect(IsFresh(t, t + 9 * 60 * 1000), "9 minutes fresh");
	Expect(!IsFresh(t, t + 10 * 60 * 1000), "10 minutes stale");
	Expect(!IsFresh(0, t), "never loaded");
	ExpectEq(Header(3, false, true), U("最近消息 · 3 个共同群组"), "zh");
	ExpectEq(
		Header(3, false, false),
		u"Recent messages · 3 groups in common"_q,
		"en");
	ExpectEq(
		Header(1, false, false),
		u"Recent messages · 1 group in common"_q,
		"en singular");
	ExpectEq(
		Header(3, true, true),
		U("最近消息 · 3 个共同群组（部分群组未加载完，点击「刷新」重试）"),
		"zh partial");
	ExpectEq(
		Header(1, true, false),
		u"Recent messages · 1 group in common (some groups didn't load, click Refresh to retry)"_q,
		"en partial");
}

// ---- 08 storage

void CacheOptions() {
	using namespace Luckgram::Cache;
	const auto gb = int64(1024) * 1024 * 1024;
	ExpectEq(LimitBytes(0), gb, "1 GB");
	ExpectEq(LimitBytes(6), 0, "no limit");
	ExpectEq(LimitLabel(0, true), u"1 GB"_q, "1 GB label");
	ExpectEq(LimitLabel(6, true), U("不限"), "no limit zh");
	ExpectEq(LimitLabel(6, false), u"No limit"_q, "no limit en");
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
	ExpectEq(KeepLabel(4, false), u"1 month"_q, "keep en");
	ExpectEq(KeepLabel(5, true), U("永久"), "forever zh");
}

[[nodiscard]] std::vector<Check> Checks() {
	const auto wrap = [](void (*test)()) {
		return [=] {
			Failure = QString();
			test();
			return Failure;
		};
	};
	return {
		{ "07.ids", wrap(ProfileIds) },
		{ "07.table-coverage", wrap(ProfileTableCoverage) },
		{ "07.known-accounts-within-6-months", wrap(ProfileKnownAccounts) },
		{ "07.old-and-new-ids", wrap(ProfileOldAndNew) },
		{ "07.monotonic", wrap(ProfileMonotonic) },
		{ "07.format-says-about", wrap(ProfileFormat) },
		{ "07.malformed-lines", wrap(ProfileMalformed) },
		{ "05.normalize", wrap(NotesNormalize) },
		{ "05.graphemes", wrap(NotesGraphemes) },
		{ "05.lines-and-search", wrap(NotesLinesAndSearch) },
		{ "06.chinese-substring", wrap(KeywordsChinese) },
		{ "06.english-whole-word", wrap(KeywordsEnglish) },
		{ "06.folder-order", wrap(KeywordsFolderOrder) },
		{ "06.highlights", wrap(KeywordsHighlights) },
		{ "06.folder-model", wrap(KeywordsModel) },
		{ "16.merge", wrap(ActivityMerge) },
		{ "16.flood-wait", wrap(ActivityFlood) },
		{ "16.cache-and-header", wrap(ActivityCacheAndHeader) },
		{ "08.options", wrap(CacheOptions) },
	};
}

} // namespace

int main() {
	const auto checks = Checks();
	auto passed = 0;
	for (const auto &check : checks) {
		const auto failure = check.run();
		if (failure.isEmpty()) {
			std::printf("PASS %s\n", check.name);
			++passed;
		} else {
			std::printf(
				"FAIL %s: %s\n",
				check.name,
				failure.toUtf8().constData());
		}
	}
	std::printf("%d of %d checks passed.\n", passed, int(checks.size()));
	return (passed == int(checks.size())) ? 0 : 1;
}
