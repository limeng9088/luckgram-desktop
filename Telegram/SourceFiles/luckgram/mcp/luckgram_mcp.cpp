/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/mcp/luckgram_mcp.h"

#include "base/random.h"
#include "core/application.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "luckgram/luckgram_double_bottom.h"
#include "luckgram/luckgram_secure_store.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/mcp/luckgram_mcp_logic.h"
#include "luckgram/mcp/luckgram_mcp_server.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"

#include <QtCore/QCoreApplication>

namespace Luckgram::Mcp {
namespace {

constexpr auto kTokenName = "mcp_token";
constexpr auto kPortSetting = "local_mcp_port";

struct ServerState {
	std::unique_ptr<Server> server;
	QString token;
	QString error;
	bool started = false;
	rpl::event_stream<> changes;
	rpl::lifetime lifetime;
};

[[nodiscard]] ServerState &State() {
	static auto result = ServerState();
	return result;
}

[[nodiscard]] QString NewToken() {
	auto bytes = QByteArray();
	for (auto i = 0; i != 4; ++i) {
		const auto part = base::RandomValue<uint64>();
		bytes.append(reinterpret_cast<const char*>(&part), sizeof(part));
	}
	return QString::fromLatin1(bytes.toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

[[nodiscard]] std::vector<not_null<Main::Account*>> ShownAccounts() {
	auto result = std::vector<not_null<Main::Account*>>();
	for (const auto account : Core::App().domain().orderedAccounts()) {
		if (account->sessionExists()
			&& !DoubleBottom::IsAccountHidden(account.get())) {
			result.push_back(account);
		}
	}
	return result;
}

[[nodiscard]] Backend MakeBackend() {
	auto result = Backend();
	result.locked = [] {
		return Core::App().passcodeLocked()
			|| !Core::App().domain().started();
	};
	result.accounts = [] {
		auto list = std::vector<AccountInfo>();
		const auto &domain = Core::App().domain();
		const auto active = &domain.active();
		for (const auto account : ShownAccounts()) {
			const auto session = &account->session();
			const auto user = session->user();
			auto info = AccountInfo();
			info.id = quint64(session->userId().bare);
			info.index = int(list.size()) + 1;
			info.name = user->name();
			info.username = user->username();
			info.phone = user->phone();
			info.unread = session->data().unreadBadge();
			info.unreadMuted = session->data().unreadBadgeMuted();
			info.premium = user->isPremium();
			info.active = (account.get() == active);
			list.push_back(std::move(info));
		}
		return list;
	};
	result.activate = [](quint64 id) {
		for (const auto account : ShownAccounts()) {
			if (quint64(account->session().userId().bare) == id) {
				// The switcher's own path: asks first when leaving the
				// current account would drop something (an ongoing call).
				Core::App().domain().maybeActivate(account);
				return QString();
			}
		}
		return QString::fromLatin1(
			"No such account is logged in. Call list_accounts to see the "
			"accounts.");
	};
	return result;
}

void Stop() {
	auto &state = State();
	if (!state.server && state.error.isEmpty()) {
		return;
	}
	state.server = nullptr;
	state.error = QString();
	state.changes.fire({});
}

void Listen() {
	auto &state = State();
	if (state.server) {
		return;
	}
	(void)Token(); // Created before the first request, not during it.
	auto server = std::make_unique<Server>(MakeBackend(), [] {
		return Token();
	});
	const auto wanted = int(Settings::GetInt(
		QString::fromLatin1(kPortSetting),
		kDefaultPort));
	const auto first = (wanted > 1023 && wanted < 65536 - kPortAttempts)
		? wanted
		: int(kDefaultPort);
	// A second copy of the app (another working folder) takes the next one.
	if (!server->listen(first, kPortAttempts)) {
		state.error = Tr(
			"端口被占用，无法启动",
			"Could not start: the ports are in use")
			+ u" ("_q
			+ QString::number(first)
			+ u"-"_q
			+ QString::number(first + kPortAttempts - 1)
			+ u")"_q;
		LOG(("Luckgram MCP Error: could not listen on 127.0.0.1:%1-%2: %3"
			).arg(first
			).arg(first + kPortAttempts - 1
			).arg(server->errorString()));
		state.changes.fire({});
		return;
	}
	state.error = QString();
	state.server = std::move(server);
	LOG(("Luckgram MCP: listening on 127.0.0.1:%1").arg(Port()));
	state.changes.fire({});
}

} // namespace

void Start() {
	auto &state = State();
	if (state.started) {
		return;
	}
	state.started = true;
	Settings::EnabledValue(
		QString::fromLatin1(kFeatureKey)
	) | rpl::on_next([](bool enabled) {
		if (enabled) {
			Listen();
		} else {
			Stop();
		}
	}, state.lifetime);
	// The server holds sockets: gone before the application object is.
	QObject::connect(
		QCoreApplication::instance(),
		&QCoreApplication::aboutToQuit,
		[] {
			auto &state = State();
			state.lifetime.destroy();
			state.server = nullptr;
		});
}

bool Running() {
	const auto &state = State();
	return state.server && state.server->port();
}

int Port() {
	const auto &state = State();
	return state.server ? state.server->port() : 0;
}

QString LastError() {
	return State().error;
}

rpl::producer<> StateChanges() {
	return State().changes.events();
}

QString Token() {
	auto &state = State();
	if (!IsValidToken(state.token)) {
		state.token = SecureStore::Get(kTokenName);
	}
	if (!IsValidToken(state.token)) {
		state.token = NewToken();
		SecureStore::Set(kTokenName, state.token);
	}
	return state.token;
}

void RegenerateToken() {
	auto &state = State();
	state.token = NewToken();
	SecureStore::Set(kTokenName, state.token);
	LOG(("Luckgram MCP: access token regenerated."));
	state.changes.fire({});
}

} // namespace Luckgram::Mcp
