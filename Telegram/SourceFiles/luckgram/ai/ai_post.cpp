/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_post.h"

#include "base/weak_ptr.h"
#include "chat_helpers/compose/compose_show.h"
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
#include "ui/text/text_entity.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace Luckgram::AI::Post {
namespace {

constexpr auto kDraftMaxHeight = 200;

[[nodiscard]] bool Enabled() {
	return Settings::IsEnabled(QString::fromLatin1(Features::kAiPost));
}

[[nodiscard]] int MaxInputChars() {
	return std::max(
		1000,
		int(Settings::RemoteNumber(
			u"ai.max_input_chars"_q,
			kDefaultMaxInputChars)));
}

// 24 需求 2: the channel's recent text posts, newest first. Telegram
// Desktop keeps no message database on disk: the loaded ones.
[[nodiscard]] QStringList RecentPosts(History *history) {
	auto result = QStringList();
	if (!history) {
		return result;
	}
	for (auto b = history->blocks.rbegin(); b != history->blocks.rend(); ++b) {
		const auto &messages = (*b)->messages;
		for (auto m = messages.rbegin(); m != messages.rend(); ++m) {
			const auto item = (*m)->data();
			if (item->isService() || !item->isRegular()) {
				continue;
			}
			const auto text = item->originalText().text;
			if (!text.trimmed().isEmpty()) {
				result.push_back(text);
				if (result.size() >= kExamples) {
					return result;
				}
			}
		}
	}
	return result;
}

struct State {
	~State() {
		if (request) {
			request->cancel();
		}
	}

