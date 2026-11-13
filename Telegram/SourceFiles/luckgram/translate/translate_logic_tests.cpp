/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
// Cases ported from mobile TranslateInLogicTest, TranslateFallbackLogicTest
// and TranslateCacheTest.
#include "luckgram/tests/luckgram_test.h"
#include "luckgram/translate/translate_logic.h"

namespace {

using namespace Luckgram::Translate;

QString S(const char *utf8) {
	return QString::fromUtf8(utf8);
}

QStringList L(std::initializer_list<const char*> list) {
	auto result = QStringList();
	for (const auto item : list) {
		result.push_back(S(item));
	}
	return result;
}

constexpr auto kDay = int64_t(24) * 60 * 60 * 1000;

} // namespace

LUCKGRAM_TEST(TranslateButtonForForeignIncomingTextOnly) {
	const auto zh = L({ "zh" });
	LG_CHECK(ShowButton(true, false, true, S("en"), zh, S("zh")));
	LG_CHECK(!ShowButton(false, false, true, S("en"), zh, S("zh")));
	LG_CHECK(!ShowButton(true, true, true, S("en"), zh, S("zh")));
	LG_CHECK(!ShowButton(true, false, false, S("en"), zh, S("zh")));
	LG_CHECK(!ShowButton(true, false, true, QString(), zh, S("zh")));
	LG_CHECK(!ShowButton(true, false, true, S("und"), zh, S("zh")));
}

LUCKGRAM_TEST(TranslateDoNotTranslateLanguagesHideTheButton) {
	LG_CHECK(!ShowButton(true, false, true, S("zh-TW"), L({ "zh" }), S("zh")));
	LG_CHECK(!ShowButton(true, false, true, S("en"), L({ "zh", "en" }), S("zh")));
	LG_CHECK(ShowButton(true, false, true, S("es"), L({ "zh", "en" }), S("zh")));
	LG_CHECK(!ShowButton(true, false, true, S("en"), QStringList(), S("en_US")));
	LG_CHECK(!ShowButton(true, false, true, S("nb"), L({ "no" }), S("zh")));
}

LUCKGRAM_TEST(TranslateBaseLanguage) {
	LG_EQUAL(Base(S("en_US")), S("en"));
	LG_EQUAL(Base(S("zh-Hant")), S("zh"));
	LG_EQUAL(Base(S("nb")), S("no"));
	LG_CHECK(Base(S(" ")).isEmpty());
	LG_CHECK(Base(S("und")).isEmpty());
	LG_CHECK(Base(QString()).isEmpty());
}

LUCKGRAM_TEST(TranslateTargetLanguageIsOnlyTheUsersChoice) {
	LG_EQUAL(TargetLanguage(S("es")), S("es"));
	LG_EQUAL(TargetLanguage(S(" ja ")), S("ja"));
	LG_CHECK(TargetLanguage(S("")).isEmpty());
	LG_CHECK(TargetLanguage(QString()).isEmpty());
}

LUCKGRAM_TEST(TranslateChatLanguageOverridesDefault) {
	LG_EQUAL(EffectiveLanguage(S("ja"), S("zh")), S("ja"));
	LG_EQUAL(EffectiveLanguage(QString(), S("zh")), S("zh"));
	LG_EQUAL(EffectiveLanguage(S("  "), S("zh")), S("zh"));
	LG_CHECK(EffectiveLanguage(QString(), S("")).isEmpty());
}

LUCKGRAM_TEST(TranslateHeaderAndMenuLabels) {
	LG_EQUAL(Header(S("英语"), S("中文"), true), S("已翻译 · 英语 → 中文"));
	LG_EQUAL(Header(QString(), S("中文"), true), S("已翻译 · 中文"));
	LG_EQUAL(
		Header(S("English"), S("Chinese"), false),
		S("Translated · English → Chinese"));
	LG_EQUAL(MenuLabel(false, true), S("翻译"));
	LG_EQUAL(MenuLabel(true, true), S("撤销翻译"));
	LG_EQUAL(MenuLabel(true, false), S("Undo translation"));
	LG_EQUAL(LanguageButton(S("日本語")), S("🌐 日本語 ▾"));
}

