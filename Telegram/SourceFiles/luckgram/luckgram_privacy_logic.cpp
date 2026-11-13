/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_privacy_logic.h"

#include <QtCore/QStringList>

#include <algorithm>

namespace Luckgram::Privacy {
namespace {

[[nodiscard]] QStringList Tokens(const QString &platform) {
	auto result = QStringList();
	for (const auto &part : platform.toLower().split(QChar('-'))) {
		const auto token = part.trimmed();
		if (!token.isEmpty()) {
			result.push_back(token);
		}
	}
	return result;
}

void Normalize(std::vector<quint64> &ids) {
	ids.erase(std::remove(ids.begin(), ids.end(), quint64(0)), ids.end());
	std::sort(ids.begin(), ids.end());
	ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
}

[[nodiscard]] bool Contains(const std::vector<quint64> &ids, quint64 id) {
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

bool IsStoreOnly(const QString &platform) {
	const auto tokens = Tokens(platform);
	if (tokens.isEmpty()) {
		return false;
	}
	for (const auto &token : tokens) {
		if (token != QLatin1String("ios") && token != QLatin1String("android")) {
			return false; // "all" or something we don't know: keep it.
		}
	}
	return true;
}

bool Ignore(bool switchOn, const QString &platform) {
	return switchOn && IsStoreOnly(platform);
}

bool Applies(const QString &platform, bool switchOn) {
	const auto tokens = Tokens(platform);
	const auto relevant = tokens.contains(QLatin1String("all"))
		|| tokens.contains(QLatin1String("ios"))
		|| tokens.contains(QLatin1String("android"));
	return relevant && !Ignore(switchOn, platform);
}

QString FirstApplicable(
		const std::vector<Restriction> &list,
		bool switchOn) {
	for (const auto &entry : list) {
		if (Applies(entry.platform, switchOn)) {
			return entry.text;
		}
	}
	return QString();
}

QString SerializeIds(std::vector<quint64> ids) {
	Normalize(ids);
	auto parts = QStringList();
	parts.reserve(int(ids.size()));
	for (const auto id : ids) {
		parts.push_back(QString::number(id));
	}
	return parts.join(QChar(','));
}

std::vector<quint64> ParseIds(const QString &value) {
	auto result = std::vector<quint64>();
	for (const auto &part : value.split(QChar(','))) {
		auto ok = false;
		const auto id = part.trimmed().toULongLong(&ok);
		if (ok && id) {
			result.push_back(id);
		}
	}
	Normalize(result);
	return result;
}

std::optional<std::vector<quint64>> WithHidden(
		std::vector<quint64> ids,
		quint64 id,
		bool hidden) {
	Normalize(ids);
	if (!id || Contains(ids, id) == hidden) {
		return std::nullopt;
	}
	if (hidden) {
		ids.push_back(id);
	} else {
		ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
	}
	Normalize(ids);
	return ids;
}

bool SuppressHiddenNotification(
		bool featureOn,
		bool hidden,
		bool notifyHidden) {
	return featureOn && hidden && !notifyHidden;
}

bool NameMatches(
		const std::vector<QString> &queryWords,
		const std::vector<QString> &nameWords) {
	if (queryWords.empty()) {
		return false;
	}
	for (const auto &query : queryWords) {
		if (query.isEmpty()) {
			continue;
		}
		const auto found = std::any_of(
			nameWords.begin(),
			nameWords.end(),
			[&](const QString &word) { return word.startsWith(query); });
		if (!found) {
			return false;
		}
	}
	return true;
}

SecondPasscodeError CheckNewSecondPasscode(
		const QString &passcode,
		const QString &repeat,
		const QString &main) {
	if (passcode.isEmpty()) {
		return SecondPasscodeError::Empty;
	} else if (passcode != repeat) {
		return SecondPasscodeError::Mismatch;
	} else if (passcode == main) {
		return SecondPasscodeError::SameAsMain;
	}
	return SecondPasscodeError::None;
}

bool AccountHidden(
		bool secondSpace,
		quint64 userId,
		const std::vector<quint64> &visible) {
	return secondSpace && userId && !Contains(visible, userId);
}

std::optional<std::vector<quint64>> ToggleVisible(
		std::vector<quint64> visible,
		quint64 userId) {
	Normalize(visible);
	if (!userId) {
		return std::nullopt;
	} else if (Contains(visible, userId)) {
		if (visible.size() <= 1) {
			return std::nullopt;
		}
		visible.erase(
			std::remove(visible.begin(), visible.end(), userId),
			visible.end());
	} else {
		visible.push_back(userId);
		Normalize(visible);
	}
	return visible;
}

QString EncodeRecord(const SecondRecord &record) {
	return QStringList{
		QString::number(kRecordVersion),
		QString::number(record.iterations),
		QString::fromLatin1(record.salt.toBase64()),
		QString::fromLatin1(record.nonce.toBase64()),
		QString::fromLatin1(record.sealed.toBase64()),
		QString::fromLatin1(record.visibleNonce.toBase64()),
		QString::fromLatin1(record.visibleSealed.toBase64()),
	}.join(QChar(':'));
}

std::optional<SecondRecord> DecodeRecord(const QString &value) {
	const auto parts = value.split(QChar(':'));
	auto ok = false;
	const auto version = parts.isEmpty() ? 0 : parts[0].toInt(&ok);
	if (!ok
		|| !((version == 1 && parts.size() == 5)
			|| (version == kRecordVersion && parts.size() == 7))) {
		return std::nullopt;
	}
	const auto iterations = parts[1].toInt(&ok);
	if (!ok || iterations <= 0) {
		return std::nullopt;
	}
	const auto decode = [](const QString &part) -> std::optional<QByteArray> {
		const auto result = QByteArray::fromBase64Encoding(
			part.toLatin1(),
			QByteArray::AbortOnBase64DecodingErrors);
		if (result.decodingStatus != QByteArray::Base64DecodingStatus::Ok) {
			return std::nullopt;
		}
		return result.decoded;
	};
	const auto salt = decode(parts[2]);
	const auto nonce = decode(parts[3]);
	const auto sealed = decode(parts[4]);
	if (!salt || !nonce || !sealed
		|| salt->size() != kSaltSize
		|| nonce->size() != kNonceSize
		|| sealed->size() <= kTagSize) {
		return std::nullopt;
	}
	auto result = SecondRecord{
		.salt = *salt,
		.nonce = *nonce,
		.sealed = *sealed,
		.iterations = iterations,
	};
	if (version == kRecordVersion
		&& !(parts[5].isEmpty() && parts[6].isEmpty())) {
		const auto visibleNonce = decode(parts[5]);
		const auto visibleSealed = decode(parts[6]);
		if (!visibleNonce || !visibleSealed
			|| visibleNonce->size() != kNonceSize
			|| visibleSealed->size() <= kTagSize) {
			return std::nullopt;
		}
		result.visibleNonce = *visibleNonce;
		result.visibleSealed = *visibleSealed;
	}
	return result;
}

} // namespace Luckgram::Privacy