	Api::RequestPtr request;
	QString examples;
	bool busy = false;
	base::unique_qptr<Ui::PopupMenu> menu;
};

void ShowPanel(not_null<WritingHost*> host) {
	auto owned = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto panel = owned.data();
	const auto state = panel->lifetime().make_state<State>();
	const auto skip = st::historySendPadding * 2;
	const auto margins = QMargins(skip, skip / 2, skip, skip / 2);
	// w-1139: callbacks run only while the host is there. FieldHost is a
	// base::has_weak_ptr (and WritingHost itself with #56).
	const auto weakHost = base::make_weak(
		dynamic_cast<base::has_weak_ptr*>(host.get()));
	const auto alive = [=]() -> WritingHost* {
		return weakHost.get() ? host.get() : nullptr;
	};

	panel->paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(panel);
		p.fillRect(clip, st::historyComposeAreaBg);
		p.fillRect(0, 0, panel->width(), st::lineWidth, st::shadowFg);
	}, panel->lifetime());

	// Header: 「写帖子」 and ✕.
	const auto header = panel->add(
		object_ptr<Ui::RpWidget>(panel),
		margins);
	const auto title = Ui::CreateChild<Ui::FlatLabel>(
		header,
		Tr("写帖子", "Write a post"),
		st::defaultFlatLabel);
	const auto close = Ui::CreateChild<Ui::LinkButton>(
		header,
		QString::fromUtf8("✕"));
	header->resize(
		header->width(),
		std::max(title->height(), close->height()));
	header->widthValue() | rpl::on_next([=](int width) {
		close->moveToRight(0, 0, width);
		title->resizeToWidth(std::max(1, width - close->width() - skip));
		title->moveToLeft(0, 0, width);
	}, header->lifetime());

	// Topic and 「生成」.
	const auto topicRow = panel->add(
		object_ptr<Ui::RpWidget>(panel),
		margins);
	const auto topic = Ui::CreateChild<Ui::InputField>(
		topicRow,
		st::defaultInputField,
		Ui::InputField::Mode::SingleLine,
		rpl::single(Tr(
			"主题，例如：周末促销，玫瑰八折",
			"Topic, e.g. Weekend sale, roses 20% off")));
	const auto generate = Ui::CreateChild<Ui::LinkButton>(
		topicRow,
		Tr("生成", "Write"));
	{
		const auto current = host->text().text.trimmed();
		if (!current.isEmpty()) {
			topic->setText(current);
		}
	}
	rpl::combine(
		topicRow->widthValue(),
		topic->heightValue()
	) | rpl::on_next([=](int width, int height) {
		topic->resizeToWidth(
			std::max(1, width - generate->width() - skip));
		topic->moveToLeft(0, 0, width);
		generate->moveToRight(0, (height - generate->height()) / 2, width);
		if (topicRow->height() != height) {
			topicRow->resize(width, height);
		}
	}, topicRow->lifetime());

	// Status: 「正在写…」 or the error.
	const auto status = panel->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			panel,
			object_ptr<Ui::FlatLabel>(
				panel,
				QString(),
				st::defaultFlatLabel),
			margins));
	status->hide(anim::type::instant);

	// The draft (editable) and what can be done with it.
	const auto draftWrap = panel->add(
		object_ptr<Ui::SlideWrap<Ui::InputField>>(
			panel,
			object_ptr<Ui::InputField>(
				panel,
				st::defaultInputField,
				Ui::InputField::Mode::MultiLine,
				rpl::single(Tr("草稿", "Draft"))),
			margins));
	const auto draft = draftWrap->entity();
	draft->setMaxHeight(style::ConvertScale(kDraftMaxHeight));
	draftWrap->hide(anim::type::instant);

	const auto editWrap = panel->add(
		object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
			panel,
			object_ptr<Ui::RpWidget>(panel),
			margins));
	const auto editRow = editWrap->entity();
	const auto shorter = Ui::CreateChild<Ui::LinkButton>(
		editRow,
		Tr("更简短", "Shorter"));
	const auto emoji = Ui::CreateChild<Ui::LinkButton>(
		editRow,
		Tr("多点表情", "More emoji"));
	const auto translate = Ui::CreateChild<Ui::LinkButton>(
		editRow,
		Tr("翻译", "Translate"));
	const auto rewrite = Ui::CreateChild<Ui::LinkButton>(
		editRow,
		Tr("重写", "Rewrite"));
	const auto edits = std::vector<not_null<Ui::LinkButton*>>{
		shorter,
		emoji,
		translate,
		rewrite,
	};
	editRow->resize(editRow->width(), shorter->height());
	editRow->widthValue() | rpl::on_next([=](int width) {
		auto x = 0;
		for (const auto &button : edits) {
			button->moveToLeft(x, 0, width);
			x += button->width() + skip;
		}
	}, editRow->lifetime());
	editWrap->hide(anim::type::instant);

	const auto postWrap = panel->add(
		object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
			panel,
			object_ptr<Ui::RpWidget>(panel),
			margins));
	const auto postRow = postWrap->entity();
	const auto schedule = Ui::CreateChild<Ui::LinkButton>(
		postRow,
		Tr("定时发布", "Schedule"));
	const auto publish = Ui::CreateChild<Ui::RoundButton>(
		postRow,
		rpl::single(Tr("发布", "Post")),
		st::defaultActiveButton);
	publish->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	postRow->resize(postRow->width(), publish->height());
	postRow->widthValue() | rpl::on_next([=](int width) {
		publish->moveToRight(0, 0, width);
		schedule->moveToRight(
			publish->width() + skip,
			(publish->height() - schedule->height()) / 2,
			width);
	}, postRow->lifetime());
	postWrap->hide(anim::type::instant);

	const auto setStatus = [=](const QString &text, bool error) {
		status->entity()->setText(text);
		status->entity()->setTextColorOverride(error
			? std::make_optional(st::attentionButtonFg->c)
			: std::make_optional(st::windowSubTextFg->c));
		status->toggle(!text.isEmpty(), anim::type::instant);
	};
	const auto setBusy = [=](bool busy) {
		state->busy = busy;
		generate->setDisabled(busy);
		for (const auto &button : edits) {
			button->setDisabled(busy);
		}
		schedule->setDisabled(busy);
		publish->setDisabled(busy);
		draft->setEnabled(!busy);
	};
	const auto showDraft = [=](const QString &text) {
		draftWrap->show(anim::type::instant);
		editWrap->show(anim::type::instant);
		postWrap->show(anim::type::instant);
		draft->setText(text);
		draft->setCursorPosition(int(text.size()));
	};
	const auto run = [=](const QString &content) {
		if (state->request) {
			state->request->cancel();
			state->request = nullptr;
		}
		setStatus(Tr("正在写…", "Writing…"), false);
		setBusy(true);
		auto args = ChatArgs();
		args.task = QString::fromLatin1(kTaskPost);
		args.messages.push_back({ u"user"_q, content });
		args.stream = true;
		state->request = Chat(
			std::move(args),
			crl::guard(panel, [=](QString, QString textSoFar) {
				showDraft(textSoFar);
			}),
			crl::guard(panel, [=](QString text) {
				state->request = nullptr;
				setBusy(false);
				setStatus(QString(), false);
				if (!text.trimmed().isEmpty()) {
					showDraft(text.trimmed());
				}
			}),
			crl::guard(panel, [=](Api::Error error) {
				state->request = nullptr;
				setBusy(false);
				if (error.code == QLatin1String(Api::kConsentDenied)) {
					setStatus(QString(), false);
				} else {
					setStatus(error.text(), true);
				}
			}));
	};
	// 00: the consent prompt before anything is sent.
	const auto ask = [=](const QString &content) {
		const auto strong = alive();
		if (!strong) {
			return;
		}
		WithConsent(strong->show(), crl::guard(panel, [=] {
			run(content);
		}));
	};
	const auto startGenerate = [=] {
		const auto text = topic->getLastText().trimmed();
		const auto strong = alive();
		if (text.isEmpty()) {
			topic->showError();
			return;
		} else if (!strong) {
			return;
		}
		// 24 需求 5: the example posts go only into this request.
		state->examples = Examples(
			RecentPosts(strong->history()),
			MaxInputChars() / 2);
		ask(Post::Generate(state->examples, text));
	};
	// 24 需求 3: every edit is based on the current draft.
	const auto edit = [=](const QString &instruction) {
		const auto text = draft->getLastText();
		if (text.trimmed().isEmpty() || state->busy) {
			return;
		}
		ask(Edit(state->examples, text, instruction));
	};
	generate->setClickedCallback(startGenerate);
	topic->submits() | rpl::on_next(startGenerate, topic->lifetime());
	shorter->setClickedCallback([=] {
		edit(QString::fromLatin1(kShorter));
	});
	emoji->setClickedCallback([=] {
		edit(QString::fromLatin1(kEmoji));
	});
	rewrite->setClickedCallback([=] {
		edit(QString::fromLatin1(kRewrite));
	});
	translate->setClickedCallback([=] {
		if (state->busy) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			panel,
			st::popupMenuWithIcons);
		for (const auto &language : Languages()) {
			const auto name = QString::fromLatin1(language.name);
			state->menu->addAction(
				QString::fromUtf8(language.label),
				[=] { edit(Translate(name)); });
		}
		state->menu->popup(QCursor::pos());
	});
	// Not from inside the clicked button of the panel itself.
	const auto hideLater = [=] {
		crl::on_main(panel, [=] {
			if (const auto strong = alive()) {
				strong->hidePanel(panel);
			}
		});
	};
	// 24 需求 4: through the field's own send path, as the admin's Send.
	const auto post = [=](bool scheduled) {
		const auto text = draft->getLastText().trimmed();
		const auto strong = alive();
		if (text.isEmpty() || state->busy || !strong) {
			return;
		}
		strong->send(TextWithTags{ text }, scheduled);
		if (!scheduled) {
			hideLater();
		}
	};
	publish->setClickedCallback([=] { post(false); });
	schedule->setClickedCallback([=] { post(true); });
	close->setClickedCallback([=] {
		// The request is cancelled with the panel (State).
		hideLater();
	});

	host->showPanel(std::move(owned));
	topic->setFocus();
}

} // namespace

void Register() {
	RegisterWritingItem({
		.id = u"ai_post"_q,
		.titleZh = "写帖子",
		.titleEn = "Write a post",
		.icon = &st::menuIconEdit,
		.order = kMenuOrder,
		.allowEmptyText = true,
		// 24 需求 1: only in channels where the user can post.
		.visible = [](not_null<WritingHost*> host) {
			return Enabled() && host->canPostInChannel();
		},
		.action = ShowPanel,
	});
}

} // namespace Luckgram::AI::Post
