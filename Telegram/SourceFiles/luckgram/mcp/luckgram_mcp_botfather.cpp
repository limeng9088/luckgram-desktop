/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/mcp/luckgram_mcp_botfather.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/flat_map.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_reply_markup.h"
#include "main/main_session.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonArray>

#include <algorithm>
#include <deque>

namespace Luckgram::Mcp {
namespace {

constexpr auto kPollInterval = crl::time(600);
constexpr auto kAnswerPolls = 34; // About 20 s for an answer to one line.
constexpr auto kPressPolls = 12; // About 7 s for a pressed button to show.
constexpr auto kReadLimit = 20;
// Longer than the longest script: three lines, about 20 s each.
constexpr auto kRunTimeout = crl::time(90 * 1000);

using Button = HistoryMessageMarkupButton;

struct Run {
	quint64 account = 0; // Whose conversation: one runs at a time.
	base::weak_ptr<Main::Session> session;
	PeerId peerId = 0;
	QString tool;
	QJsonObject arguments;
	QStringList steps;
	int index = 0;
	MsgId baseline = 0; // Answers are newer than this.
	TimeId sentAt = 0;
	int polls = 0;
	QString watched; // What the pressed message looked like before.
	MsgId pressed = 0;
	ToolDone done;
};

[[nodiscard]] ToolOutcome Failed(const QString &text) {
	auto result = ToolOutcome();
	result.error = text;
	return result;
}

[[nodiscard]] QJsonArray ButtonsJson(not_null<HistoryItem*> item) {
	auto result = QJsonArray();
	const auto markup = item->Get<HistoryMessageReplyMarkup>();
	if (!markup) {
		return result;
	}
	for (const auto &row : markup->data.rows) {
		for (const auto &button : row) {
			auto entry = QJsonObject();
			entry.insert(u"text"_q, button.text);
			if (button.type == Button::Type::Url) {
				entry.insert(u"url"_q, QString::fromUtf8(button.data));
			}
			result.push_back(entry);
		}
	}
	return result;
}

[[nodiscard]] QJsonObject MessageJson(not_null<HistoryItem*> item) {
	auto result = QJsonObject();
	result.insert(u"id"_q, double(item->id.bare));
	result.insert(u"date"_q, double(item->date()));
	result.insert(u"out"_q, item->out());
	result.insert(u"text"_q, item->originalText().text);
	const auto buttons = ButtonsJson(item);
	if (!buttons.isEmpty()) {
		result.insert(u"buttons"_q, buttons);
	}
	return result;
}

// Text and buttons together: what changes when BotFather edits a message
// after a button under it was pressed.
[[nodiscard]] QString Signature(not_null<HistoryItem*> item) {
	auto result = item->originalText().text;
	for (const auto &entry : ButtonsJson(item)) {
		result += QChar('\n') + entry.toObject().value(u"text"_q).toString();
	}
	return result;
}

// The newest message of @BotFather that is an answer: after `baseline`,
// not ours, and not older than the line it answers.
[[nodiscard]] HistoryItem *NewAnswer(
		not_null<History*> history,
		const std::shared_ptr<Run> &run) {
	const auto last = history->lastMessage();
	if (!last
		|| last->out()
		|| !IsServerMsgId(last->id)
		|| last->id <= run->baseline
		|| last->date() + 5 < run->sentAt) {
		return nullptr;
	}
	return last;
}

// One conversation with BotFather per account at a time: answers are told
// apart only by coming after the line that was sent, so two tools talking
// at once would read each other's answers (and one could be handed the
// token the other asked for). The rest wait in the order they came.
struct Queue {
	bool busy = false;
	std::deque<Fn<void()>> waiting;
};

[[nodiscard]] base::flat_map<quint64, Queue> &Queues() {
	static auto result = base::flat_map<quint64, Queue>();
	return result;
}

void Enqueue(quint64 account, Fn<void()> start) {
	auto &queue = Queues()[account];
	if (queue.busy) {
		queue.waiting.push_back(std::move(start));
		return;
	}
	queue.busy = true;
	start();
}

void Finish(const std::shared_ptr<Run> &run, ToolOutcome outcome) {
	const auto done = base::take(run->done);
	if (!done) {
		return; // Finished before: the queue has moved on already.
	}
	done(std::move(outcome));
	// The next one starts from the event loop, after this answer is out.
	const auto account = run->account;
	crl::on_main([=] {
		auto &queue = Queues()[account];
		if (queue.waiting.empty()) {
			queue.busy = false;
			return;
		}
		const auto next = std::move(queue.waiting.front());
		queue.waiting.pop_front();
		next();
	});
}

// The result of a script: BotFather's last answer, and what the tool was
// for (the token, the list of bots).
void FinishWithAnswer(
		const std::shared_ptr<Run> &run,
		not_null<HistoryItem*> answer) {
	const auto text = answer->originalText().text;
	if (!BotFatherSucceeded(run->tool, text)) {
		Finish(run, Failed(u"BotFather answered: "_q + text.left(600)));
		return;
	}
	auto result = ToolOutcome();
	result.value.insert(u"reply"_q, MessageJson(answer));
	const auto token = ExtractBotToken(text);
	if (run->tool == u"botfather_create_bot"_q) {
		const auto username = run->arguments.value(u"username"_q).toString();
		result.value.insert(u"created"_q, true);
		result.value.insert(u"username"_q, username);
		result.value.insert(u"link"_q, u"https://t.me/"_q + username);
		result.value.insert(u"token"_q, token);
	} else if (run->tool == u"botfather_get_token"_q
		|| run->tool == u"botfather_revoke_token"_q) {
		result.value.insert(
			u"bot"_q,
			run->arguments.value(u"bot"_q).toString());
		result.value.insert(u"token"_q, token);
	} else if (run->tool == u"botfather_list_bots"_q) {
		auto bots = QJsonArray();
		for (const auto &entry : ButtonsJson(answer)) {
			const auto name = entry.toObject().value(u"text"_q).toString();
			if (name.startsWith(QChar('@'))) {
				bots.push_back(name.mid(1));
			}
		}
		result.value.insert(u"bots"_q, bots);
	} else if (run->tool != u"botfather_send"_q) {
		result.value.insert(
			u"bot"_q,
			run->arguments.value(u"bot"_q).toString());
		result.value.insert(u"done"_q, true);
	}
	Finish(run, std::move(result));
}

void NextStep(const std::shared_ptr<Run> &run);

void WaitForAnswer(const std::shared_ptr<Run> &run) {
	const auto session = run->session.get();
	if (!session) {
		Finish(run, Failed(u"The account is gone."_q));
		return;
	}
	base::call_delayed(kPollInterval, QCoreApplication::instance(), [=] {
		const auto strong = run->session.get();
		const auto peer = strong
			? strong->data().peerLoaded(run->peerId)
			: nullptr;
		if (!peer) {
			Finish(run, Failed(u"The account is gone."_q));
			return;
		}
		const auto history = strong->data().history(peer);
		const auto answer = NewAnswer(history, run);
		if (!answer) {
			if (++run->polls > kAnswerPolls) {
				Finish(run, Failed(
					u"BotFather did not answer in time. Use botfather_read "
					u"to see where the conversation stopped."_q));
			} else {
				WaitForAnswer(run);
			}
			return;
		}
		run->baseline = answer->id;
		++run->index;
		const auto text = answer->originalText().text;
		if (run->index < run->steps.size() && BotFatherRefused(text)) {
			// The next line would be read as something else.
			Finish(run, Failed(u"BotFather answered: "_q + text.left(600)));
		} else if (run->index < run->steps.size()) {
			NextStep(run);
		} else {
			FinishWithAnswer(run, answer);
		}
	});
}

void NextStep(const std::shared_ptr<Run> &run) {
	const auto session = run->session.get();
	const auto peer = session
		? session->data().peerLoaded(run->peerId)
		: nullptr;
	if (!peer) {
		Finish(run, Failed(u"The account is gone."_q));
		return;
	}
	const auto history = session->data().history(peer);
	if (const auto last = history->lastMessage()) {
		if (IsServerMsgId(last->id) && last->id > run->baseline) {
			run->baseline = last->id;
		}
	}
	auto message = Api::MessageToSend(Api::SendAction(history));
	message.textWithTags = { run->steps[run->index], TextWithTags::Tags() };
	message.action.clearDraft = false;
	run->sentAt = base::unixtime::now();
	run->polls = 0;
	session->api().sendMessage(std::move(message));
	WaitForAnswer(run);
}

void WaitForPress(const std::shared_ptr<Run> &run) {
	const auto session = run->session.get();
	if (!session) {
		Finish(run, Failed(u"The account is gone."_q));
		return;
	}
	base::call_delayed(kPollInterval, QCoreApplication::instance(), [=] {
		const auto strong = run->session.get();
		const auto peer = strong
			? strong->data().peerLoaded(run->peerId)
			: nullptr;
		if (!peer) {
			Finish(run, Failed(u"The account is gone."_q));
			return;
		}
		const auto history = strong->data().history(peer);
		const auto item = strong->data().message(run->peerId, run->pressed);
		const auto answer = NewAnswer(history, run);
		const auto edited = item && (Signature(item) != run->watched);
		if (!answer && !edited && ++run->polls <= kPressPolls) {
			WaitForPress(run);
			return;
		}
		// A new message wins over the edited one: BotFather answers some
		// buttons with a message of its own.
		const auto shown = answer ? answer : item;
		if (!shown) {
			Finish(run, Failed(u"The message is gone."_q));
			return;
		}
		auto result = ToolOutcome();
		result.value.insert(u"reply"_q, MessageJson(shown));
		result.value.insert(u"changed"_q, (answer != nullptr) || edited);
		const auto token = ExtractBotToken(shown->originalText().text);
		if (!token.isEmpty()) {
			result.value.insert(u"token"_q, token);
		}
		Finish(run, std::move(result));
	});
}

void Press(const std::shared_ptr<Run> &run, not_null<PeerData*> peer) {
	const auto session = run->session.get();
	const auto id = MsgId(run->arguments.value(u"message_id"_q).toInt());
	const auto wanted = run->arguments.value(u"button"_q).toString();
	const auto item = session->data().message(run->peerId, id);
	const auto markup = item
		? item->Get<HistoryMessageReplyMarkup>()
		: nullptr;
	if (!item || item->out()) {
		Finish(run, Failed(
			u"No such message of BotFather is loaded. Call botfather_read "
			u"and use an id from it."_q));
		return;
	} else if (!markup) {
		Finish(run, Failed(u"This message has no buttons."_q));
		return;
	}
	const Button *found = nullptr;
	for (const auto &row : markup->data.rows) {
		for (const auto &button : row) {
			if (!found && !button.text.compare(wanted, Qt::CaseInsensitive)) {
				found = &button;
			}
		}
	}
	if (!found) {
		auto names = QStringList();
		for (const auto &entry : ButtonsJson(item)) {
			names.push_back(entry.toObject().value(u"text"_q).toString());
		}
		Finish(run, Failed(
			u"No such button. The buttons are: "_q + names.join(u", "_q)));
		return;
	}
	// Checked on the button itself as well, not only on the name asked for.
	const auto refusal = BotFatherPressRefusal(found->text);
	if (!refusal.isEmpty()) {
		Finish(run, Failed(refusal));
		return;
	}
	const auto history = session->data().history(peer);
	if (const auto last = history->lastMessage()) {
		if (IsServerMsgId(last->id)) {
			run->baseline = last->id;
		}
	}
	run->sentAt = base::unixtime::now();
	run->polls = 0;
	run->pressed = id;
	run->watched = Signature(item);
	if (found->type == Button::Type::Url) {
		auto result = ToolOutcome();
		result.value.insert(u"url"_q, QString::fromUtf8(found->data));
		result.value.insert(u"changed"_q, false);
		Finish(run, std::move(result));
	} else if (found->type == Button::Type::Callback) {
		using Flag = MTPmessages_GetBotCallbackAnswer::Flag;
		session->api().request(MTPmessages_GetBotCallbackAnswer(
			MTP_flags(Flag::f_data),
			peer->input(),
			MTP_int(int(id.bare)),
			MTP_bytes(found->data),
			MTP_inputCheckPasswordEmpty()
		)).done([=](const MTPmessages_BotCallbackAnswer &) {
			WaitForPress(run);
		}).fail([=](const MTP::Error &error) {
			// BotFather often edits the message and lets the query time
			// out: the message is what tells.
			if (error.type() == u"BOT_RESPONSE_TIMEOUT"_q) {
				WaitForPress(run);
			} else {
				Finish(run, Failed(u"Telegram refused: "_q + error.type()));
			}
		}).send();
	} else if (found->type == Button::Type::Default) {
		// A button of the keyboard under the input field: its text is sent,
		// under the same rule as a typed line.
		const auto refused = BotFatherSendRefusal(found->text);
		if (!refused.isEmpty()) {
			Finish(run, Failed(refused));
			return;
		}
		run->steps = QStringList{ found->text };
		run->index = 0;
		run->tool = u"botfather_send"_q;
		NextStep(run);
	} else {
		Finish(run, Failed(
			u"This kind of button cannot be pressed from here."_q));
	}
}

void Read(const std::shared_ptr<Run> &run, not_null<PeerData*> peer) {
	const auto session = run->session.get();
	const auto limit = std::clamp(
		run->arguments.value(u"limit"_q).toInt(),
		1,
		kReadLimit);
	session->api().request(MTPmessages_GetHistory(
		peer->input(),
		MTP_int(0),
		MTP_int(0),
		MTP_int(0),
		MTP_int(limit),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		const auto strong = run->session.get();
		if (!strong) {
			Finish(run, Failed(u"The account is gone."_q));
			return;
		}
		const auto owner = &strong->data();
		auto messages = QJsonArray();
		result.match([](const MTPDmessages_messagesNotModified &) {
		}, [&](const auto &data) {
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());
			for (const auto &message : data.vmessages().v) {
				// Kept by the app like any loaded message, so a button
				// under it can be pressed by its id afterwards.
				const auto item = owner->addNewMessage(
					message,
					MessageFlags(),
					NewMessageType::Existing);
				if (item) {
					messages.push_back(MessageJson(item));
				}
			}
		});
		auto outcome = ToolOutcome();
		outcome.value.insert(u"messages"_q, messages);
		Finish(run, std::move(outcome));
	}).fail([=](const MTP::Error &error) {
		Finish(run, Failed(u"Telegram refused: "_q + error.type()));
	}).send();
}

void Start(const std::shared_ptr<Run> &run, not_null<PeerData*> peer) {
	const auto user = peer->asUser();
	// The name alone is not proof: only the real BotFather is talked to.
	if (!user
		|| !user->isBot()
		|| quint64(peerToUser(user->id).bare) != kBotFatherUserId) {
		Finish(run, Failed(u"@BotFather could not be found."_q));
		return;
	}
	run->peerId = peer->id;
	if (run->tool == u"botfather_read"_q) {
		Read(run, peer);
	} else if (run->tool == u"botfather_press"_q) {
		Press(run, peer);
	} else {
		run->steps = BotFatherScript(run->tool, run->arguments);
		if (run->steps.isEmpty()) {
			Finish(run, Failed(u"This tool is not available."_q));
		} else {
			NextStep(run);
		}
	}
}

} // namespace

