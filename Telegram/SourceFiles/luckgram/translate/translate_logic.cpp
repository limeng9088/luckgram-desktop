/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/translate/translate_logic.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Luckgram::Translate {
namespace {

[[nodiscard]] QString Normalized(const QString &language) {
	return language.trimmed().toLower().replace(QChar('_'), QChar('-'));
}

[[nodiscard]] bool IsLowerAscii(const QString &value) {
	for (const auto ch : value) {
		if (ch < QChar('a') || ch > QChar('z')) {
			return false;
		}
	}
	return true;
}

} // namespace

QString TelegramFailure(const QString &errorText, bool timedOut) {
	if (timedOut) {
		return QString::fromLatin1(kTimeout);
	}
	const auto trimmed = errorText.trimmed();
	return trimmed.isEmpty() ? QString::fromLatin1(kEmptyResult) : trimmed;
}

QString AiLanguage(const QString &language) {
	const auto s = Normalized(language);
	if (s.isEmpty() || s == QStringLiteral("und")) {
		return QString();
	}
	const auto dash = s.indexOf(QChar('-'));
	const auto base = (dash > 0) ? s.left(dash) : s;
	const auto rest = (dash > 0) ? s.mid(dash + 1) : QString();
	if (base.size() < 2 || base.size() > 3 || !IsLowerAscii(base)) {
		return QString();
	}
	if (base == QStringLiteral("zh")) {
		const auto hant = (rest == QStringLiteral("hant"))
			|| rest.startsWith(QStringLiteral("hant-"))
			|| (rest == QStringLiteral("tw"))
			|| (rest == QStringLiteral("hk"))
			|| (rest == QStringLiteral("mo"));
		return hant ? QStringLiteral("zh-Hant") : QStringLiteral("zh");
	} else if (base == QStringLiteral("pt")) {
		return (rest == QStringLiteral("br"))
			? QStringLiteral("pt-BR")
			: QStringLiteral("pt");
	} else if (base == QStringLiteral("nb") || base == QStringLiteral("nn")) {
		return QStringLiteral("no");
	} else if (base == QStringLiteral("iw")) {
		return QStringLiteral("he");
	} else if (base == QStringLiteral("in")) {
		return QStringLiteral("id");
	}
	return base;
}

QString Base(const QString &language) {
	auto s = Normalized(language);
	if (s.isEmpty() || s == QStringLiteral("und")) {
		return QString();
	}
	const auto dash = s.indexOf(QChar('-'));
	if (dash > 0) {
		s = s.left(dash);
	}
	if (s == QStringLiteral("nb") || s == QStringLiteral("nn")) {
		return QStringLiteral("no");
	}
	return s;
}

bool IsExcluded(const QString &language, const QStringList &doNotTranslate) {
	const auto base = Base(language);
	if (base.isEmpty()) {
		return false;
	}
	for (const auto &item : doNotTranslate) {
		if (base == Base(item)) {
			return true;
		}
	}
	return false;
}

bool ShowButton(
		bool featureOn,
		bool outgoing,
		bool hasText,
		const QString &detected,
		const QStringList &doNotTranslate,
		const QString &to) {
	if (!featureOn || outgoing || !hasText) {
		return false;
	}
	const auto base = Base(detected);
	if (base.isEmpty() || base == Base(to)) {
		return false;
	}
	return !IsExcluded(base, doNotTranslate);
}

QString TargetLanguage(const QString &chosen) {
	return chosen.trimmed();
}

QString EffectiveLanguage(
		const QString &chatChosen,
		const QString &defaultChosen) {
	const auto chat = TargetLanguage(chatChosen);
	return chat.isEmpty() ? TargetLanguage(defaultChosen) : chat;
}

QString LanguageButton(const QString &toName) {
	return QString::fromUtf8("🌐 ")
		+ toName
		+ QString::fromUtf8(" ▾");
}

QString LanguageLink(int64_t peerId, int64_t msgId) {
	return QString::fromLatin1("internal:")
		+ QString::fromLatin1(kLanguageLinkPrefix)
		+ QString::number(peerId)
		+ QChar(':')
		+ QString::number(msgId);
}

std::optional<LanguageLinkTarget> ParseLanguageLink(const QString &link) {
	const auto prefix = QString::fromLatin1(kLanguageLinkPrefix);
	if (!link.startsWith(prefix)) {
		return std::nullopt;
	}
	const auto parts = link.mid(prefix.size()).split(QChar(':'));
	if (parts.size() != 2) {
		return std::nullopt;
	}
	auto peerOk = false;
	auto msgOk = false;
	const auto peerId = parts[0].toLongLong(&peerOk);
	const auto msgId = parts[1].toLongLong(&msgOk);
	if (!peerOk || !msgOk || peerId <= 0 || msgId == 0) {
		return std::nullopt;
	}
	return LanguageLinkTarget{ .peerId = peerId, .msgId = msgId };
}

QByteArray EncodeChatLanguages(const std::map<int64_t, QString> &languages) {
	auto object = QJsonObject();
	for (const auto &[peerId, language] : languages) {
		const auto code = language.trimmed();
		if (peerId > 0 && !code.isEmpty()) {
			object.insert(QString::number(peerId), code);
		}
	}
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::map<int64_t, QString> DecodeChatLanguages(const QByteArray &data) {
	auto result = std::map<int64_t, QString>();
	const auto document = QJsonDocument::fromJson(data);
	if (!document.isObject()) {
		return result;
	}
	const auto object = document.object();
	for (auto i = object.begin(); i != object.end(); ++i) {
		auto ok = false;
		const auto peerId = i.key().toLongLong(&ok);
		const auto code = i.value().toString().trimmed();
		if (ok && peerId > 0 && !code.isEmpty()) {
			result.emplace(peerId, code);
		}
	}
	return result;
}

std::map<int64_t, QString> MergeChatLanguages(
		std::map<int64_t, QString> saved,
		const std::map<int64_t, QString> &memory,
		const std::set<int64_t> &removed) {
	for (const auto peerId : removed) {
		saved.erase(peerId);
	}
	for (const auto &[peerId, language] : memory) {
		saved[peerId] = language;
	}
	return saved;
}

std::optional<int64_t> LegacyChatPeerId(
		const QString &key,
		const QString &prefix) {
	if (prefix.isEmpty() || !key.startsWith(prefix)) {
		return std::nullopt;
	}
	auto ok = false;
	const auto peerId = key.mid(prefix.size()).toLongLong(&ok);
	return (ok && peerId > 0) ? std::make_optional(peerId) : std::nullopt;
}

QStringList PushRecent(const QStringList &recent, const QString &language) {
	const auto code = language.trimmed();
	auto result = QStringList();
	if (!code.isEmpty()) {
		result.push_back(code);
	}
	for (const auto &item : recent) {
		if (result.size() >= kMaxRecent) {
			break;
		}
		const auto trimmed = item.trimmed();
		if (!trimmed.isEmpty() && !result.contains(trimmed)) {
			result.push_back(trimmed);
		}
	}
	return result;
}

QString ChatOverrideAfter(Scope scope, const QString &language) {
	return (scope == Scope::ThisChat) ? language.trimmed() : QString();
}

QString Header(const QString &fromName, const QString &toName, bool chinese) {
	const auto arrow = QString::fromUtf8(" → ");
	const auto prefix = chinese
		? QString::fromUtf8("已翻译 · ")
		: QString::fromUtf8("Translated · ");
	return fromName.isEmpty()
		? (prefix + toName)
		: (prefix + fromName + arrow + toName);
}

QString MenuLabel(bool translated, bool chinese) {
	if (translated) {
		return chinese
			? QString::fromUtf8("撤销翻译")
			: QStringLiteral("Undo translation");
	}
	return chinese
		? QString::fromUtf8("翻译")
		: QStringLiteral("Translate");
}

QString StateKey(
		uint64_t account,
		int64_t peer,
		int64_t msgId,
		const QString &textKey) {
	return QString::number(account)
		+ QChar(':') + QString::number(peer)
		+ QChar(':') + QString::number(msgId)
		+ QChar(':') + textKey;
}

QString EncodeLanguages(const QString &from, const QString &to) {
	return from + QChar('>') + to;
}

Languages DecodeLanguages(const QString &value) {
	const auto gt = value.indexOf(QChar('>'));
	return (gt < 0)
		? Languages{ value, QString() }
		: Languages{ value.left(gt), value.mid(gt + 1) };
}

Failure FailureKind(const QString &aiErrorCode) {
	if (aiErrorCode == QStringLiteral("network_error")) {
		return Failure::Network;
	} else if (aiErrorCode == QStringLiteral("quota_exceeded")) {
		return Failure::Quota;
	} else if (aiErrorCode == QStringLiteral("input_too_large")) {
		return Failure::TooLong;
	} else if (aiErrorCode == QStringLiteral("consent_denied")) {
		return Failure::Consent;
	}
	return Failure::Other;
}

QString FailureMessage(Failure kind, bool chinese) {
	switch (kind) {
	case Failure::Network:
		return chinese
			? QString::fromUtf8("网络不通，请检查网络或代理")
			: QStringLiteral("No connection. Please check your network or proxy.");
	case Failure::Quota:
		return chinese
			? QString::fromUtf8("今日翻译次数已用完，明天再来")
			: QStringLiteral("You've used all of today's translations. Please come back tomorrow.");
	case Failure::TooLong:
		return chinese
			? QString::fromUtf8("这段文字太长，无法翻译")
			: QStringLiteral("This text is too long to translate.");
	case Failure::Other:
	case Failure::Consent:
		break;
	}
	return chinese
		? QString::fromUtf8("翻译失败，请稍后再试")
		: QStringLiteral("Translation failed. Please try again later.");
}

Cache::Cache(int maxEntries, int64_t maxAgeMs)
: _maxEntries(maxEntries)
, _maxAgeMs(maxAgeMs) {
}

QString Cache::Key(const QString &to, const QString &text) {
	auto raw = to.toUtf8();
	raw.append('\0');
	raw.append(text.toUtf8());
	return QString::fromLatin1(
		QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex());
}

std::optional<Cache::Entry> Cache::get(const QString &key, int64_t nowMs) {
	const auto i = _index.find(key);
	if (i == _index.end()) {
		return std::nullopt;
	}
	const auto time = i->second->entry.timeMs;
	if (nowMs - time > _maxAgeMs || nowMs < time - _maxAgeMs) {
		_list.erase(i->second);
		_index.erase(i);
		return std::nullopt;
	}
	_list.splice(_list.end(), _list, i->second);
	return i->second->entry;
}

void Cache::put(
		const QString &key,
		const QString &text,
		const QString &fromLang,
		int64_t nowMs) {
	if (key.isEmpty() || text.isEmpty()) {
		return;
	}
	if (const auto i = _index.find(key); i != _index.end()) {
		_list.erase(i->second);
		_index.erase(i);
	}
	_list.push_back({ key, { text, fromLang, nowMs } });
	_index.emplace(key, std::prev(_list.end()));
	while (int(_list.size()) > _maxEntries) {
		_index.erase(_list.front().key);
		_list.pop_front();
	}
}

void Cache::remove(const QString &key) {
	if (const auto i = _index.find(key); i != _index.end()) {
		_list.erase(i->second);
		_index.erase(i);
	}
}

int Cache::size() const {
	return int(_list.size());
}

bool Cache::prune(int64_t nowMs) {
	auto removed = false;
	for (auto i = _list.begin(); i != _list.end();) {
		if (nowMs - i->entry.timeMs > _maxAgeMs) {
			_index.erase(i->key);
			i = _list.erase(i);
			removed = true;
		} else {
			++i;
		}
	}
	return removed;
}

QByteArray Cache::serialize() const {
	auto array = QJsonArray();
	for (const auto &item : _list) {
		auto object = QJsonObject();
		object.insert(QStringLiteral("k"), item.key);
		object.insert(QStringLiteral("t"), item.entry.text);
		if (!item.entry.fromLang.isEmpty()) {
			object.insert(QStringLiteral("f"), item.entry.fromLang);
		}
		object.insert(QStringLiteral("d"), double(item.entry.timeMs));
		array.push_back(object);
	}
	return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

Cache Cache::Parse(const QByteArray &json, int64_t nowMs) {
	auto result = Cache();
	if (json.isEmpty()) {
		return result;
	}
	const auto document = QJsonDocument::fromJson(json);
	if (!document.isArray()) {
		return result;
	}
	for (const auto value : document.array()) {
		const auto object = value.toObject();
		const auto key = object.value(QStringLiteral("k")).toString();
		const auto text = object.value(QStringLiteral("t")).toString();
		const auto time = int64_t(object.value(QStringLiteral("d")).toDouble());
		if (key.isEmpty() || text.isEmpty() || nowMs - time > result._maxAgeMs) {
			continue;
		}
		result.put(
			key,
			text,
			object.value(QStringLiteral("f")).toString(),
			time);
	}
	return result;
}

} // namespace Luckgram::Translate
