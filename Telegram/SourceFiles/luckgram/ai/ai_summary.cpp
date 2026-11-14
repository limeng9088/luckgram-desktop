/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_summary.h"

#include "apiwrap.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "history/history.h"
#include "history/history_item.h"
#include "luckgram/ai/ai_compose_logic.h"
#include "luckgram/ai/ai_summary_logic.h"
#include "luckgram/luckgram_ai.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_features.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/pro/luckgram_pro.h"
#include "luckgram/pro/luckgram_pro_logic.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QDateTime>

namespace Luckgram::AI {
namespace {

// Telegram returns at most 100 messages per request.
constexpr auto kPage = 100;
// The card's text part scrolls beyond this.
constexpr auto kMaxBodyHeight = 260;
// Reference links: "lg-summary:<message id>".
constexpr auto kJumpPrefix = "lg-summary:";

[[nodiscard]] bool SummaryEnabled() {
	return Settings::IsEnabled(QString::fromLatin1(Features::kAiSummary));
}

[[nodiscard]] int MaxChars() {
	return int(Settings::RemoteNumber(
		u"ai.max_input_chars"_q,
		kDefaultMaxInputChars));
}

[[nodiscard]] bool IsPro() {
	return Pro::IsPro();
}

// 29 需求 5: "unread" covers up to the Pro limit for Pro users, else 100.
[[nodiscard]] int UnreadCap(int unread) {
	return Pro::SummaryUnreadCap(
		unread,
		IsPro(),
		Pro::Current().summaryMaxNow(QDateTime::currentMSecsSinceEpoch()));
}

[[nodiscard]] Pro::Texts ProTexts() {
	return Pro::Texts(IsChinese());
}

// Mobile ProPerks.Texts.allUnread / followUpHintFree.
[[nodiscard]] QString AllUnread(int count) {
	return IsChinese()
		? (u"全部未读（%1 条）"_q).arg(count)
		: (u"All unread (%1)"_q).arg(count);
}

[[nodiscard]] QString FollowUpHintFree() {
	return Tr("追问（Pro 专享）", "Ask follow-ups (Pro only)");
}

[[nodiscard]] base::flat_map<not_null<History*>, SummaryBar*> &Bars() {
	static auto result = base::flat_map<not_null<History*>, SummaryBar*>();
	return result;
}

// 需求 2: the line of one message, media as "[图片] caption".
[[nodiscard]] QString LineTextOf(not_null<HistoryItem*> item) {
	using Media = Summary::Media;
	const auto text = item->originalText().text;
	const auto chinese = IsChinese();
	const auto media = item->media();
	auto type = Media::None;
	auto detail = QString();
	if (media) {
		if (media->photo()) {
			type = Media::Photo;
		} else if (const auto document = media->document()) {
			if (document->isVoiceMessage()) {
				type = Media::Voice;
			} else if (document->isVideoMessage()) {
				type = Media::VideoMessage;
			} else if (document->sticker()) {
				type = Media::Sticker;
			} else if (document->isAnimation()) {
				type = Media::Gif;
			} else if (document->isVideoFile()) {
				type = Media::Video;
			} else if (document->isAudioFile() || document->isSong()) {
				type = Media::Music;
			} else {
				type = Media::File;
				detail = document->filename();
			}
		} else if (dynamic_cast<const Data::MediaLocation*>(media)) {
			type = Media::Location;
		} else if (media->sharedContact()) {
			type = Media::Contact;
		} else if (const auto poll = media->poll()) {
			type = Media::Poll;
			detail = poll->question.text;
		}
	}
	return Summary::LineText(type, detail, text, chinese);
}

// Loads the last `count` messages straight from Telegram, newest
// backwards, like Android ChatSummaryController.load().
class Loader final : public base::has_weak_ptr {
public:
	explicit Loader(not_null<History*> history) : _history(history) {
	}
	~Loader() {
		cancel();
	}

	void load(int count, Fn<void(int)> done, Fn<void(QString)> fail) {
		cancel();
		_lines.clear();
		_ids.clear();
		_items.clear();
		_done = std::move(done);
		_fail = std::move(fail);
		loadPage(0, count);
	}
	void cancel() {
		if (const auto id = base::take(_requestId)) {
			_history->session().api().request(id).cancel();
		}
	}