void RunBotFather(
		not_null<Main::Session*> session,
		const QString &tool,
		const QJsonObject &arguments,
		ToolDone done) {
	const auto run = std::make_shared<Run>();
	run->account = quint64(session->userId().bare);
	run->session = base::make_weak(session);
	run->tool = tool;
	run->arguments = arguments;
	run->done = std::move(done);
	Enqueue(run->account, [=] {
		// Whatever happens to the account meanwhile (logged out: its
		// requests and timers are dropped without a word), this run ends
		// and lets the next one of the queue start.
		base::call_delayed(kRunTimeout, QCoreApplication::instance(), [=] {
			Finish(run, Failed(u"BotFather did not answer in time."_q));
		});
		const auto strong = run->session.get();
		if (!strong) {
			Finish(run, Failed(u"The account is gone."_q));
			return;
		}
		const auto owner = &strong->data();
		const auto username = QString::fromLatin1(kBotFatherUsername);
		if (const auto known = owner->peerByUsername(username)) {
			Start(run, known);
			return;
		}
		strong->api().request(MTPcontacts_ResolveUsername(
			MTP_flags(0),
			MTP_string(username),
			MTP_string()
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			const auto &data = result.data();
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());
			const auto peerId = peerFromMTP(data.vpeer());
			const auto peer = peerId ? owner->peerLoaded(peerId) : nullptr;
			if (!peer) {
				Finish(run, Failed(u"@BotFather could not be found."_q));
			} else {
				Start(run, peer);
			}
		}).fail([=](const MTP::Error &error) {
			Finish(run, Failed(u"Telegram refused: "_q + error.type()));
		}).send();
	});
}

} // namespace Luckgram::Mcp