LUCKGRAM_TEST(TranslateLogsTelegramErrorTimeoutOrEmpty) {
	LG_CHECK(ShouldFallback(false));
	LG_CHECK(!ShouldFallback(true));
	LG_EQUAL(
		TelegramFailure(S("TRANSLATIONS_DISABLED_ALT"), false),
		S("TRANSLATIONS_DISABLED_ALT"));
	LG_EQUAL(TelegramFailure(S(" FLOOD_WAIT_12 "), false), S("FLOOD_WAIT_12"));
	LG_EQUAL(TelegramFailure(QString(), true), S(kTimeout));
	LG_EQUAL(TelegramFailure(S("X"), true), S(kTimeout));
	LG_EQUAL(TelegramFailure(QString(), false), S(kEmptyResult));
}

LUCKGRAM_TEST(TranslateAiLanguageIsIso639) {
	LG_EQUAL(AiLanguage(S("en")), S("en"));
	LG_EQUAL(AiLanguage(S("en_US")), S("en"));
	LG_EQUAL(AiLanguage(S("DE-de")), S("de"));
	LG_EQUAL(AiLanguage(S("zh")), S("zh"));
	LG_EQUAL(AiLanguage(S("zh_CN")), S("zh"));
	LG_EQUAL(AiLanguage(S("zh-hans")), S("zh"));
	LG_EQUAL(AiLanguage(S("zh_TW")), S("zh-Hant"));
	LG_EQUAL(AiLanguage(S("zh-Hant")), S("zh-Hant"));
	LG_EQUAL(AiLanguage(S("zh-HK")), S("zh-Hant"));
	LG_EQUAL(AiLanguage(S("pt_br")), S("pt-BR"));
	LG_EQUAL(AiLanguage(S("pt-PT")), S("pt"));
	LG_EQUAL(AiLanguage(S("nb")), S("no"));
	LG_EQUAL(AiLanguage(S("iw")), S("he"));
	LG_EQUAL(AiLanguage(S("in")), S("id"));
	LG_EQUAL(AiLanguage(S("fil")), S("fil"));
	LG_CHECK(AiLanguage(QString()).isEmpty());
	LG_CHECK(AiLanguage(S("")).isEmpty());
	LG_CHECK(AiLanguage(S("und")).isEmpty());
	LG_CHECK(AiLanguage(S("x")).isEmpty());
	LG_CHECK(AiLanguage(S("english")).isEmpty());
	LG_CHECK(AiLanguage(S("e1")).isEmpty());
}

LUCKGRAM_TEST(TranslateFailureKindAndMessages) {
	LG_EQUAL(FailureKind(S("network_error")), Failure::Network);
	LG_EQUAL(FailureKind(S("quota_exceeded")), Failure::Quota);
	LG_EQUAL(FailureKind(S("input_too_large")), Failure::TooLong);
	LG_EQUAL(FailureKind(S("consent_denied")), Failure::Consent);
	LG_EQUAL(FailureKind(S("upstream_unavailable")), Failure::Other);
	LG_EQUAL(FailureKind(QString()), Failure::Other);
	LG_EQUAL(
		FailureMessage(Failure::Network, true),
		S("网络不通，请检查网络或代理"));
	LG_EQUAL(
		FailureMessage(Failure::Quota, true),
		S("今日翻译次数已用完，明天再来"));
	LG_EQUAL(
		FailureMessage(Failure::TooLong, true),
		S("这段文字太长，无法翻译"));
	LG_EQUAL(FailureMessage(Failure::Other, true), S("翻译失败，请稍后再试"));
	LG_EQUAL(
		FailureMessage(Failure::Consent, true),
		FailureMessage(Failure::Other, true));
	LG_CHECK(FailureMessage(Failure::Network, false)
		!= FailureMessage(Failure::Other, false));
}