	[[nodiscard]] const std::vector<Summary::Line> &lines() const {
		return _lines;
	}
	[[nodiscard]] const std::set<qint64> &ids() const {
		return _ids;
	}

private:
	void loadPage(MsgId offsetId, int remaining) {
		const auto peer = _history->peer;
		const auto limit = std::min(kPage, remaining);
		_requestId = _history->session().api().request(
			MTPmessages_GetHistory(
				peer->input(),
				MTP_int(offsetId.bare),
				MTP_int(0), // offset_date
				MTP_int(0), // add_offset
				MTP_int(limit),
				MTP_int(0), // max_id
				MTP_int(0), // min_id
				MTP_long(0)) // hash
		).done(crl::guard(this, [=](const MTPmessages_Messages &result) {
			_requestId = 0;
			auto &owner = _history->owner();
			owner.processExistingMessages(peer->asChannel(), result);
			auto received = 0;
			auto minId = MsgId();
			result.match([](const MTPDmessages_messagesNotModified &) {
			}, [&](const auto &data) {
				for (const auto &message : data.vmessages().v) {
					const auto id = IdFromMessage(message);
					++received;
					if (!minId || id < minId) {
						minId = id;
					}
					// Ids only: a message deleted while the next pages
					// load must not leave a dangling pointer (review P2).
					_items.push_back(id);
				}
			});
			const auto left = remaining - received;
			if (!received || left <= 0 || received < limit) {
				finish();
			} else {
				loadPage(minId, left);
			}
		})).fail(crl::guard(this, [=](const MTP::Error &error) {
			_requestId = 0;
			if (_items.empty()) {
				// specs#72: Telegram's reason, not just "network error".
				if (const auto fail = base::take(_fail)) {
					fail(error.type());
				}
			} else {
				finish();
			}
		})).send();
	}

	void finish() {
		ranges::sort(_items);
		const auto &owner = _history->owner();
		for (const auto id : _items) {
			const auto item = owner.message(_history->peer, id);
			if (!item || item->isService() || _ids.contains(item->id.bare)) {
				continue;
			}
			const auto text = LineTextOf(item);
			if (text.isEmpty()) {
				continue;
			}
			_lines.push_back({
				.id = item->id.bare,
				.sender = item->out()
					? Tr("我", "Me")
					: item->from()->name(),
				.time = QDateTime::fromSecsSinceEpoch(
					item->date()).toString(u"MM-dd HH:mm"_q),
				.text = text,
			});
			_ids.emplace(item->id.bare);
		}
		_items.clear();
		if (const auto done = base::take(_done)) {
			done(int(_lines.size()));
		}
	}

	const not_null<History*> _history;
	mtpRequestId _requestId = 0;
	std::vector<MsgId> _items;
	std::vector<Summary::Line> _lines;
	std::set<qint64> _ids;
	Fn<void(int)> _done;
	Fn<void(QString)> _fail;

};

// task=summary over the loaded range, long ranges in parts then merged
// (需求 5); task=chat for questions (需求 4).
class Summarizer final : public base::has_weak_ptr {
public:
	~Summarizer() {
		cancel();
	}

	void cancel() {
		if (const auto request = base::take(_request)) {
			request->cancel();
		}
	}

	void summarize(
			const std::vector<Summary::Line> &lines,
			Fn<void(QString)> done,
			Fn<void(Api::Error)> fail) {
		cancel();
		_done = std::move(done);
		_fail = std::move(fail);
		_parts = Summary::Split(lines, MaxChars());
		_partial.clear();
		if (_parts.isEmpty()) {
			if (const auto done = base::take(_done)) {
				done(QString());
			}
			return;
		}
		next();
	}

	void ask(
			const std::vector<Summary::Line> &lines,
			const QString &question,
			Fn<void(QString)> done,
			Fn<void(Api::Error)> fail) {
		cancel();
		_done = std::move(done);
		_fail = std::move(fail);
		_parts.clear();
		_partial.clear();
		chat(
			QString::fromLatin1(kTaskChat),
			Summary::AskRequest(lines, question, MaxChars()),
			[=](QString text) {
				if (const auto done = base::take(_done)) {
					done(text);
				}
			});
	}

private:
	void next() {
		const auto index = int(_partial.size());
		if (index >= _parts.size()) {
			if (_partial.size() == 1) {
				if (const auto done = base::take(_done)) {
					done(_partial.front());
				}
				return;
			}
			chat(
				QString::fromLatin1(kTaskSummary),
				Summary::MergeRequest(_partial, MaxChars()),
				[=](QString text) {
					if (const auto done = base::take(_done)) {
						done(text);
					}
				});
			return;
		}
		chat(
			QString::fromLatin1(kTaskSummary),
			_parts[index],
			[=](QString text) {
				_partial.push_back(text);
				next();
			});
	}

