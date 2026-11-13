/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_logout_logic.h"

namespace Luckgram::Logout {

bool ClearsDeviceData(Event event, int sessionsLeft) {
	return (event == Event::AccountLoggedOut) && (sessionsLeft <= 0);
}

} // namespace Luckgram::Logout