LUCKGRAM_TEST(TranslateCacheKeyHidesText) {
	const auto k = Cache::Key(S("zh"), S("Hello"));
	LG_EQUAL(k, Cache::Key(S("zh"), S("Hello")));
	LG_CHECK(k != Cache::Key(S("en"), S("Hello")));
	LG_CHECK(k != Cache::Key(S("zh"), S("Hello!")));
	LG_EQUAL(k.size(), 64);
	LG_CHECK(!k.contains(S("Hello")));
}

LUCKGRAM_TEST(TranslateCacheHitWithinSevenDaysMissAfter) {
	auto c = Cache();
	const auto k = Cache::Key(S("zh"), S("Hello"));
	c.put(k, S("你好"), S("en"), 1000);
	const auto hit = c.get(k, 1000 + 6 * kDay);
	LG_CHECK(hit.has_value());
	LG_EQUAL(hit->text, S("你好"));
	LG_EQUAL(hit->fromLang, S("en"));
	LG_CHECK(!c.get(k, 1000 + 7 * kDay + 1).has_value());
	LG_EQUAL(c.size(), 0);
}

LUCKGRAM_TEST(TranslateCacheDropsLeastRecentlyUsed) {
	auto c = Cache(3, Cache::kMaxAgeMs);
	c.put(S("a"), S("A"), QString(), 1);
	c.put(S("b"), S("B"), QString(), 2);
	c.put(S("c"), S("C"), QString(), 3);
	LG_CHECK(c.get(S("a"), 4).has_value()); // a is now most recently used
	c.put(S("d"), S("D"), QString(), 5);
	LG_EQUAL(c.size(), 3);
	LG_CHECK(!c.get(S("b"), 6).has_value());
	LG_CHECK(c.get(S("a"), 6).has_value());
	LG_CHECK(c.get(S("c"), 6).has_value());
	LG_CHECK(c.get(S("d"), 6).has_value());
}

LUCKGRAM_TEST(TranslateCacheDefaultLimitIs2000) {
	auto c = Cache();
	for (auto i = 0; i < 2100; ++i) {
		c.put(
			QStringLiteral("k") + QString::number(i),
			QStringLiteral("v") + QString::number(i),
			QString(),
			i);
	}
	LG_EQUAL(c.size(), Cache::kMaxEntries);
	LG_CHECK(!c.get(S("k0"), 3000).has_value());
	LG_CHECK(c.get(S("k2099"), 3000).has_value());
}

LUCKGRAM_TEST(TranslateCacheIgnoresEmptyResults) {
	auto c = Cache();
	c.put(S("k"), S(""), QString(), 1);
	c.put(S("k2"), QString(), QString(), 1);
	LG_EQUAL(c.size(), 0);
}

LUCKGRAM_TEST(TranslateCacheSerializeRoundTripSkipsExpired) {
	auto c = Cache();
	c.put(S("old"), S("旧"), QString(), 0);
	c.put(S("new"), S("新"), S("en"), 8 * kDay);
	auto r = Cache::Parse(c.serialize(), 8 * kDay + 1);
	LG_CHECK(!r.get(S("old"), 8 * kDay + 1).has_value());
	const auto hit = r.get(S("new"), 8 * kDay + 1);
	LG_CHECK(hit.has_value());
	LG_EQUAL(hit->text, S("新"));
	LG_EQUAL(hit->fromLang, S("en"));
	LG_EQUAL(Cache::Parse("not json", 0).size(), 0);
	LG_EQUAL(Cache::Parse(QByteArray(), 0).size(), 0);
}

