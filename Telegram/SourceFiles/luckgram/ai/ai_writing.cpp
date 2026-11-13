/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_writing.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_channel.h"
#include "data/data_drafts.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "history/view/history_view_schedule_box.h"
#include "luckgram/ai/ai_compose_logic.h"
#include "luckgram/ai/ai_text_logic.h"
#include "luckgram/ai/ai_writing_registry.h"
#include "luckgram/luckgram_ai.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_compose_panel.h"
#include "luckgram/luckgram_compose_slots.h"
#include "luckgram/luckgram_features.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "menu/menu_send_details.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QPointer>
#include <QtGui/QCursor>

namespace Luckgram::AI {
namespace {

// 21 需求 3: "已润色 · 撤销" stays this long.
constexpr auto kUndoHintMs = crl::time(8000);

[[nodiscard]] bool WritingEnabled() {
	return Settings::IsEnabled(QString::fromLatin1(Features::kAiWriting));
}

[[nodiscard]] const style::icon *EffectIcon(const QString &id) {
	if (id == u"polish"_q) {
		return &st::menuIconCustomize;
	} else if (id == u"formal"_q) {
		return &st::menuIconManage;
	} else if (id == u"friendly"_q) {
		return &st::menuIconEmoji;
	} else if (id == u"shorter"_q) {
		return &st::menuIconShrink;
	}
	return &st::menuIconEdit;
}

// The text with its formatting moved to where the same text survived in
// the result (21 review P2 on mobile: mentions, links, custom emoji, bold).
[[nodiscard]] TextWithTags WithFormatting(
		const TextWithTags &before,
		const QString &result) {
	if (before.tags.isEmpty()) {
		return { result };
	}
	auto ranges = std::vector<std::pair<int, int>>();
	ranges.reserve(before.tags.size());
	for (const auto &tag : before.tags) {
		ranges.emplace_back(tag.offset, tag.offset + tag.length);
	}
	const auto starts = Writing::Relocate(before.text, ranges, result);
	auto out = TextWithTags{ result };
	for (auto i = 0, count = int(starts.size()); i != count; ++i) {
		if (starts[i] >= 0) {
			auto tag = before.tags[i];
			tag.offset = starts[i];
			out.tags.push_back(tag);
		}
	}
	return out;
}

// 21 review P3: the chat was switched while the result was streaming into
// the shared field, so the half-written text was saved as that chat's local
// draft. Put the user's original back.
void RestoreDraft(
		not_null<History*> history,
		const TextWithTags &before,
		const QString &written) {
	const auto draft = history->localDraft(MsgId(), PeerId());
	if (!draft || draft->textWithTags.text != written) {
		return;
	}
	const auto size = int(before.text.size());
	draft->textWithTags = before;
	draft->cursor = MessageCursor(size, size, 0);
	history->session().local().writeDrafts(history);
}

// The WritingHost of one message field (21 需求 6).
class FieldHost final : public WritingHost {
public:
	explicit FieldHost(const Compose::SlotContext &context);
	~FieldHost();

	not_null<Main::Session*> session() override;
	std::shared_ptr<ChatHelpers::Show> show() override;
	History *history() override;
	MsgId topicRootId() override;
	TextWithTags text() override;
	void setText(const TextWithTags &text, bool undoable) override;
	bool canUndo() override;
	void undo() override;
	Ui::RpWidget *showPanel(object_ptr<Ui::RpWidget> panel) override;
	void hidePanel(not_null<Ui::RpWidget*> panel) override;
	bool canPostInChannel() override;
	void send(const TextWithTags &text, bool schedule) override;

	void showMenu(not_null<QWidget*> parent);
	void runEffect(const Writing::Effect &effect);
	[[nodiscard]] bool busy() const;

private:
	void replaceText(
		const TextWithTags &text,
		Ui::InputField::HistoryAction action
			= Ui::InputField::HistoryAction::NewEntry);
	void showUndoHint(const QString &done);
	void hideUndoHint();
	void sendNow(const TextWithTags &text, ::Api::SendOptions options);

