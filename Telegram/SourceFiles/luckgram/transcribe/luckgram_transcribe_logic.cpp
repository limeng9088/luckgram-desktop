/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/transcribe/luckgram_transcribe_logic.h"

#include <QtCore/QStringList>

#include <algorithm>

namespace Luckgram::Transcribe {

bool UseLuckgram(bool featureOn, bool premium) {
	return featureOn && !premium;
}

bool TooLarge(int64_t bytes, double maxMb) {
	const auto limit = (maxMb > 0) ? maxMb : double(kDefaultMaxMb);
	return double(bytes) > limit * 1024. * 1024.;
}

QString UploadMime(bool roundVideo, const QString &mime) {
	if (roundVideo) {
		return QString::fromLatin1("audio/mp4");
	}
	const auto lower = mime.trimmed().toLower();
	if (lower.startsWith(QString::fromLatin1("audio/"))) {
		return lower;
	}
	return QString::fromLatin1("audio/ogg"); // Voice messages are OGG Opus.
}

QString CacheKey(int64_t peerId, int64_t msgId) {
	return QString::number(peerId) + QChar(':') + QString::number(msgId);
}

QString Link(LinkAction action, int64_t peerId, int64_t msgId) {
	return QString::fromLatin1("internal:")
		+ QString::fromLatin1(kLinkPrefix)
		+ QString::fromLatin1(
			(action == LinkAction::Retry) ? "retry:" : "translate:")
		+ QString::number(peerId)
		+ QChar(':')
		+ QString::number(msgId);
}

std::optional<LinkTarget> ParseLink(const QString &link) {
	const auto prefix = QString::fromLatin1(kLinkPrefix);
	if (!link.startsWith(prefix)) {
		return std::nullopt;
	}
	const auto parts = link.mid(prefix.size()).split(QChar(':'));
	if (parts.size() != 3) {
		return std::nullopt;
	}
	auto result = LinkTarget();
	if (parts[0] == QString::fromLatin1("retry")) {
		result.action = LinkAction::Retry;
	} else if (parts[0] == QString::fromLatin1("translate")) {
		result.action = LinkAction::Translate;
	} else {
		return std::nullopt;
	}
	auto peerOk = false;
	auto msgOk = false;
	result.peerId = parts[1].toLongLong(&peerOk);
	result.msgId = parts[2].toLongLong(&msgOk);
	if (!peerOk || !msgOk || result.peerId <= 0 || result.msgId == 0) {
		return std::nullopt;
	}
	return result;
}

bool TooShort(int64_t durationMs) {
	return durationMs < kMinRecordMs;
}

double LevelRatio(int level) {
	return (level <= 0) ? 0. : std::min(1., level / kMaxLevel);
}

QString TidyResult(const QString &raw, const QString &tidy) {
	const auto trimmed = tidy.trimmed();
	return trimmed.isEmpty() ? raw : trimmed;
}

QString FailedText(bool chinese) {
	return chinese
		? QString::fromUtf8("转写失败 · ")
		: QString::fromUtf8("Couldn't transcribe · ");
}

QString RetryLabel(bool chinese) {
	return chinese
		? QString::fromUtf8("重试")
		: QString::fromLatin1("Retry");
}

} // namespace Luckgram::Transcribe
