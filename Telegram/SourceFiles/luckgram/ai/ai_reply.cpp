/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_reply.h"

#include "chat_helpers/compose/compose_show.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "luckgram/ai/ai_compose_logic.h"
#include "luckgram/ai/ai_writing_registry.h"
#include "luckgram/luckgram_ai.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_features.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace Luckgram::AI {
namespace {

[[nodiscard]] bool ReplyEnabled() {
	return Settings::IsEnabled(QString::fromLatin1(Features::kAiReply));
}

[[nodiscard]] QString Me() {
	return Tr("我", "Me");
}

// Android AiReplyFeature.transcript: the media words when there's no text.
[[nodiscard]] QString ItemText(not_null<HistoryItem*> item) {
	const auto text = item->originalText().text;
	if (!text.isEmpty()) {
		return text;
	}
	const auto media = item->media();
	if (!media) {
		return QString();
	} else if (media->photo()) {
		return Tr("[图片]", "[Photo]");
	} else if (const auto document = media->document()) {
		if (document->isVoiceMessage()) {
			return Tr("[语音]", "[Voice message]");
		} else if (document->isVideoFile() || document->isVideoMessage()) {
			return Tr("[视频]", "[Video]");
		} else if (document->sticker()) {
			return Tr("[贴纸]", "[Sticker]");
		}
		return Tr("[文件]", "[File]");
	}
	return QString();
}

// Recent loaded messages, oldest first, as "name: text" lines (20 需求 2).
// In a topic or replies thread only that thread's messages.
[[nodiscard]] QString Transcript(
		not_null<History*> history,
		MsgId topicRootId) {
	auto lines = QStringList();
	for (auto b = history->blocks.rbegin(); b != history->blocks.rend(); ++b) {
		const auto &messages = (*b)->messages;
		for (auto m = messages.rbegin(); m != messages.rend(); ++m) {
			if (lines.size() >= Reply::kContextMessages) {
				break;
			}
			const auto item = (*m)->data();
			if (item->isService() || !item->isRegular()) {
				continue;
			} else if (topicRootId
				&& item->id != topicRootId
				&& item->topicRootId() != topicRootId
				&& item->replyToTop() != topicRootId) {
				continue;
			}
			const auto text = ItemText(item);
			if (text.isEmpty()) {
				continue;
			}
			const auto name = item->out()
				? Me()
				: item->from()->name();
			lines.push_front(Reply::TranscriptLine(name, text));
		}
		if (lines.size() >= Reply::kContextMessages) {
			break;
		}
	}
	return lines.join('\n');
}

struct PanelState {
	Api::RequestPtr request;
	TextWithTags draft; // The user's text when opened; "换一批" uses it.
	Ui::FlatLabel *status = nullptr;
	Ui::VerticalLayout *list = nullptr;
	std::vector<Ui::AbstractButton*> rows;
	std::vector<Ui::FlatLabel*> labels;
	std::vector<QString> texts;
	Ui::LinkButton *again = nullptr;
	Ui::LinkButton *undo = nullptr;
	bool picked = false;
};

void ShowCandidates(
		not_null<WritingHost*> host,
		not_null<PanelState*> state,
		const QStringList &candidates) {
	if (!candidates.isEmpty()) {
		state->status->hide();
	}
	const auto padding = st::historySendPadding;
	for (auto i = 0, count = int(candidates.size()); i != count; ++i) {
		if (i >= int(state->rows.size())) {
			const auto row = state->list->add(
				object_ptr<Ui::AbstractButton>(state->list));
			const auto label = Ui::CreateChild<Ui::FlatLabel>(
				row,
				QString(),
				st::defaultFlatLabel);
			label->setAttribute(Qt::WA_TransparentForMouseEvents);
			row->widthValue() | rpl::on_next([=](int width) {
				label->resizeToWidth(std::max(0, width - 4 * padding));
				row->resize(width, label->height() + 2 * padding);
				label->moveToLeft(2 * padding, padding, width);
			}, row->lifetime());
			label->heightValue() | rpl::skip(1) | rpl::on_next([=] {
				const auto width = row->width();
				row->resize(width, label->height() + 2 * padding);
			}, row->lifetime());
			row->paintRequest() | rpl::on_next([=](QRect clip) {
				if (row->isOver()) {
					QPainter(row).fillRect(clip, st::windowBgOver);
				}
			}, row->lifetime());
			const auto index = int(state->rows.size());
			row->setClickedCallback([=] {
				// 20 需求 3: replaces the field (undoable), never sends.
				const auto &text = state->texts[index];
				host->setText({ text }, !state->picked);
				state->picked = true;
				state->undo->show();
			});
			state->rows.push_back(row);
			state->labels.push_back(label);
			state->texts.emplace_back();
		}
		state->texts[i] = candidates[i];
		state->labels[i]->setText(candidates[i]);
	}
}

void Generate(not_null<WritingHost*> host, not_null<PanelState*> state) {
	if (const auto request = base::take(state->request)) {
		request->cancel();
	}
	const auto history = host->history();
	if (!history) {
		return;
	}
	for (const auto row : state->rows) {
		delete row; // VerticalLayout removes the row.
	}
	state->rows.clear();
	state->labels.clear();
	state->texts.clear();
	state->status->setText(Tr("正在生成…", "Writing…"));
	state->status->show();
	state->again->setEnabled(false);

	auto args = ChatArgs();
	args.task = QString::fromLatin1(kTaskReply);
	args.stream = true;
	args.messages.push_back({
		u"user"_q,
		Reply::Prompt(
			Transcript(history, host->topicRootId()),
			state->draft.text,
			Me()),
	});
	const auto list = state->list;
	state->request = Chat(
		std::move(args),
		crl::guard(list, [=](QString, QString soFar) {
			ShowCandidates(host, state, Reply::Candidates(soFar));
		}),
		crl::guard(list, [=](QString text) {
			state->request = nullptr;
			state->again->setEnabled(true);
			const auto candidates = Reply::Candidates(text);
			ShowCandidates(host, state, candidates);
			if (candidates.isEmpty()) {
				state->status->setText(Tr(
					"没有生成回复，请换一批",
					"No replies. Please try again"));
				state->status->show();
			}
		}),
		crl::guard(list, [=](Api::Error error) {
			state->request = nullptr;
			state->again->setEnabled(true);
			state->status->setText(Api::ErrorText(error.code));
			state->status->show();
		}));
}

void ShowPanel(not_null<WritingHost*> host) {
	auto panel = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto raw = panel.data();
	const auto state = raw->lifetime().make_state<PanelState>();
	state->draft = host->text();
	raw->lifetime().add([=] {
		if (const auto request = base::take(state->request)) {
			request->cancel();
		}
	});
	raw->paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(raw);
		p.fillRect(clip, st::historyComposeAreaBg);
		p.fillRect(0, 0, raw->width(), st::lineWidth, st::shadowFg);
	}, raw->lifetime());

	const auto padding = st::historySendPadding;
	const auto header = raw->add(object_ptr<Ui::RpWidget>(raw));
	header->resize(header->width(), st::historyReplyHeight * 3 / 4);
	const auto title = Ui::CreateChild<Ui::FlatLabel>(
		header,
		Tr("帮我回复", "Help me reply"),
		st::defaultSubTextLabel);
	state->undo = Ui::CreateChild<Ui::LinkButton>(
		header,
		Tr("撤销", "Undo"));
	state->undo->hide();
	state->again = Ui::CreateChild<Ui::LinkButton>(
		header,
		Tr("换一批", "Try again"));
	const auto close = Ui::CreateChild<Ui::LinkButton>(header, u"✕"_q);
	header->widthValue() | rpl::on_next([=](int width) {
		const auto top = [&](not_null<QWidget*> w) {
			return (header->height() - w->height()) / 2;
		};
		auto right = 2 * padding;
		close->moveToRight(right, top(close), width);
		right += close->width() + 2 * padding;
		state->again->moveToRight(right, top(state->again), width);
		right += state->again->width() + 2 * padding;
		state->undo->moveToRight(right, top(state->undo), width);
		title->moveToLeft(2 * padding, top(title), width);
	}, header->lifetime());

	state->status = raw->add(
		object_ptr<Ui::FlatLabel>(raw, QString(), st::defaultFlatLabel),
		QMargins(2 * padding, 0, 2 * padding, padding));
	state->list = raw->add(object_ptr<Ui::VerticalLayout>(raw));

	state->undo->setClickedCallback([=] {
		host->undo();
		state->picked = false;
		state->undo->hide();
	});
	state->again->setClickedCallback([=] {
		Generate(host, state);
	});
	close->setClickedCallback([=] {
		host->hidePanel(raw);
	});

	host->showPanel(std::move(panel));
	Generate(host, state);
}

} // namespace

void RegisterReplyItem() {
	RegisterWritingItem({
		.id = u"ai_reply"_q,
		.titleZh = "帮我回复",
		.titleEn = "Help me reply",
		.icon = &st::menuIconReply,
		.order = Reply::kMenuOrder,
		.allowEmptyText = true,
		.visible = [](not_null<WritingHost*> host) {
			return ReplyEnabled() && host->history();
		},
		.action = [](not_null<WritingHost*> host) {
			// 00: the consent prompt before anything is sent.
			WithConsent(host->show(), crl::guard(host.get(), [=] {
				ShowPanel(host);
			}));
		},
	});
}

} // namespace Luckgram::AI
