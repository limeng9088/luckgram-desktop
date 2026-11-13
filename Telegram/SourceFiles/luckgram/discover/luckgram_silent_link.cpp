/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/discover/luckgram_silent_link.h"

#include "apiwrap.h"
#include "base/call_delayed.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/pro/luckgram_pro_logic.h"
#include "main/main_session.h"
#include "window/notifications_manager.h"

#include <QtCore/QDateTime>
#include <QtCore/QUrl>

#include <array>
#include <limits>

namespace Luckgram::SilentLink {
namespace {

// The nonce lives 10 minutes; give up sooner.
constexpr auto kLinkTimeoutMs = qint64(90) * 1000;
// The bot reply may come after the link: delete the chat again then.
constexpr auto kCleanupDelaysMs = std::array<crl::time, 3>{ 2000, 6000, 15000 };
constexpr auto kWatchBotMs = crl::time(60 * 1000);

struct Shared {
	UserId user = 0;
	State state = State::Unknown;
	bool linking = false;
	std::vector<Fn<void(bool)>> waiting;
	Api::RequestPtr request;
	rpl::lifetime watchLifetime;
};

[[nodiscard]] Shared &Data() {
	static auto result = Shared();
	return result;
}

[[nodiscard]] qint64 NowMs() {
	return QDateTime::currentMSecsSinceEpoch();
}

void ForUser(not_null<Main::Session*> session) {
	auto &data = Data();
	if (data.user != session->userId()) {
		data.user = session->userId();
		data.state = State::Unknown;
	}
}

void Finish(bool ok) {
	auto &data = Data();
	data.linking = false;
	auto waiting = base::take(data.waiting);
	for (const auto &callback : waiting) {
		callback(ok);
	}
}

void ClearBotChat(not_null<Main::Session*> session, not_null<UserData*> bot) {
	const auto weak = base::make_weak(session);
	const auto clear = [=] {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		if (const auto history = strong->data().historyLoaded(bot->id)) {
			Core::App().notifications().clearFromHistory(history);
			strong->api().deleteConversation(bot, false);
		}
	};
	clear();
	for (const auto delay : kCleanupDelaysMs) {
		base::call_delayed(delay, session, clear);
	}
	auto &data = Data();
	data.watchLifetime.destroy();
	session->data().newItemAdded(
	) | rpl::filter([=](not_null<HistoryItem*> item) {
		return item->history()->peer == bot;
	}) | rpl::on_next([=] {
		base::call_delayed(0, session, clear);
	}, data.watchLifetime);
	base::call_delayed(kWatchBotMs, session, [] {
		Data().watchLifetime.destroy();
	});
}

void Poll(
		not_null<Main::Session*> session,
		QString nonce,
		not_null<UserData*> bot,
		qint64 expiresAt) {
	if (!Pro::ShouldPoll(QString(), NowMs(), expiresAt)) {
		Finish(false);
		return;
	}
	const auto weak = base::make_weak(session);
	const auto retry = [=] {
		base::call_delayed(Pro::kLinkPollIntervalMs, session, [=] {
			if (const auto strong = weak.get()) {
				Poll(strong, nonce, bot, expiresAt);
			} else {
				Finish(false);
			}
		});
	};
	Data().request = Api::Send({
		.path = u"/v1/link?nonce="_q
			+ QString::fromLatin1(QUrl::toPercentEncoding(nonce)),
	}, [=](Api::Response response) {
		const auto strong = weak.get();
		if (!strong) {
			Finish(false);
			return;
		}
		const auto status = Pro::ParseLink(response.json());
		if (status.linked || status.request == u"linked"_q) {
			Data().state = State::Linked;
			ClearBotChat(strong, bot);
			Finish(true);
		} else if (Pro::ShouldPoll(status.request, NowMs(), expiresAt)) {
			retry();
		} else {
			Finish(false);
		}
	}, [=](Api::Error) {
		retry(); // Offline for a moment: keep trying until the deadline.
	});
}

void StartBot(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		QString nonce,
		qint64 expiresAt) {
	const auto bot = peer->asUser();
	if (!bot || !bot->isBot() || !bot->botInfo) {
		Finish(false);
		return;
	}
	bot->botInfo->startToken = nonce;
	session->api().sendBotStart(show, bot);
	Poll(session, nonce, bot, expiresAt);
}

void Start(not_null<Main::Session*> session, std::shared_ptr<Ui::Show> show) {
	const auto weak = base::make_weak(session);
	Data().request = Api::Send({
		.method = "POST",
		.path = u"/v1/link/start"_q,
	}, [=](Api::Response response) {
		const auto strong = weak.get();
		const auto start = Pro::ParseStart(response.json());
		const auto username = Pro::BotUsername(start.url);
		if (!strong || start.nonce.isEmpty() || username.isEmpty()) {
			Finish(false);
			return;
		}
		const auto expiresAt = std::min(
			(start.expiresAtMs > 0)
				? qint64(start.expiresAtMs)
				: std::numeric_limits<qint64>::max(),
			NowMs() + kLinkTimeoutMs);
		if (const auto peer = strong->data().peerByUsername(username)) {
			StartBot(strong, show, peer, start.nonce, expiresAt);
			return;
		}
		strong->api().request(MTPcontacts_ResolveUsername(
			MTP_flags(0),
			MTP_string(username),
			MTPstring()
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			const auto strong = weak.get();
			if (!strong) {
				Finish(false);
				return;
			}
			const auto &data = result.data();
			strong->data().processUsers(data.vusers());
			strong->data().processChats(data.vchats());
			const auto peer = strong->data().peerLoaded(
				peerFromMTP(data.vpeer()));
			if (!peer) {
				Finish(false);
				return;
			}
			StartBot(strong, show, peer, start.nonce, expiresAt);
		}).fail([=] {
			Finish(false);
		}).send();
	}, [=](Api::Error error) {
		if (error.code == u"already_linked"_q) {
			Data().state = State::Linked;
			Finish(true);
		} else {
			Finish(false);
		}
	});
}

} // namespace

State Cached(not_null<Main::Session*> session) {
	ForUser(session);
	return Data().state;
}

void ClearCache() {
	Data().state = State::Unknown;
}

void Refresh(not_null<Main::Session*> session, Fn<void()> done) {
	ForUser(session);
	Api::Send({
		.path = u"/v1/link"_q,
	}, [=](Api::Response response) {
		Data().state = Pro::ParseLink(response.json()).linked
			? State::Linked
			: State::NotLinked;
		if (done) {
			done();
		}
	}, [=](Api::Error) {
		if (done) {
			done();
		}
	});
}

void Ensure(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show,
		Fn<void(bool)> done) {
	if (Cached(session) == State::Linked) {
		done(true);
		return;
	}
	auto &data = Data();
	data.waiting.push_back(std::move(done));
	if (data.linking) {
		return;
	}
	data.linking = true;
	const auto weak = base::make_weak(session);
	Refresh(session, [=] {
		const auto strong = weak.get();
		if (!strong) {
			Finish(false);
		} else if (Data().state == State::Linked) {
			Finish(true);
		} else {
			Start(strong, show);
		}
	});
}

} // namespace Luckgram::SilentLink