	void chat(
			const QString &task,
			const QString &content,
			Fn<void(QString)> done) {
		auto args = ChatArgs();
		args.task = task;
		args.stream = false;
		args.messages.push_back({ u"user"_q, content });
		_request = Chat(
			std::move(args),
			nullptr,
			crl::guard(this, [=](QString text) {
				_request = nullptr;
				done(text.trimmed());
			}),
			crl::guard(this, [=](Api::Error error) {
				_request = nullptr;
				if (const auto fail = base::take(_fail)) {
					fail(error);
				}
			}));
	}

	Api::RequestPtr _request;
	QStringList _parts;
	QStringList _partial;
	Fn<void(QString)> _done;
	Fn<void(Api::Error)> _fail;

};

// Points with numbered references; a reference jumps to its message (only
// IDs from the range, 需求 3).
[[nodiscard]] TextWithEntities Render(
		const QStringList &points,
		const std::set<qint64> &ids,
		int &reference) {
	auto result = TextWithEntities();
	for (const auto &point : points) {
		if (!result.text.isEmpty()) {
			result.append('\n');
		}
		if (points.size() > 1) {
			result.append(u"• "_q);
		}
		for (const auto &segment : Summary::Parse(point, ids)) {
			if (!segment.messageId) {
				result.append(segment.text);
				continue;
			}
			result.append(' ');
			const auto label = u"[%1]"_q.arg(++reference);
			const auto from = int(result.text.size());
			result.append(label);
			result.entities.push_back(EntityInText(
				EntityType::CustomUrl,
				from,
				label.size(),
				QString::fromLatin1(kJumpPrefix)
					+ QString::number(segment.messageId)));
			result.entities.push_back(EntityInText(
				EntityType::Bold,
				from,
				label.size()));
		}
	}
	return result;
}

} // namespace

struct SummaryBar::State {
	Ui::AbstractButton *bar = nullptr;
	Ui::FlatLabel *barLabel = nullptr;
	Ui::VerticalLayout *card = nullptr;
	Ui::FlatLabel *title = nullptr;
	Ui::LinkButton *collapse = nullptr;
	Ui::RpWidget *scrollWrap = nullptr; // ScrollArea is no RpWidget.
	Ui::ScrollArea *scroll = nullptr;
	Ui::VerticalLayout *body = nullptr;
	Ui::FlatLabel *status = nullptr;
	Ui::FlatLabel *summary = nullptr;
	Ui::FlatLabel *qa = nullptr;
	Ui::RpWidget *askRow = nullptr;
	Ui::InputField *question = nullptr;