	const not_null<Ui::InputField*> _field;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const Fn<History*()> _history;
	const Fn<MsgId()> _topicRootId;
	std::optional<TextWithTags> _undoText;
	Api::RequestPtr _request;
	rpl::lifetime _requestLifetime;
	bool _writing = false;
	base::unique_qptr<Ui::PopupMenu> _menu;
	QPointer<Ui::RpWidget> _undoHint;
	base::Timer _undoHintTimer;

};

FieldHost::FieldHost(const Compose::SlotContext &context)
: _field(context.field)
, _show(context.show)
, _history(context.history)
, _topicRootId(context.topicRootId)
, _undoHintTimer([=] { hideUndoHint(); }) {
}

FieldHost::~FieldHost() {
	if (const auto request = base::take(_request)) {
		request->cancel();
	}
}

not_null<Main::Session*> FieldHost::session() {
	return &_show->session();
}

std::shared_ptr<ChatHelpers::Show> FieldHost::show() {
	return _show;
}

History *FieldHost::history() {
	return _history ? _history() : nullptr;
}

MsgId FieldHost::topicRootId() {
	return _topicRootId ? _topicRootId() : MsgId();
}

TextWithTags FieldHost::text() {
	return _field->getTextWithTags();
}

void FieldHost::replaceText(
		const TextWithTags &text,
		Ui::InputField::HistoryAction action) {
	_writing = true;
	_field->setTextWithTags(text, action);
	_field->setCursorPosition(text.text.size());
	_writing = false;
}

void FieldHost::setText(const TextWithTags &text, bool undoable) {
	if (undoable) {
		_undoText = _field->getTextWithTags();
	}
	replaceText(text);
}

bool FieldHost::canUndo() {
	return _undoText.has_value();
}

void FieldHost::undo() {
	if (auto previous = base::take(_undoText)) {
		replaceText(*previous);
	}
}

Ui::RpWidget *FieldHost::showPanel(object_ptr<Ui::RpWidget> panel) {
	if (const auto host = Compose::PanelHost::For(_field)) {
		return host->show(std::move(panel));
	}
	return nullptr; // Not attached: the panel is destroyed.
}

void FieldHost::hidePanel(not_null<Ui::RpWidget*> panel) {
	if (const auto host = Compose::PanelHost::For(_field)) {
		host->hide(panel);
	}
}

bool FieldHost::canPostInChannel() {
	const auto chat = history();
	const auto channel = chat ? chat->peer->asBroadcast() : nullptr;
	return channel && channel->canPostMessages();
}

void FieldHost::send(const TextWithTags &text, bool schedule) {
	const auto chat = history();
	if (!chat || text.text.trimmed().isEmpty()) {
		return;
	}
	if (!schedule) {
		sendNow(text, ::Api::SendOptions());
		return;
	}
	_show->showBox(HistoryView::PrepareScheduleBox(
		_field.get(),
		_show,
		SendMenu::Details{ .type = SendMenu::Type::Scheduled },
		crl::guard(this, [=](::Api::SendOptions options) {
			sendNow(text, options);
		})));
}

void FieldHost::sendNow(
		const TextWithTags &text,
		::Api::SendOptions options) {
	const auto chat = history();
	if (!chat) {
		return;
	}
	// As the channel when posting in one (upstream sends as the channel).
	auto message = ::Api::MessageToSend(::Api::SendAction(chat, options));
	message.textWithTags = text;
	chat->session().api().sendMessage(std::move(message));
	_undoText = std::nullopt;
	_field->clear();
}

bool FieldHost::busy() const {
	return _request != nullptr;
}

void FieldHost::showMenu(not_null<QWidget*> parent) {
	const auto hasText = !_field->getLastText().trimmed().isEmpty();
	const auto items = VisibleWritingItems(this, hasText);
	if (items.empty()) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	for (const auto &item : items) {
		const auto action = item.action;
		_menu->addAction(Tr(item.titleZh, item.titleEn), [=] {
			if (action) {
				action(this);
			}
		}, item.icon);
	}
	_menu->popup(QCursor::pos());
}

void FieldHost::runEffect(const Writing::Effect &effect) {
	const auto before = _field->getTextWithTags();
	const auto original = before.text;
	if (original.trimmed().isEmpty()) {
		return;
	}
	// 21 需求 4.
	if (original.size() > Writing::kMaxChars) {
		_show->showToast(IsChinese()
			? (u"文字太长（超过 %1 字），请缩短后再试"_q).arg(Writing::kMaxChars)
			: (u"The text is too long (over %1 characters). "
				"Please shorten it."_q).arg(Writing::kMaxChars));
		return;
	}
	if (const auto request = base::take(_request)) {
		request->cancel();
	}
	const auto chat = history();
	const auto doneZh = effect.doneZh;
	const auto doneEn = effect.doneEn;
	const auto id = QString::fromLatin1(effect.id);
	WithConsent(_show, crl::guard(this, [=] {
		auto args = ChatArgs();
		args.task = QString::fromLatin1(kTaskWriting);
		args.stream = true;
		args.messages.push_back({ u"user"_q, original });
		args.extra.insert(u"effect"_q, id);
		// 21 需求 2: the field shows the result as it is written, like
		// Android. Anything else changing the field (the user typing, a
		// switch to another chat) stops it.
		const auto stillOurs = [=] {
			return (history() == chat) && _request;
		};
		const auto written = std::make_shared<QString>(original);
		_request = Chat(
			std::move(args),
			crl::guard(this, [=](QString, QString soFar) {
				if (stillOurs()) {
					// One undo step for the whole stream (Ctrl+Z).
					*written = soFar;
					replaceText(
						{ soFar },
						Ui::InputField::HistoryAction::MergeEntry);
				}
			}),
			crl::guard(this, [=](QString text) {
				_requestLifetime.destroy();
				if (!stillOurs()) {
					_request = nullptr;
					if (chat && history() != chat) {
						RestoreDraft(chat, before, *written);
					}
					return;
				}
				_request = nullptr;
				const auto result = text.trimmed();
				if (result.isEmpty()) {
					replaceText(before);
					return;
				}
				replaceText(WithFormatting(before, result));
				_undoText = before;
				showUndoHint(Tr(doneZh, doneEn));
			}),
			crl::guard(this, [=](Api::Error error) {
				_requestLifetime.destroy();
				const auto ours = stillOurs();
				_request = nullptr;
				if (ours) {
					replaceText(before);
				} else if (chat && history() != chat) {
					RestoreDraft(chat, before, *written);
				}
				if (error.code != QLatin1String(Api::kCancelled)
					&& error.code != QLatin1String(Api::kConsentDenied)) {
					_show->showToast(Api::ErrorText(error.code));
				}
			}));
		_requestLifetime.destroy();
		_field->changes() | rpl::on_next([=] {
			if (_writing || !_request) {
				return;
			}
			if (const auto request = base::take(_request)) {
				request->cancel(); // No callback after this.
			}
			if (chat && history() != chat) {
				RestoreDraft(chat, before, *written);
			}
			crl::on_main(this, [=] {
				_requestLifetime.destroy();
			});
		}, _requestLifetime);
	}));
}

void FieldHost::hideUndoHint() {
	_undoHintTimer.cancel();
	if (const auto hint = _undoHint.data()) {
		hidePanel(hint);
	}
}

// 21 需求 3: "已润色 · 撤销" above the field.
void FieldHost::showUndoHint(const QString &done) {
	auto panel = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto row = panel->add(object_ptr<Ui::RpWidget>(panel.data()));
	row->resize(row->width(), st::historyReplyHeight);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		row,
		done,
		st::defaultFlatLabel);
	const auto undoButton = Ui::CreateChild<Ui::LinkButton>(
		row,
		Tr("撤销", "Undo"));
	row->paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(row);
		p.fillRect(clip, st::historyComposeAreaBg);
		p.fillRect(0, 0, row->width(), st::lineWidth, st::shadowFg);
	}, row->lifetime());
	row->widthValue() | rpl::on_next([=](int width) {
		const auto skip = st::historySendPadding * 2;
		const auto height = row->height();
		undoButton->moveToRight(
			skip,
			(height - undoButton->height()) / 2,
			width);
		label->resizeToWidth(
			std::max(0, width - 3 * skip - undoButton->width()));
		label->moveToLeft(skip, (height - label->height()) / 2, width);
	}, row->lifetime());
	undoButton->setClickedCallback(crl::guard(this, [=] {
		this->undo();
		hideUndoHint();
	}));
	_undoHint = showPanel(std::move(panel));
	_undoHintTimer.callOnce(kUndoHintMs);
}

} // namespace

