/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_logout_logic.h"

namespace Luckgram::Logout {

bool ClearsDeviceData(Event event, int sessionsLeft) {
	return (event == Event::AccountLoggedOut) && (sessionsLeft <= 0);
}

Cause ServerError(int code, const QString &type) {
	constexpr auto kMaxType = 64;
	auto clean = QString();
	for (const auto ch : type) {
		if (clean.size() == kMaxType) {
			break;
		}
		const auto c = ch.unicode();
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
			clean.append(ch);
		}
	}
	return { .reason = Reason::ServerError, .code = code, .type = clean };
}

QString Describe(const Cause &cause) {
	switch (cause.reason) {
	case Reason::User: return QStringLiteral("user");
	case Reason::ForgottenPasscode: return QStringLiteral("forgotten passcode");
	case Reason::AlreadyLoggedIn: return QStringLiteral("already logged in");
	case Reason::WrongSelf: return QStringLiteral("wrong self user");
	case Reason::ServerError:
		return QStringLiteral("server error %1 %2").arg(cause.code).arg(
			cause.type.isEmpty() ? QStringLiteral("(no type)") : cause.type);
	case Reason::ServerNotice: return QStringLiteral("server notice");
	case Reason::Unknown: break;
	}
	return QStringLiteral("unknown");
}

QString LogLine(int index, quint64 userId, const Cause &cause) {
	return QStringLiteral("Luckgram Account: logged out, account index %1, user %2, reason: %3")
		.arg(index)
		.arg(userId)
		.arg(Describe(cause));
}

} // namespace Luckgram::Logout