	Loader *loader = nullptr;
	Summarizer summarizer;
	Fn<void()> retry;
	TextWithEntities qaText;
	int unread = 0;
	int width = 0;
	int references = 0;
	bool cardShown = false;
	bool collapsed = false;
	bool busy = false;
	bool loaded = false;
};

SummaryBar::SummaryBar(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<History*> history)
: _controller(controller)
, _history(history)
, _wrap(parent, object_ptr<Ui::RpWidget>(parent))
, _shadow(std::make_unique<Ui::PlainShadow>(parent))
, _state(std::make_unique<State>()) {
	_wrap.hide(anim::type::instant);
	_shadow->hide();
	_shadow->showOn(rpl::combine(
		_wrap.shownValue(),
		_wrap.heightValue(),
		rpl::mappers::_1 && rpl::mappers::_2 > 0
	) | rpl::filter([=](bool shown) {
		return (shown == _shadow->isHidden());
	}));
	Bars()[history] = this;
	setup();
}

SummaryBar::~SummaryBar() {
	const auto i = Bars().find(_history);
	if (i != end(Bars()) && i->second == this) {
		Bars().erase(i);
	}
}

SummaryBar *SummaryBar::For(not_null<History*> history) {
	const auto i = Bars().find(history);
	return (i != end(Bars())) ? i->second : nullptr;
}

void SummaryBar::setup() {
	const auto state = _state.get();
	const auto content = _wrap.entity();
	const auto loader = content->lifetime().make_state<Loader>(_history);
	state->loader = loader;
	content->lifetime().add([=] {
		state->summarizer.cancel();
		loader->cancel();
	});
	const auto padding = st::boxRowPadding;
	const auto history = _history;
	const auto controller = _controller;

	// ---- the bar (需求 1)
	state->bar = Ui::CreateChild<Ui::AbstractButton>(content);
	state->bar->setAttribute(Qt::WA_OpaquePaintEvent);
	state->bar->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(state->bar).fillRect(clip, st::historyComposeButtonBg);
	}, state->bar->lifetime());
	state->barLabel = Ui::CreateChild<Ui::FlatLabel>(
		state->bar,
		QString(),
		st::historyTranslateLabel);
	state->barLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	// ---- the card (需求 2–6)
	state->card = Ui::CreateChild<Ui::VerticalLayout>(content);
	state->card->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(state->card).fillRect(clip, st::historyComposeAreaBg);
	}, state->card->lifetime());

	const auto header = state->card->add(
		object_ptr<Ui::RpWidget>(state->card));
	header->resize(header->width(), st::historyTranslateBarHeight);
	state->title = Ui::CreateChild<Ui::FlatLabel>(
		header,
		QString(),
		st::historyTranslateLabel);
	state->collapse = Ui::CreateChild<Ui::LinkButton>(
		header,
		Tr("收起", "Collapse"));
	const auto close = Ui::CreateChild<Ui::LinkButton>(
		header,
		Tr("关闭", "Close"));
	header->widthValue() | rpl::on_next([=](int width) {
		const auto height = header->height();
		const auto top = [&](not_null<QWidget*> w) {
			return (height - w->height()) / 2;
		};
		auto right = padding.right();
		close->moveToRight(right, top(close), width);
		right += close->width() + padding.right();
		state->collapse->moveToRight(right, top(state->collapse), width);
		right += state->collapse->width() + padding.right();
		state->title->resizeToWidth(
			std::max(1, width - right - padding.left()));
		state->title->moveToLeft(padding.left(), top(state->title), width);
	}, header->lifetime());

	// Ui::ScrollArea isn't a Ui::RpWidget, so VerticalLayout::add() can't
	// take it: it lives in a plain row of the card instead.
	state->scrollWrap = state->card->add(
		object_ptr<Ui::RpWidget>(state->card));
	state->scroll = Ui::CreateChild<Ui::ScrollArea>(
		state->scrollWrap,
		st::defaultSolidScroll);
	state->body = state->scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(state->scroll));
	const auto label = [&](const style::FlatLabel &st) {
		const auto result = state->body->add(
			object_ptr<Ui::FlatLabel>(state->body, QString(), st),
			QMargins(padding.left(), 0, padding.right(), padding.top() / 2));
		result->setSelectable(true);
		return result;
	};
	state->status = label(st::luckgramAiSubTextLabel);
	state->summary = label(st::luckgramAiTextLabel);
	state->qa = label(st::luckgramAiTextLabel);
	state->summary->hide();
	state->qa->hide();
	state->card->add(
		object_ptr<Ui::FlatLabel>(
			state->card,
			Tr("AI 生成，可能有误", "AI-generated, may be inaccurate"),
			st::luckgramAiSubTextLabel),
		QMargins(padding.left(), padding.top() / 2, padding.right(), 0));

	state->body->heightValue() | rpl::on_next([=](int height) {
		const auto max = style::ConvertScale(kMaxBodyHeight);
		const auto shown = std::min(height, max);
		state->scrollWrap->resize(state->scrollWrap->width(), shown);
		state->scroll->resize(state->scrollWrap->width(), shown);
	}, state->body->lifetime());
	state->scrollWrap->widthValue() | rpl::on_next([=](int width) {
		state->scroll->resize(width, state->scrollWrap->height());
	}, state->scrollWrap->lifetime());
	state->scroll->widthValue() | rpl::on_next([=](int width) {
		state->body->resizeToWidth(width);
	}, state->scroll->lifetime());

	state->askRow = state->card->add(
		object_ptr<Ui::RpWidget>(state->card),
		QMargins(padding.left(), padding.top() / 2, padding.right(), 0));
	state->question = Ui::CreateChild<Ui::InputField>(
		state->askRow,
		st::defaultInputField,
		Ui::InputField::Mode::SingleLine,
		rpl::single(IsPro()
			? Tr("问这些消息…", "Ask about these messages…")
			: FollowUpHintFree()),
		QString());
	const auto send = Ui::CreateChild<Ui::LinkButton>(
		state->askRow,
		Tr("发送", "Send"));
	state->askRow->resize(0, state->question->height());
	state->askRow->widthValue() | rpl::on_next([=](int width) {
		const auto height = state->askRow->height();
		send->moveToRight(0, (height - send->height()) / 2, width);
		state->question->resizeToWidth(
			std::max(1, width - send->width() - padding.right()));
		state->question->moveToLeft(0, 0, width);
	}, state->askRow->lifetime());

	// ---- layout of the two modes
	const auto relayout = [=] {
		const auto width = state->width;
		if (state->cardShown) {
			state->bar->hide();
			state->card->show();
			state->card->resizeToWidth(width);
			content->resize(width, state->card->height());
		} else {
			state->card->hide();
			state->bar->show();
			const auto height = st::historyTranslateBarHeight;
			state->bar->resize(width, height);
			const auto label = state->barLabel;
			label->resizeToWidth(std::min(
				label->textMaxWidth(),
				std::max(1, width - padding.left() - padding.right())));
			label->moveToLeft(
				(width - label->width()) / 2,
				(height - label->height()) / 2,
				width);
			content->resize(width, height);
		}
	};
	state->card->heightValue() | rpl::skip(1) | rpl::on_next([=] {
		if (state->cardShown) {
			relayout();
		}
	}, state->card->lifetime());
	_relayout = relayout;

	const auto refreshShown = [=] {
		const auto barWanted = SummaryEnabled()
			&& (state->unread >= Summary::kMinUnreadForBar);
		if (!state->cardShown) {
			state->barLabel->setText(IsChinese()
				? (u"%1 条未读消息 · 总结"_q).arg(state->unread)
				: (u"%1 unread messages · Summarize"_q).arg(state->unread));
		}
		relayout();
		_wrap.toggle(
			SummaryEnabled() && (state->cardShown || barWanted),
			anim::type::normal);
	};

	const auto setStatus = [=](const TextWithEntities &text, Fn<void()> retry) {
		state->retry = std::move(retry);
		state->status->setMarkedText(text);
		state->status->setVisible(!text.text.isEmpty());
	};
	const auto failed = [=](const QString &code, Fn<void()> retry) {
		state->busy = false;
		if (code == QLatin1String(Api::kConsentDenied)) {
			state->cardShown = false;
			refreshShown();
			return;
		}
		auto text = TextWithEntities{ Api::ErrorText(code) };
		text.append(Ui::Text::Link(Tr(" 点此重试", " Click to retry")));
		setStatus(text, std::move(retry));
	};
	state->status->setClickHandlerFilter([=](const auto &, auto) {
		if (const auto retry = state->retry) {
			retry();
		}
		return false;
	});
	const auto jumpFilter = [=](const ClickHandlerPtr &handler, auto) {
		const auto url = handler ? handler->url() : QString();
		const auto prefix = QString::fromLatin1(kJumpPrefix);
		if (!url.startsWith(prefix)) {
			return true;
		}
		const auto id = url.mid(prefix.size()).toLongLong();
		controller->showPeerHistory(
			history->peer,
			Window::SectionShow::Way::Forward,
			MsgId(id));
		return false;
	};
	state->summary->setClickHandlerFilter(jumpFilter);
	state->qa->setClickHandlerFilter(jumpFilter);

	// 需求 2: load, summarize, show the points.
	const auto start = std::make_shared<Fn<void(int, QString)>>();
	*start = [=](int count, QString rangeLabel) {
		state->cardShown = true;
		state->collapsed = false;
		state->collapse->setText(Tr("收起", "Collapse"));
		state->scrollWrap->show();
		state->askRow->show();
		state->title->setText(Tr("AI 总结 · ", "AI summary · ") + rangeLabel);
		state->summary->hide();
		state->qa->hide();
		state->qaText = {};
		state->references = 0;
		state->loaded = false;
		state->busy = true;
		state->summarizer.cancel();
		setStatus({ Tr("正在读取消息…", "Reading messages…") }, nullptr);
		refreshShown();
		const auto retry = [=] { (*start)(count, rangeLabel); };
		WithConsent(controller->uiShow(), crl::guard(loader, [=] {
			loader->load(count, crl::guard(loader, [=](int found) {
				if (!found) {
					state->busy = false;
					setStatus({ Tr(
						"没有可总结的文字消息",
						"No text messages to summarize") }, nullptr);
					return;
				}
				state->loaded = true;
				setStatus({ IsChinese()
					? (u"正在总结 %1 条消息…"_q).arg(found)
					: (u"Summarizing %1 messages…"_q).arg(found) }, nullptr);
				state->summarizer.summarize(
					loader->lines(),
					crl::guard(loader, [=](QString text) {
						state->busy = false;
						setStatus({}, nullptr);
						state->summary->setMarkedText(Render(
							Summary::Bullets(text),
							loader->ids(),
							state->references));
						state->summary->show();
					}),
					crl::guard(loader, [=](Api::Error error) {
						Pro::HandleError(controller, error); // 29 需求 6
						failed(error.code, retry);
					}));
			}), crl::guard(loader, [=](QString code) {
				failed(code, retry);
			}));
		}), crl::guard(loader, [=] {
			state->busy = false;
			state->cardShown = false;
			refreshShown();
		}));
	};
	_start = [=](int count, QString rangeLabel) {
		(*start)(count, rangeLabel);
	};

	// 需求 4: questions about the range.
	const auto ask = [=] {
		const auto question = state->question->getLastText().trimmed();
		if (question.isEmpty() || state->busy || !state->loaded) {
			return;
		}
		if (!IsPro()) {
			// 29 需求 5: follow-up questions are Pro-only.
			Pro::ShowUpgrade(controller, Pro::Reason::FollowUp);
			return;
		}
		state->question->clear();
		state->busy = true;
		if (!state->qaText.text.isEmpty()) {
			state->qaText.append(u"\n\n"_q);
		}
		state->qaText.append(Ui::Text::Bold(Tr("问：", "Q: ") + question));
		state->qaText.append('\n');
		const auto before = state->qaText;
		state->qa->setMarkedText(
			TextWithEntities(before).append(QString::fromUtf8("…")));
		state->qa->show();
		state->summarizer.ask(
			loader->lines(),
			question,
			crl::guard(loader, [=](QString text) {
				state->busy = false;
				state->qaText = before;
				state->qaText.append(Render(
					Summary::Bullets(text),
					loader->ids(),
					state->references));
				state->qa->setMarkedText(state->qaText);
				state->scroll->scrollToY(state->scroll->scrollTopMax());
			}),
			crl::guard(loader, [=](Api::Error error) {
				state->busy = false;
				Pro::HandleError(controller, error);
				state->qaText = before;
				state->qaText.append(Api::ErrorText(error.code));
				state->qa->setMarkedText(state->qaText);
			}));
	};
	state->question->submits() | rpl::on_next(ask, state->question->lifetime());
	send->setClickedCallback(ask);

	state->collapse->setClickedCallback([=] {
		state->collapsed = !state->collapsed;
		state->scrollWrap->setVisible(!state->collapsed);
		state->askRow->setVisible(!state->collapsed);
		state->collapse->setText(state->collapsed
			? Tr("展开", "Expand")
			: Tr("收起", "Collapse"));
		relayout();
	});
	close->setClickedCallback([=] {
		state->summarizer.cancel();
		loader->cancel();
		state->busy = false;
		state->cardShown = false;
		refreshShown();
	});
	state->bar->setClickedCallback([=] {
		(*start)(UnreadCap(state->unread), Tr("未读", "Unread"));
	});

	// 需求 1: the unread count of this chat.
	rpl::combine(
		Settings::EnabledValue(QString::fromLatin1(Features::kAiSummary)),
		rpl::single(rpl::empty) | rpl::then(
			history->session().changes().historyUpdates(
				history,
				Data::HistoryUpdate::Flag::UnreadView
			) | rpl::to_empty)
	) | rpl::on_next([=](bool, auto) {
		state->unread = history->unreadCount();
		refreshShown();
	}, _wrap.lifetime());

	_wrap.geometryValue(
	) | rpl::on_next([=](QRect rect) {
		updateShadowGeometry(rect);
	}, _wrap.lifetime());
}