void RegisterWritingDebugDemo();

bool HidesUpstreamAiButton() {
	return WritingEnabled();
}

rpl::producer<bool> HidesUpstreamAiButtonValue() {
	return Settings::EnabledValue(QString::fromLatin1(Features::kAiWriting));
}

Compose::Slot CreateWritingSlot(const Compose::SlotContext &context) {
	auto button = object_ptr<Ui::IconButton>(
		context.parent,
		st::historyLuckgramAi);
	const auto raw = button.data();
	// Found by the CI screenshots (luckgram_screenshots.cpp, D7 ✦ menu).
	raw->setObjectName(u"luckgram-ai-writing"_q);
	const auto host = raw->lifetime().make_state<FieldHost>(context);
	const auto field = context.field;
	raw->setClickedCallback([=] {
		if (!host->busy()) {
			host->showMenu(raw);
		}
	});
	auto wanted = rpl::combine(
		Settings::EnabledValue(QString::fromLatin1(Features::kAiWriting)),
		rpl::single(rpl::empty) | rpl::then(field->changes())
	) | rpl::map([=](bool enabled, auto&&) {
		// Android AiWritingController.shouldShowButton().
		return enabled
			&& (!field->getLastText().trimmed().isEmpty()
				|| HasWritingItemsForEmptyText(host));
	}) | rpl::distinct_until_changed();
	// specs#94 compact input bar: 「AI 写作」 in the 「+」 menu opens the
	// same ✦ menu (the button itself is hidden then).
	auto plus = [=](not_null<Ui::DropdownMenu*> menu) {
		if (!WritingEnabled()) {
			return;
		}
		menu->addAction(Tr("AI 写作", "AI writing"), [=] {
			if (!host->busy()) {
				host->showMenu(raw);
			}
		}, &st::menuIconCustomize);
	};
	return {
		.widget = std::move(button),
		.wanted = std::move(wanted),
		.plus = std::move(plus),
	};
}

