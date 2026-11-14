/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/mcp/luckgram_mcp.h"

#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/random.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_user.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
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
#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>

namespace Luckgram::Mcp {
namespace {

constexpr auto kTokenName = "mcp_token";
constexpr auto kPortSetting = "local_mcp_port";
constexpr auto kContactsTimeout = crl::time(15 * 1000);

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

[[nodiscard]] Main::Session *SessionOf(quint64 id) {
	for (const auto account : ShownAccounts()) {
		const auto session = &account->session();
		if (quint64(session->userId().bare) == id) {
			return session;
		}
	}
	return nullptr;
}

[[nodiscard]] ToolOutcome Failed(const char *text) {
	auto result = ToolOutcome();
	result.error = QString::fromLatin1(text);
	return result;
}

// Skill "friends".
[[nodiscard]] ToolOutcome FriendsOf(
		not_null<Main::Session*> session,
		const QJsonObject &arguments) {
	const auto query = arguments.value(u"query"_q).toString();
	const auto limit = arguments.value(u"limit"_q).toInt();
	const auto offset = arguments.value(u"offset"_q).toInt();
	auto total = 0;
	auto list = QJsonArray();
	for (const auto &row : session->data().contactsList()->all()) {
		const auto history = row->history();
		const auto user = history ? history->peer->asUser() : nullptr;
		if (!user || user->isSelf()) {
			continue;
		} else if (!query.isEmpty()
			&& !user->name().contains(query, Qt::CaseInsensitive)
			&& !user->username().contains(query, Qt::CaseInsensitive)) {
			continue;
		}
		const auto index = total++;
		if (index < offset || int(list.size()) >= limit) {
			continue;
		}
		auto entry = QJsonObject();
		entry.insert(u"id"_q, QString::number(quint64(peerToUser(user->id).bare)));
		entry.insert(u"name"_q, user->name());
		if (!user->username().isEmpty()) {
			entry.insert(u"username"_q, user->username());
		}
		if (!user->phone().isEmpty()) {
			entry.insert(u"phone"_q, user->phone());
		}
		entry.insert(u"bot"_q, user->isBot());
		entry.insert(
			u"mutual"_q,
			bool(user->flags() & UserDataFlag::MutualContact));
		list.push_back(entry);
	}
	auto result = ToolOutcome();
	result.value.insert(u"total"_q, total);
	result.value.insert(u"offset"_q, offset);
	result.value.insert(u"friends"_q, list);
	return result;
}

void ListFriends(
		not_null<Main::Session*> session,
		const QJsonObject &arguments,
		ToolDone done) {
	if (session->data().contactsLoaded().current()) {
		done(FriendsOf(session, arguments));
		return;
	}
	// Contacts come from the server once per launch; asked for now.
	struct Wait {
		ToolDone done;
		rpl::lifetime lifetime;
	};
	const auto wait = std::make_shared<Wait>();
	wait->done = std::move(done);
	const auto finish = [=](bool loaded) {
		if (const auto done = base::take(wait->done)) {
			done(loaded
				? FriendsOf(session, arguments)
				: Failed("The contacts did not load in time. Try again."));
		}
	};
	// The subscription holds the state weakly: the timeout below is what
	// keeps it alive, and what drops it when nothing came.
	const auto weak = std::weak_ptr<Wait>(wait);
	session->data().contactsLoaded().value(
	) | rpl::filter([](bool loaded) {
		return loaded;
	}) | rpl::take(1) | rpl::on_next([=] {
		if (const auto strong = weak.lock()) {
			if (const auto done = base::take(strong->done)) {
				done(FriendsOf(session, arguments));
			}
		}
	}, wait->lifetime);
	session->api().requestContacts();
	base::call_delayed(kContactsTimeout, session, [=] {
		finish(false);
	});
}

[[nodiscard]] QJsonObject UserJson(not_null<UserData*> user) {
	auto result = QJsonObject();
	result.insert(
		u"id"_q,
		QString::number(quint64(peerToUser(user->id).bare)));
	result.insert(u"name"_q, user->name());
	if (!user->username().isEmpty()) {
		result.insert(u"username"_q, user->username());
	}
	if (!user->phone().isEmpty()) {
		result.insert(u"phone"_q, user->phone());
	}
	result.insert(u"bot"_q, user->isBot());
	result.insert(u"friend"_q, user->isContact());
	return result;
}

// What Telegram said, in words an agent can act on.
[[nodiscard]] ToolOutcome ServerError(const MTP::Error &error) {
	const auto type = error.type();
	auto result = ToolOutcome();
	result.error = type.startsWith(u"FLOOD_WAIT_"_q)
		? (u"Telegram asks this account to wait "_q
			+ type.mid(11)
			+ u" seconds (FLOOD_WAIT). Do not retry sooner."_q)
		: (type == u"USERNAME_NOT_OCCUPIED"_q
			|| type == u"USERNAME_INVALID"_q)
		? u"No user has this username."_q
		: (type == u"PHONE_NOT_OCCUPIED"_q)
		? u"No user can be found by this phone number from this account."_q
		: (u"Telegram refused: "_q + type);
	return result;
}

// The user a friends tool names: by id among those the account has seen,
// by username or phone from the server.
void ResolveUser(
		not_null<Main::Session*> session,
		const QJsonObject &arguments,
		Fn<void(UserData *user, ToolOutcome failure)> done) {
	const auto owner = &session->data();
	const auto resolved = [=](const MTPcontacts_ResolvedPeer &result) {
		const auto &data = result.data();
		owner->processUsers(data.vusers());
		owner->processChats(data.vchats());
		const auto peerId = peerFromMTP(data.vpeer());
		const auto peer = peerId ? owner->peerLoaded(peerId) : nullptr;
		const auto user = peer ? peer->asUser() : nullptr;
		if (!user) {
			done(nullptr, Failed("This is not a user."));
		} else {
			done(user, ToolOutcome());
		}
	};
	const auto failed = [=](const MTP::Error &error) {
		done(nullptr, ServerError(error));
	};
	const auto id = arguments.value(u"user_id"_q).toString();
	const auto username = arguments.value(u"username"_q).toString();
	const auto phone = arguments.value(u"phone"_q).toString();
	if (!id.isEmpty()) {
		const auto user = owner->userLoaded(UserId(id.toULongLong()));
		if (!user) {
			done(nullptr, Failed(
				"This account has not seen a user with this id. Use the "
				"username or the phone instead."));
		} else {
			done(user, ToolOutcome());
		}
	} else if (!username.isEmpty()) {
		const auto known = owner->peerByUsername(username);
		if (const auto user = known ? known->asUser() : nullptr) {
			done(user, ToolOutcome());
			return;
		}
		session->api().request(MTPcontacts_ResolveUsername(
			MTP_flags(0),
			MTP_string(username),
			MTP_string()
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			resolved(result);
		}).fail([=](const MTP::Error &error) {
			failed(error);
		}).send();
	} else {
		session->api().request(MTPcontacts_ResolvePhone(
			MTP_string(phone)
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			resolved(result);
		}).fail([=](const MTP::Error &error) {
			failed(error);
		}).send();
	}
}

void FindUser(
		not_null<Main::Session*> session,
		const QJsonObject &arguments,
		ToolDone done) {
	ResolveUser(session, arguments, [=](UserData *user, ToolOutcome failure) {
		if (!user) {
			done(std::move(failure));
			return;
		}
		auto result = ToolOutcome();
		result.value.insert(u"user"_q, UserJson(user));
		done(std::move(result));
	});
}

// Skill "friends_manage".
void AddFriend(
		not_null<Main::Session*> session,
		const QJsonObject &arguments,
		ToolDone done) {
	ResolveUser(session, arguments, [=](UserData *user, ToolOutcome failure) {
		if (!user) {
			done(std::move(failure));
			return;
		} else if (user->isSelf()) {
			done(Failed("This is the account itself."));
			return;
		} else if (user->isContact()) {
			auto result = ToolOutcome();
			result.value.insert(u"added"_q, false);
			result.value.insert(u"already_friend"_q, true);
			result.value.insert(u"user"_q, UserJson(user));
			done(std::move(result));
			return;
		}
		const auto given = arguments.value(u"first_name"_q).toString();
		const auto first = !given.isEmpty()
			? given
			: !user->firstName.isEmpty()
			? user->firstName
			: user->name();
		const auto last = arguments.contains(u"last_name"_q)
			? arguments.value(u"last_name"_q).toString()
			: user->lastName;
		session->api().request(MTPcontacts_AddContact(
			MTP_flags(0),
			user->inputUser(),
			MTP_string(first),
			MTP_string(last),
			MTP_string(user->phone()),
			MTPTextWithEntities()
		)).done([=](const MTPUpdates &updates) {
			session->api().applyUpdates(updates);
			auto result = ToolOutcome();
			result.value.insert(u"added"_q, true);
			result.value.insert(u"already_friend"_q, false);
			result.value.insert(u"user"_q, UserJson(user));
			done(std::move(result));
		}).fail([=](const MTP::Error &error) {
			done(ServerError(error));
		}).send();
	});
}

void RemoveFriend(
		not_null<Main::Session*> session,
		const QJsonObject &arguments,
		ToolDone done) {
	ResolveUser(session, arguments, [=](UserData *user, ToolOutcome failure) {
		if (!user) {
			done(std::move(failure));
			return;
		} else if (!user->isContact()) {
			auto result = ToolOutcome();
			result.value.insert(u"removed"_q, false);
			result.value.insert(u"was_friend"_q, false);
			result.value.insert(u"user"_q, UserJson(user));
			done(std::move(result));
			return;
		}
		session->api().request(MTPcontacts_DeleteContacts(
			MTP_vector<MTPInputUser>(1, user->inputUser())
		)).done([=](const MTPUpdates &updates) {
			session->api().applyUpdates(updates);
			auto result = ToolOutcome();
			result.value.insert(u"removed"_q, true);
			result.value.insert(u"was_friend"_q, true);
			result.value.insert(u"user"_q, UserJson(user));
			done(std::move(result));
		}).fail([=](const MTP::Error &error) {
			done(ServerError(error));
		}).send();
	});
}

// Skill "chats".
[[nodiscard]] ToolOutcome ChatsOf(
		not_null<Main::Session*> session,
		const QJsonObject &arguments) {
	const auto unreadOnly = arguments.value(u"unread_only"_q).toBool();
	const auto limit = arguments.value(u"limit"_q).toInt();
	const auto offset = arguments.value(u"offset"_q).toInt();
	const auto chats = session->data().chatsList();
	auto total = 0;
	auto list = QJsonArray();
	for (const auto &row : chats->indexed()->all()) {
		const auto history = row->history();
		if (!history) {
			continue; // The archive row.
		}
		const auto peer = history->peer;
		const auto unread = history->unreadCount();
		const auto marked = history->unreadMark();
		if (unreadOnly && !unread && !marked) {
			continue;
		}
		const auto index = total++;
		if (index < offset || int(list.size()) >= limit) {
			continue;
		}
		const auto user = peer->asUser();
		auto entry = QJsonObject();
		entry.insert(u"id"_q, QString::number(quint64(peer->id.value)));
		entry.insert(u"type"_q, user
			? (user->isBot() ? u"bot"_q : u"user"_q)
			: peer->isBroadcast()
			? u"channel"_q
			: peer->isMegagroup()
			? u"supergroup"_q
			: u"group"_q);
		entry.insert(u"name"_q, peer->name());
		if (!peer->username().isEmpty()) {
			entry.insert(u"username"_q, peer->username());
		}
		entry.insert(u"unread"_q, unread);
		entry.insert(u"unread_mark"_q, marked);
		entry.insert(
			u"muted"_q,
			session->data().notifySettings().isMuted(peer));
		entry.insert(u"pinned"_q, row->entry()->isPinnedDialog(FilterId()));
		if (const auto item = history->chatListMessage()) {
			entry.insert(u"last_message_date"_q, double(item->date()));
		}
		list.push_back(entry);
	}
	auto result = ToolOutcome();
	result.value.insert(u"total"_q, total);
	result.value.insert(u"offset"_q, offset);
	// False while the app has loaded only the top of a long chat list.
	result.value.insert(u"complete"_q, chats->loaded());
	result.value.insert(u"chats"_q, list);
	return result;
}

[[nodiscard]] Backend MakeBackend() {
	auto result = Backend();
	result.skillEnabled = [](quint64 account, const QString &skill) {
		return SkillEnabled(account, skill);
	};
	result.nowMs = [] {
		return QDateTime::currentMSecsSinceEpoch();
	};
	result.loadTimes = [](const QString &key) {
		return ParseTimes(Settings::GetString(key));
	};
	result.saveTimes = [](
			const QString &key,
			const std::vector<qint64> &times) {
		if (times.empty()) {
			Settings::Remove(key);
		} else {
			Settings::SetString(key, SerializeTimes(times));
		}
	};
	result.call = [](
			const QString &tool,
			quint64 account,
			const QJsonObject &arguments,
			ToolDone done) {
		const auto session = SessionOf(account);
		if (!session) {
			done(Failed(
				"No such account is logged in. Call list_accounts to see "
				"the accounts."));
		} else if (tool == u"list_friends"_q) {
			ListFriends(session, arguments, std::move(done));
		} else if (tool == u"list_chats"_q) {
			done(ChatsOf(session, arguments));
		} else if (tool == u"find_user"_q) {
			FindUser(session, arguments, std::move(done));
		} else if (tool == u"add_friend"_q) {
			AddFriend(session, arguments, std::move(done));
		} else if (tool == u"remove_friend"_q) {
			RemoveFriend(session, arguments, std::move(done));
		} else {
			done(Failed("This tool is not available."));
		}
	};
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

std::vector<AccountRow> AccountRows() {
	auto result = std::vector<AccountRow>();
	if (Core::App().passcodeLocked() || !Core::App().domain().started()) {
		return result;
	}
	for (const auto account : ShownAccounts()) {
		const auto session = &account->session();
		result.push_back({
			.id = quint64(session->userId().bare),
			.name = session->user()->name(),
		});
	}
	return result;
}

bool SkillEnabled(quint64 account, const QString &skill) {
	return Settings::GetBool(SkillSettingKey(skill, account), false);
}

void SetSkillEnabled(quint64 account, const QString &skill, bool enabled) {
	const auto key = SkillSettingKey(skill, account);
	if (enabled) {
		Settings::SetBool(key, true);
	} else {
		Settings::Remove(key); // Off is the default: no key left behind.
	}
	LOG(("Luckgram MCP: skill %1 %2 for account %3."
		).arg(skill
		).arg(enabled ? u"on"_q : u"off"_q
		).arg(account));
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