LUCKGRAM_TEST(TranslateStateKeyChangesWithEditedText) {
	LG_EQUAL(StateKey(1, -100, 42, S("abc")), S("1:-100:42:abc"));
	LG_CHECK(StateKey(1, -100, 42, S("abc")) != StateKey(1, -100, 42, S("abd")));
	LG_CHECK(StateKey(0, -100, 42, S("abc")) != StateKey(1, -100, 42, S("abc")));
}

LUCKGRAM_TEST(TranslateLanguagesRoundTrip) {
	const auto both = DecodeLanguages(EncodeLanguages(S("en"), S("zh")));
	LG_EQUAL(both.from, S("en"));
	LG_EQUAL(both.to, S("zh"));
	const auto toOnly = DecodeLanguages(EncodeLanguages(QString(), S("zh")));
	LG_CHECK(toOnly.from.isEmpty());
	LG_EQUAL(toOnly.to, S("zh"));
	const auto none = DecodeLanguages(QString());
	LG_CHECK(none.from.isEmpty() && none.to.isEmpty());
}

LUCKGRAM_TEST(TranslateCacheRemove) {
	auto c = Cache();
	c.put(S("a"), S("A"), QString(), 1);
	c.put(S("b"), S("B"), QString(), 1);
	c.remove(S("a"));
	c.remove(S("missing"));
	LG_EQUAL(c.size(), 1);
	LG_CHECK(!c.get(S("a"), 2).has_value());
	LG_CHECK(c.get(S("b"), 2).has_value());
}

// specs#105: the 「🌐 中文 ▾」 link, recent languages, chat or default.

LUCKGRAM_TEST(TranslateLanguageLinkRoundTrip) {
	const auto link = LanguageLink(777000, 42);
	LG_EQUAL(link, S("internal:luckgram_translate_lang:777000:42"));
	const auto parsed = ParseLanguageLink(link.mid(9)); // After "internal:".
	LG_CHECK(parsed.has_value());
	LG_EQUAL(parsed->peerId, int64_t(777000));
	LG_EQUAL(parsed->msgId, int64_t(42));
}

LUCKGRAM_TEST(TranslateLanguageLinkRejectsGarbage) {
	LG_CHECK(!ParseLanguageLink(S("luckgram_translate_lang:")));
	LG_CHECK(!ParseLanguageLink(S("luckgram_translate_lang:1")));
	LG_CHECK(!ParseLanguageLink(S("luckgram_translate_lang:a:2")));
	LG_CHECK(!ParseLanguageLink(S("luckgram_translate_lang:0:2")));
	LG_CHECK(!ParseLanguageLink(S("luckgram_translate_lang:1:0")));
	LG_CHECK(!ParseLanguageLink(S("luckgram_translate_lang:1:2:3")));
	LG_CHECK(!ParseLanguageLink(S("other:1:2")));
}

LUCKGRAM_TEST(TranslateRecentNewestFirst) {
	LG_EQUAL(PushRecent(QStringList(), S("ja")), L({ "ja" }));
	LG_EQUAL(PushRecent(L({ "en", "ja" }), S("ja")), L({ "ja", "en" }));
	LG_EQUAL(PushRecent(L({ "en" }), S("  ")), L({ "en" }));
	LG_EQUAL(
		PushRecent(L({ "a", "b", "c", "d", "e" }), S("f")),
		L({ "f", "a", "b", "c", "d" }));
}

LUCKGRAM_TEST(TranslateScopeChatOrDefault) {
	LG_EQUAL(ChatOverrideAfter(Scope::ThisChat, S("ja")), S("ja"));
	LG_CHECK(ChatOverrideAfter(Scope::Default, S("ja")).isEmpty());
	// Default chosen: the chat follows it, effective language is the new
	// default.
	LG_EQUAL(
		EffectiveLanguage(ChatOverrideAfter(Scope::Default, S("ja")), S("ja")),
		S("ja"));
	LG_EQUAL(
		EffectiveLanguage(ChatOverrideAfter(Scope::ThisChat, S("ja")), S("en")),
		S("ja"));
}
