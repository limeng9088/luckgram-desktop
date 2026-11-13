/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// Spec 01 translation rules, pure (QtCore + std only, unit tested in
// luckgram_tests). Same rules and wording as mobile TranslateInLogic,
// TranslateFallbackLogic and TranslateCache.

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

namespace Luckgram::Translate {

// Feature switches (spec 01 「开关」).
inline constexpr auto kFeatureIn = "translate_in";
inline constexpr auto kFeatureOut = "translate_out";

// Telegram gets this long before we stop waiting and fall back.
inline constexpr auto kTelegramTimeoutMs = 8000;

// Diagnostic-log names, one per entry point.
inline constexpr auto kSourceOut = "translate_out";
inline constexpr auto kSourceMessage = "translate_message";
inline constexpr auto kSourceChat = "translate_chat";
inline constexpr auto kSourceVoice = "translate_voice";

// What the diagnostic log records for Telegram's failure.
inline constexpr auto kTimeout = "TIMEOUT";
inline constexpr auto kEmptyResult = "EMPTY_RESULT";
[[nodiscard]] QString TelegramFailure(const QString &errorText, bool timedOut);

// Every Telegram failure falls back, a usable result never does.
[[nodiscard]] inline bool ShouldFallback(bool telegramGaveText) {
	return !telegramGaveText;
}

// The `to_lang` sent to the server: ISO 639-1 (or 639-2), with the script
// or region only where it changes the written language (zh-Hant, pt-BR).
// Empty when unusable.
[[nodiscard]] QString AiLanguage(const QString &language);

// Lower case, no region, every zh-* is zh, nb/nn is no. Empty if unknown.
[[nodiscard]] QString Base(const QString &language);

[[nodiscard]] bool IsExcluded(
	const QString &language,
	const QStringList &doNotTranslate);

// A.2/A.3: the one-tap button shows next to someone else's message with
// text in a known language that is neither excluded nor the target.
[[nodiscard]] bool ShowButton(
	bool featureOn,
	bool outgoing,
	bool hasText,
	const QString &detected,
	const QStringList &doNotTranslate,
	const QString &to);

// specs#93: only the user's choice, never a default (first use asks).
[[nodiscard]] QString TargetLanguage(const QString &chosen);

// specs#105: the chat's own choice, else the default, else empty.
[[nodiscard]] QString EffectiveLanguage(
	const QString &chatChosen,
	const QString &defaultChosen);

// specs#105: 「🌐 中文 ▾」.
[[nodiscard]] QString LanguageButton(const QString &toName);

// specs#105: the 「🌐 中文 ▾」 link in a translation's header, opened by
// upstream's internal URL handlers. ParseLanguageLink takes the part after
// "internal:" (what the handler's regex sees).
inline constexpr auto kLanguageLinkPrefix = "luckgram_translate_lang:";
[[nodiscard]] QString LanguageLink(int64_t peerId, int64_t msgId);
struct LanguageLinkTarget {
	int64_t peerId = 0;
	int64_t msgId = 0;
};
[[nodiscard]] std::optional<LanguageLinkTarget> ParseLanguageLink(
	const QString &link);

// specs#105: each chat's own reading language, peer id -> language code.
// Kept in the account's encrypted storage (a list of peer ids is user
// content), as {"<peer id>":"ja",...}. Bad entries are dropped on read.
[[nodiscard]] QByteArray EncodeChatLanguages(
	const std::map<int64_t, QString> &languages);
[[nodiscard]] std::map<int64_t, QString> DecodeChatLanguages(
	const QByteArray &data);

// Loading the saved chat languages: choices made in memory before the
// load win, and chats reset to the default before the load stay reset
// (removed), instead of getting the saved value back.
[[nodiscard]] std::map<int64_t, QString> MergeChatLanguages(
	std::map<int64_t, QString> saved,
	const std::map<int64_t, QString> &memory,
	const std::set<int64_t> &removed);

// #25-#43 kept them as plain luckgram.json keys "<prefix><peer id>":
// the peer id of such a key, if it has the prefix and a valid id.
[[nodiscard]] std::optional<int64_t> LegacyChatPeerId(
	const QString &key,
	const QString &prefix);

// specs#105 3: 「最近使用」, newest first, no duplicates, at most kMaxRecent.
inline constexpr auto kMaxRecent = 5;
[[nodiscard]] QStringList PushRecent(
	const QStringList &recent,
	const QString &language);

// specs#105 2: changing the language inside a chat asks 「仅本聊天 / 设为
// 默认」. Which chat override remains after the choice: the new language
// for this chat only, or none (the chat follows the new default).
enum class Scope {
	ThisChat,
	Default,
};
[[nodiscard]] QString ChatOverrideAfter(Scope scope, const QString &language);

// A.1: 「已翻译 · 英语 → 中文」, just the target without a known source.
[[nodiscard]] QString Header(
	const QString &fromName,
	const QString &toName,
	bool chinese);

// Menu label: 「翻译」 or 「撤销翻译」.
[[nodiscard]] QString MenuLabel(bool translated, bool chinese);

// Key of a message's translation state: per account, chat, message and
// text, so an edited message loses its (stale) translation.
[[nodiscard]] QString StateKey(
	uint64_t account,
	int64_t peer,
	int64_t msgId,
	const QString &textKey);

// The "from>to" languages kept with a saved translation, and back.
[[nodiscard]] QString EncodeLanguages(const QString &from, const QString &to);
struct Languages {
	QString from;
	QString to;
};
[[nodiscard]] Languages DecodeLanguages(const QString &value);

enum class Failure {
	Other,
	Network,
	Quota,
	TooLong,
	Consent,
};

[[nodiscard]] Failure FailureKind(const QString &aiErrorCode);

// Declining the AI consent means "Telegram only": the generic message.
[[nodiscard]] QString FailureMessage(Failure kind, bool chinese);

// 「翻译服务」5: results keyed by (target language, text) hash, so the
// cache file holds translations but not the original texts. At most
// kMaxEntries, each kept kMaxAgeMs, least recently used evicted first.
class Cache final {
public:
	static constexpr auto kMaxEntries = 2000;
	static constexpr auto kMaxAgeMs = int64_t(7) * 24 * 60 * 60 * 1000;

	struct Entry {
		QString text;
		QString fromLang; // Empty when unknown.
		int64_t timeMs = 0;
	};

	explicit Cache(int maxEntries = kMaxEntries, int64_t maxAgeMs = kMaxAgeMs);
	// The index points into the list: moving keeps it valid, copying not.
	Cache(Cache &&other) = default;
	Cache &operator=(Cache &&other) = default;
	Cache(const Cache &other) = delete;
	Cache &operator=(const Cache &other) = delete;

	[[nodiscard]] static QString Key(const QString &to, const QString &text);

	[[nodiscard]] std::optional<Entry> get(const QString &key, int64_t nowMs);
	void put(
		const QString &key,
		const QString &text,
		const QString &fromLang,
		int64_t nowMs);
	void remove(const QString &key);
	[[nodiscard]] int size() const;
	bool prune(int64_t nowMs);

	// JSON array, least recently used first.
	[[nodiscard]] QByteArray serialize() const;
	[[nodiscard]] static Cache Parse(const QByteArray &json, int64_t nowMs);

private:
	struct Item {
		QString key;
		Entry entry;
	};
	using List = std::list<Item>;

	int _maxEntries = kMaxEntries;
	int64_t _maxAgeMs = kMaxAgeMs;
	List _list; // Most recently used at the back.
	std::unordered_map<QString, List::iterator> _index;

};

} // namespace Luckgram::Translate