void SummaryBar::showRangePicker() {
	const auto state = _state.get();
	const auto unread = state->unread;
	const auto pro = IsPro();
	const auto badge = u" · "_q + ProTexts().proOnlyBadge();
	const auto controller = _controller;
	const auto weak = base::make_weak(state->loader);
	const auto start = _start;
	const auto run = [=](int count, QString label) {
		if (Pro::SummaryNeedsPro(count, pro)) {
			// 29 需求 5: the larger ranges are Pro-only.
			Pro::ShowUpgrade(controller, Pro::Reason::SummaryRange);
		} else if (weak.get()) {
			start(count, label);
		}
	};
	struct Option {
		QString text;
		int count = 0;
		QString label;
	};
	auto options = std::vector<Option>();
	if (unread > 0) {
		const auto n = UnreadCap(unread);
		const auto all = (unread > Pro::kSummaryMaxFree);
		if (!all) {
			options.push_back({
				IsChinese()
					? (u"未读（%1 条）"_q).arg(n)
					: (u"Unread (%1)"_q).arg(n),
				n,
				Tr("未读", "Unread"),
			});
		} else if (pro) {
			options.push_back({ AllUnread(n), n, Tr("未读", "Unread") });
		} else {
			// > 100: opens the upgrade box.
			options.push_back({
				AllUnread(unread) + badge,
				unread,
				Tr("未读", "Unread"),
			});
		}
	}
	for (const auto n : Summary::kRanges) {
		const auto label = IsChinese()
			? (u"最近 %1 条"_q).arg(n)
			: (u"Last %1"_q).arg(n);
		options.push_back({
			Pro::SummaryNeedsPro(n, pro) ? (label + badge) : label,
			n,
			label,
		});
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Tr("总结聊天", "Summarize chat")));
		for (const auto &option : options) {
			const auto button = box->addRow(
				object_ptr<Ui::SettingsButton>(
					box,
					rpl::single(option.text),
					st::settingsButtonNoIcon),
				QMargins());
			button->setClickedCallback([=, count = option.count,
					label = option.label] {
				box->closeBox();
				run(count, label);
			});
		}
		box->addButton(rpl::single(Tr("取消", "Cancel")), [=] {
			box->closeBox();
		});
	}));
}