void RegisterWritingEffects() {
	RegisterWritingDebugDemo();
	for (const auto &effect : Writing::Effects()) {
		const auto copy = effect;
		RegisterWritingItem({
			.id = QString::fromLatin1(effect.id),
			.titleZh = effect.titleZh,
			.titleEn = effect.titleEn,
			.icon = EffectIcon(QString::fromLatin1(effect.id)),
			.order = effect.order,
			.action = [=](not_null<WritingHost*> host) {
				if (const auto field = dynamic_cast<FieldHost*>(host.get())) {
					field->runEffect(copy);
				}
			},
		});
	}
}

void RegisterWritingDebugDemo() {
	// 21 验收: a test item first, only in channels, opening a test panel
	// that writes into the field with undo. Dev builds only.
	if (!Features::IsDevBuild()) {
		return;
	}
	RegisterWritingItem({
		.id = u"debug_panel"_q,
		.titleZh = "测试面板",
		.titleEn = "Test panel",
		.icon = &st::menuIconBot,
		.order = -1,
		.allowEmptyText = true,
		.visible = [](not_null<WritingHost*> host) {
			const auto chat = host->history();
			return chat && chat->peer->isBroadcast();
		},
		.action = [](not_null<WritingHost*> host) {
			auto panel = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
			const auto row = panel->add(
				object_ptr<Ui::RpWidget>(panel.data()));
			row->resize(row->width(), st::historyReplyHeight);
			const auto label = Ui::CreateChild<Ui::FlatLabel>(
				row,
				u"Debug panel"_q,
				st::defaultFlatLabel);
			const auto write = Ui::CreateChild<Ui::LinkButton>(
				row,
				u"写入 / Write"_q);
			const auto close = Ui::CreateChild<Ui::LinkButton>(
				row,
				u"✕"_q);
			row->paintRequest() | rpl::on_next([=](QRect clip) {
				QPainter(row).fillRect(clip, st::historyComposeAreaBg);
			}, row->lifetime());
			row->widthValue() | rpl::on_next([=](int width) {
				const auto skip = st::historySendPadding * 2;
				const auto top = [&](not_null<QWidget*> w) {
					return (row->height() - w->height()) / 2;
				};
				close->moveToRight(skip, top(close), width);
				write->moveToRight(
					skip * 2 + close->width(),
					top(write),
					width);
				label->moveToLeft(skip, top(label), width);
			}, row->lifetime());
			const auto raw = panel.data();
			write->setClickedCallback([=] {
				host->setText(
					{ u"Text written by the debug panel ✍️"_q },
					true);
			});
			// The panel lives in the field's PanelHost, which goes away with
			// the field together with the host.
			close->setClickedCallback([=] {
				host->hidePanel(raw);
			});
			host->showPanel(std::move(panel));
		},
	});
}

} // namespace Luckgram::AI