void SummaryBar::updateShadowGeometry(QRect wrapGeometry) {
	const auto regular = QRect(
		wrapGeometry.x(),
		wrapGeometry.y() + wrapGeometry.height(),
		wrapGeometry.width(),
		st::lineWidth);
	_shadow->setGeometry(_shadowGeometryPostprocess
		? _shadowGeometryPostprocess(regular)
		: regular);
}

void SummaryBar::raise() {
	_wrap.raise();
	_shadow->raise();
}

void SummaryBar::finishAnimating() {
	_wrap.finishAnimating();
}

void SummaryBar::setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess) {
	_shadowGeometryPostprocess = std::move(postprocess);
	updateShadowGeometry(_wrap.geometry());
}

void SummaryBar::move(int x, int y) {
	_wrap.move(x, y);
}

void SummaryBar::resizeToWidth(int width) {
	if (_state->width != width) {
		_state->width = width;
		if (_relayout) {
			_relayout();
		}
	}
}

int SummaryBar::height() const {
	return _wrap.height();
}

rpl::producer<int> SummaryBar::heightValue() const {
	return _wrap.heightValue();
}

void AddSummaryMenuAction(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const Ui::Menu::MenuCallback &addAction) {
	if (!SummaryEnabled()) {
		return;
	}
	const auto history = peer->owner().history(peer);
	if (!SummaryBar::For(history)) {
		return; // Only for the open chat, where the card shows.
	}
	addAction(Tr("总结聊天", "Summarize chat"), [=] {
		if (const auto bar = SummaryBar::For(history)) {
			bar->showRangePicker();
		}
	}, &st::menuIconDiscussion);
}

} // namespace Luckgram::AI
