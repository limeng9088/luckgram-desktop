/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/discover/luckgram_square_ui.h"

#include "base/event_filter.h"
#include "base/unique_qptr.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/file_utilities.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "luckgram/discover/luckgram_discover_logic.h"
#include "luckgram/discover/luckgram_silent_link.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonDocument>
#include <QtCore/QPointer>
#include <QtGui/QCursor>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainterPath>

#include <cmath>

namespace Luckgram::SquareUi {
namespace {

constexpr auto kImageCacheMax = 200;
constexpr auto kBlockedNamesKey = "discover_blocked_names";

struct Shared {
	rpl::event_stream<Square::Post> postUpdated;
	rpl::event_stream<Square::Post> postCreated;
	rpl::event_stream<QString> postRemoved;
	rpl::event_stream<QString> authorBlocked;
	QString myAuthorId;
	QJsonObject profile;
	bool profileKnown = false;
	QHash<QString, QImage> images;
	QStringList imageOrder;
	QHash<QString, std::vector<Fn<void(QImage)>>> loading;
};

[[nodiscard]] Shared &Data() {
	static auto result = Shared();
	return result;
}

[[nodiscard]] QString L(const char *text) {
	return QString::fromLatin1(text);
}

[[nodiscard]] qint64 NowMs() {
	return QDateTime::currentMSecsSinceEpoch();
}

[[nodiscard]] QByteArray JsonBody(const QJsonObject &o) {
	return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// Callbacks run only while the window lives.
Api::RequestPtr Request(
		not_null<Window::SessionController*> controller,
		QByteArray method,
		QString path,
		std::optional<QJsonObject> body,
		Fn<void(QJsonObject)> done,
		Fn<void(Api::Error)> fail) {
	auto args = Api::RequestArgs{
		.method = std::move(method),
		.path = std::move(path),
	};
	if (body) {
		args.body = JsonBody(*body);
		args.contentType = "application/json";
	}
	const auto weak = base::make_weak(controller);
	return Api::Send(std::move(args), [=](Api::Response response) {
		if (done && weak.get()) {
			done(response.json());
		}
	}, [=](Api::Error error) {
		if (fail && weak.get()) {
			fail(error);
		}
	});
}

[[nodiscard]] bool IsNoProfile(const Api::Error &error) {
	return (error.code == u"not_found"_q)
		|| (error.code == u"profile_required"_q)
		|| (error.status == 404);
}

void SetProfile(const QJsonObject &profile) {
	auto &data = Data();
	data.profile = profile;
	data.profileKnown = true;
	const auto id = profile.value(u"id"_q).toString();
	if (!id.isEmpty()) {
		data.myAuthorId = id;
	}
}

[[nodiscard]] QString MyUsername(not_null<Window::SessionController*> controller) {
	return controller->session().user()->username();
}

class Userpic final : public Ui::RpWidget {
public:
	Userpic(QWidget *parent, Square::Author author, int size)
	: RpWidget(parent)
	, _author(std::move(author)) {
		resize(size, size);
		if (!_author.avatarUrl.isEmpty()) {
			LoadImage(_author.avatarUrl, this, [=](QImage image) {
				_image = std::move(image);
				update();
			});
		}
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto r = rect();
		auto path = QPainterPath();
		path.addEllipse(r);
		if (!_image.isNull()) {
			p.setClipPath(path);
			const auto scaled = _image.scaled(
				r.size() * style::DevicePixelRatio(),
				Qt::KeepAspectRatioByExpanding,
				Qt::SmoothTransformation);
			p.drawImage(r, scaled, QRect(
				(scaled.width() - r.width() * style::DevicePixelRatio()) / 2,
				(scaled.height() - r.height() * style::DevicePixelRatio()) / 2,
				r.width() * style::DevicePixelRatio(),
				r.height() * style::DevicePixelRatio()));
			return;
		}
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		p.drawEllipse(r);
		const auto name = _author.displayName.trimmed();
		if (!name.isEmpty()) {
			p.setPen(st::windowFgActive);
			p.setFont(st::semiboldFont);
			p.drawText(r, style::al_center, name.left(1).toUpper());
		}
	}

private:
	const Square::Author _author;
	QImage _image;

};

// Photos (3 per row, 2 for 2 or 4) or one video cover with play and
// duration; a click opens the full image or the video.
class MediaGrid final : public Ui::RpWidget {
public:
	MediaGrid(QWidget *parent, const Square::Post &post)
	: RpWidget(parent) {
		if (post.video) {
			_video = true;
			_duration = post.video->durationSeconds;
			_aspect = (post.video->width > 0 && post.video->height > 0)
				? double(post.video->height) / post.video->width
				: 9. / 16.;
			_cells.push_back({ post.video->coverUrl, post.video->url });
		} else {
			for (const auto &image : post.images) {
				_cells.push_back({ image.url, image.url });
			}
			if (post.images.size() == 1 && post.images[0].width > 0) {
				_aspect = double(post.images[0].height)
					/ post.images[0].width;
			}
		}
		for (auto i = 0; i != int(_cells.size()); ++i) {
			if (_cells[i].url.isEmpty()) {
				continue;
			}
			LoadImage(_cells[i].url, this, [=](QImage image) {
				_cells[i].image = std::move(image);
				update();
			});
		}
		setCursor(style::cur_pointer);
	}

	[[nodiscard]] bool empty() const {
		return _cells.empty();
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto gap = style::ConvertScale(4);
		const auto max = std::min(newWidth, style::ConvertScale(420));
		const auto count = int(_cells.size());
		if (!count) {
			return 0;
		}
		if (count == 1) {
			const auto aspect = std::clamp(_aspect, 0.5, 1.33);
			const auto h = int(std::round(max * aspect));
			_cells[0].rect = QRect(0, 0, max, h);
			return h;
		}
		const auto columns = (count == 2 || count == 4) ? 2 : 3;
		const auto cell = (max - gap * (columns - 1)) / columns;
		for (auto i = 0; i != count; ++i) {
			_cells[i].rect = QRect(
				(i % columns) * (cell + gap),
				(i / columns) * (cell + gap),
				cell,
				cell);
		}
		const auto rows = (count + columns - 1) / columns;
		return rows * cell + (rows - 1) * gap;
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = style::ConvertScale(6);
		for (const auto &cell : _cells) {
			auto path = QPainterPath();
			path.addRoundedRect(cell.rect, radius, radius);
			p.save();
			p.setClipPath(path);
			if (cell.image.isNull()) {
				p.fillRect(cell.rect, st::windowBgOver);
			} else {
				const auto ratio = style::DevicePixelRatio();
				const auto scaled = cell.image.scaled(
					cell.rect.size() * ratio,
					Qt::KeepAspectRatioByExpanding,
					Qt::SmoothTransformation);
				p.drawImage(cell.rect, scaled, QRect(
					(scaled.width() - cell.rect.width() * ratio) / 2,
					(scaled.height() - cell.rect.height() * ratio) / 2,
					cell.rect.width() * ratio,
					cell.rect.height() * ratio));
			}
			p.restore();
		}
		if (_video && !_cells.empty()) {
			const auto r = _cells[0].rect;
			const auto size = style::ConvertScale(44);
			const auto circle = QRect(
				r.x() + (r.width() - size) / 2,
				r.y() + (r.height() - size) / 2,
				size,
				size);
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(0, 0, 0, 128));
			p.drawEllipse(circle);
			auto triangle = QPainterPath();
			const auto c = circle.center();
			const auto t = size / 5;
			triangle.moveTo(c.x() - t + t / 3, c.y() - t);
			triangle.lineTo(c.x() + t + t / 3, c.y());
			triangle.lineTo(c.x() - t + t / 3, c.y() + t);
			triangle.closeSubpath();
			p.setBrush(Qt::white);
			p.drawPath(triangle);
			const auto duration = Square::FormatDuration(_duration);
			p.setFont(st::normalFont);
			const auto w = st::normalFont->width(duration)
				+ style::ConvertScale(12);
			const auto h = st::normalFont->height + style::ConvertScale(4);
			const auto badge = QRect(
				r.x() + r.width() - w - style::ConvertScale(6),
				r.y() + r.height() - h - style::ConvertScale(6),
				w,
				h);
			p.setBrush(QColor(0, 0, 0, 128));
			p.drawRoundedRect(badge, h / 2, h / 2);
			p.setPen(Qt::white);
			p.drawText(badge, style::al_center, duration);
		}
	}

	void mousePressEvent(QMouseEvent *e) override {
		_pressed = cellAt(e->pos());
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		const auto index = cellAt(e->pos());
		if (index >= 0 && index == base::take(_pressed)) {
			// Full image / the video with sound in the default app.
			File::OpenUrl(_cells[index].openUrl);
		}
	}

private:
	struct Cell {
		QString url;
		QString openUrl;
		QImage image;
		QRect rect;
	};

	[[nodiscard]] int cellAt(QPoint point) const {
		for (auto i = 0; i != int(_cells.size()); ++i) {
			if (_cells[i].rect.contains(point)) {
				return i;
			}
		}
		return -1;
	}

	std::vector<Cell> _cells;
	bool _video = false;
	int _duration = 0;
	double _aspect = 1.;
	int _pressed = -1;

};

// Photo, name and time; a click opens the author.
class PostHeader final : public Ui::AbstractButton {
public:
	PostHeader(QWidget *parent, const Square::Post &post)
	: AbstractButton(parent)
	, _name(post.author.displayName)
	, _time(Tx().timeAgo(post.createdAtMs, NowMs()))
	, _userpic(Ui::CreateChild<Userpic>(
		this,
		post.author,
		style::ConvertScale(40))) {
		_userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
		resize(width(), style::ConvertScale(48));
	}

protected:
	int resizeGetHeight(int newWidth) override {
		_userpic->moveToLeft(0, style::ConvertScale(4), newWidth);
		return style::ConvertScale(48);
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		const auto left = style::ConvertScale(52);
		const auto available = width() - left - style::ConvertScale(36);
		p.setPen(st::windowBoldFg);
		p.setFont(st::semiboldFont);
		p.drawText(
			left,
			style::ConvertScale(6) + st::semiboldFont->ascent,
			st::semiboldFont->elided(_name, available));
		p.setPen(st::windowSubTextFg);
		p.setFont(st::normalFont);
		p.drawText(
			left,
			style::ConvertScale(26) + st::normalFont->ascent,
			_time);
	}

private:
	const QString _name;
	const QString _time;
	const not_null<Userpic*> _userpic;

};

void AddFooterRow(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		std::shared_ptr<Square::Post> post,
		bool full) {
	const auto row = container->add(
		object_ptr<Ui::RpWidget>(container),
		style::margins(0, style::ConvertScale(6), 0, style::ConvertScale(6)));
	row->resize(row->width(), st::defaultLinkButton.font->height);
	const auto like = Ui::CreateChild<Ui::LinkButton>(row, QString());
	const auto comment = Ui::CreateChild<Ui::LinkButton>(row, QString());
	const auto refresh = [=] {
		const auto tx = Tx();
		like->setText((post->liked ? u"♥ "_q : u"♡ "_q)
			+ tx.likes(post->likeCount));
		comment->setText(tx.comments(post->commentCount));
		comment->moveToLeft(like->width() + style::ConvertScale(24), 0);
	};
	refresh();
	like->moveToLeft(0, 0);
	like->setClickedCallback([=] {
		ToggleLike(controller, *post);
	});
	comment->setClickedCallback([=] {
		if (!full) {
			ShowPost(controller, *post);
		}
	});
	PostUpdated() | rpl::filter([=](const Square::Post &updated) {
		return updated.id == post->id;
	}) | rpl::on_next([=](const Square::Post &updated) {
		post->likeCount = updated.likeCount;
		post->liked = updated.liked;
		post->commentCount = updated.commentCount;
		refresh();
	}, row->lifetime());
}

void ShowPostMenu(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		const Square::Post &post) {
	const auto menu = parent->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();
	*menu = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto tx = Tx();
	if (Square::CanDeletePost(post, MyAuthorId())) {
		(*menu)->addAction(tx.deleteText(), [=] {
			DeletePost(controller, post);
		}, &st::menuIconDelete);
	}
	(*menu)->addAction(tx.reportPost(), [=] {
		Report(controller, u"post"_q, post.id);
	}, &st::menuIconReport);
	if (post.author.id != MyAuthorId()) {
		const auto author = post.author;
		(*menu)->addAction(tx.reportAuthor(), [=] {
			Report(controller, u"user"_q, author.id);
		}, &st::menuIconReport);
		(*menu)->addAction(tx.block(), [=] {
			Block(controller, author);
		}, &st::menuIconBlock);
	}
	(*menu)->popup(QCursor::pos());
}

void AddCommentRow(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		const Square::Comment &comment,
		const Square::Post &post,
		Fn<void(Square::Comment)> reply,
		Fn<void(Square::Comment)> remove) {
	const auto tx = Tx();
	const auto indent = comment.depth ? style::ConvertScale(24) : 0;
	auto text = Ui::Text::Bold(comment.author.displayName);
	text.append(u"  "_q).append(comment.status == L(Square::kStatusHidden)
		? tx.hidden()
		: comment.text);
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(container, rpl::single(text), st::boxLabel),
		style::margins(indent, style::ConvertScale(6), 0, 0));
	label->setSelectable(true);
	const auto row = container->add(
		object_ptr<Ui::RpWidget>(container),
		style::margins(indent, style::ConvertScale(2), 0, 0));
	row->resize(row->width(), st::defaultLinkButton.font->height);
	auto left = 0;
	const auto time = Ui::CreateChild<Ui::FlatLabel>(
		row,
		tx.timeAgo(comment.createdAtMs, NowMs()),
		st::boxDividerLabel);
	time->resizeToNaturalWidth(style::ConvertScale(200));
	time->moveToLeft(left, 0);
	left += time->width() + style::ConvertScale(16);
	const auto addLink = [&](const QString &title, Fn<void()> callback) {
		const auto link = Ui::CreateChild<Ui::LinkButton>(row, title);
		link->moveToLeft(left, 0);
		link->setClickedCallback(std::move(callback));
		left += link->width() + style::ConvertScale(16);
	};
	addLink(tx.reply(), [=] { reply(comment); });
	if (Square::CanDeleteComment(comment, &post, MyAuthorId())) {
		addLink(tx.deleteText(), [=] { remove(comment); });
	}
	if (comment.author.id != MyAuthorId()) {
		const auto id = comment.id;
		addLink(tx.report(), [=] { Report(controller, u"comment"_q, id); });
	}
}

void ProfileBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		QJsonObject current,
		Fn<void()> saved) {
	const auto tx = Tx();
	box->setTitle(rpl::single(tx.profileTitle()));
	if (current.value(u"display_name"_q).toString().isEmpty()) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(tx.profileNeeded()),
			st::boxLabel));
	}
	const auto name = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::SingleLine,
		rpl::single(tx.displayName()),
		current.value(u"display_name"_q).toString()));
	name->setMaxLength(64);
	const auto bio = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine,
		rpl::single(tx.bio()),
		current.value(u"bio"_q).toString()));
	bio->setMaxLength(Square::kProfileBioMax);
	const auto username = MyUsername(controller);
	Ui::Checkbox *show = nullptr;
	if (!username.isEmpty()) {
		show = box->addRow(object_ptr<Ui::Checkbox>(
			box,
			tx.useMyUsername(username),
			!current.value(u"username"_q).toString().isEmpty()
				|| current.isEmpty()));
	}
	const auto saving = box->lifetime().make_state<bool>(false);
	box->setFocusCallback([=] { name->setFocusFast(); });
	box->addButton(rpl::single(tx.save()), [=] {
		if (*saving) {
			return;
		}
		const auto displayName = name->getLastText().trimmed();
		if (displayName.isEmpty()) {
			name->showError();
			return;
		}
		auto body = QJsonObject();
		body.insert(u"display_name"_q, displayName);
		body.insert(u"bio"_q, bio->getLastText().trimmed());
		body.insert(
			u"username"_q,
			(show && show->checked()) ? username : QString());
		*saving = true;
		const auto weak = QPointer<Ui::GenericBox>(box.get());
		Request(controller, "PUT", u"/v1/square/profile"_q, body, [=](QJsonObject result) {
			SetProfile(result.value(u"display_name"_q).toString().isEmpty()
				? body
				: result);
			controller->showToast(Tx().saved());
			if (weak) {
				weak->closeBox();
			}
			if (saved) {
				saved();
			}
		}, [=](Api::Error error) {
			*saving = false;
			ShowError(controller, error);
		});
	});
	box->addButton(rpl::single(tx.cancel()), [=] { box->closeBox(); });
}

void ReportBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		QString target,
		QString id) {
	const auto tx = Tx();
	box->setTitle(rpl::single(tx.reportTitle()));
	const auto container = box->verticalLayout();
	for (const auto &reason : Square::ReportReasons()) {
		const auto button = container->add(object_ptr<Ui::SettingsButton>(
			container,
			rpl::single(tx.reportReason(reason)),
			st::settingsButtonNoIcon));
		button->setClickedCallback([=] {
			box->closeBox();
			Request(
				controller,
				"POST",
				u"/v1/square/report"_q,
				Square::ReportBody(target, id, reason),
				[=](QJsonObject) { controller->showToast(Tx().reported()); },
				[=](Api::Error error) { ShowError(controller, error); });
		});
	}
	box->addButton(rpl::single(tx.cancel()), [=] { box->closeBox(); });
}

void PostBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Square::Post post) {
	struct State {
		Square::Post post;
		std::vector<Square::Comment> comments;
		QString cursor;
		bool loading = false;
		bool loaded = false;
		QString replyParent;
		rpl::variable<QString> replyName;
		Api::RequestPtr request;
		Ui::VerticalLayout *list = nullptr;
		Fn<void()> rebuild;
		Fn<void()> load;
	};
	const auto state = box->lifetime().make_state<State>();
	state->post = post;
	const auto tx = Tx();
	box->setWidth(st::boxWideWidth);
	box->setTitle(rpl::single(tx.title()));
	const auto container = box->verticalLayout();
	container->add(
		CreatePostWidget(container, controller, post, true),
		st::boxRowPadding);
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rpl::single(tx.commentsHeader()));
	state->list = container->add(
		object_ptr<Ui::VerticalLayout>(container),
		st::boxRowPadding);

	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine,
		state->replyName.value() | rpl::map([=](const QString &name) {
			return name.isEmpty() ? Tx().commentHint() : Tx().replyingTo(name);
		})));
	field->setMaxLength(Square::kCommentMax);

	const auto reply = [=](Square::Comment comment) {
		state->replyParent = Square::ReplyParentId(state->comments, comment);
		state->replyName = comment.author.displayName;
		field->setFocusFast();
	};
	const auto remove = [=](Square::Comment comment) {
		controller->show(Ui::MakeConfirmBox({
			.text = Tx().deleteCommentConfirm(),
			.confirmed = [=](Fn<void()> close) {
				close();
				Request(
					controller,
					"DELETE",
					u"/v1/square/comments/"_q + Square::PathId(comment.id),
					std::nullopt,
					crl::guard(box.get(), [=](QJsonObject) {
						auto &list = state->comments;
						const auto before = int(list.size());
						list.erase(std::remove_if(list.begin(), list.end(), [&](
								const Square::Comment &c) {
							return (c.id == comment.id)
								|| (c.parentId == comment.id);
						}), list.end());
						auto updated = state->post;
						updated.commentCount = std::max(
							0,
							updated.commentCount - (before - int(list.size())));
						state->post = updated;
						NotifyPostUpdated(updated);
						state->rebuild();
						controller->showToast(Tx().deleted());
					}),
					[=](Api::Error error) { ShowError(controller, error); });
			},
			.confirmText = Tx().deleteText(),
			.confirmStyle = &st::attentionBoxButton,
		}));
	};
	state->rebuild = [=] {
		const auto list = state->list;
		list->clear();
		const auto threaded = Square::Thread(state->comments);
		if (threaded.empty()) {
			list->add(object_ptr<Ui::FlatLabel>(
				list,
				rpl::single(state->loaded
					? Tx().noComments()
					: Tx().commentsLoading()),
				st::boxDividerLabel));
		}
		for (const auto &comment : threaded) {
			AddCommentRow(list, controller, comment, state->post, reply, remove);
		}
		if (!state->cursor.isEmpty()) {
			const auto more = list->add(
				object_ptr<Ui::LinkButton>(list, Tx().loadMore()),
				style::margins(0, style::ConvertScale(8), 0, 0));
			more->setClickedCallback([=] { state->load(); });
		}
		list->resizeToWidth(list->width());
	};
	state->load = [=] {
		if (state->loading) {
			return;
		}
		state->loading = true;
		const auto path = Square::WithCursor(
			u"/v1/square/posts/"_q + Square::PathId(post.id) + u"/comments"_q,
			state->cursor);
		state->request = Request(controller, "GET", path, std::nullopt, [=](
				QJsonObject json) {
			const auto page = Square::ParseComments(json);
			state->comments.insert(
				state->comments.end(),
				page.items.begin(),
				page.items.end());
			state->cursor = page.nextCursor;
			state->loading = false;
			state->loaded = true;
			state->rebuild();
		}, [=](Api::Error error) {
			state->loading = false;
			state->loaded = true;
			state->rebuild();
			ShowError(controller, error);
		});
	};
	state->rebuild();
	state->load();

	const auto send = [=] {
		const auto text = field->getLastText().trimmed();
		if (!Square::IsValidComment(text)) {
			field->showError();
			return;
		}
		RequireProfile(controller, crl::guard(box.get(), [=] {
			Request(
				controller,
				"POST",
				u"/v1/square/posts/"_q + Square::PathId(post.id) + u"/comments"_q,
				Square::CommentBody(text, state->replyParent),
				crl::guard(box.get(), [=](QJsonObject json) {
					auto comment = Square::ParseComment(json);
					if (comment.id.isEmpty()) {
						return;
					}
					state->comments.push_back(comment);
					auto updated = state->post;
					++updated.commentCount;
					state->post = updated;
					NotifyPostUpdated(updated);
					field->clear();
					state->replyParent = QString();
					state->replyName = QString();
					state->rebuild();
					controller->showToast(Tx().commentSent());
				}),
				[=](Api::Error error) { ShowError(controller, error); });
		}));
	};
	box->addButton(rpl::single(tx.send()), send);
	box->addLeftButton(rpl::single(tx.cancelReply()), [=] {
		state->replyParent = QString();
		state->replyName = QString();
	});
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });

	PostRemoved() | rpl::filter([=](const QString &id) {
		return id == post.id;
	}) | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());
	AuthorBlocked() | rpl::filter([=](const QString &id) {
		return id == post.author.id;
	}) | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());
	box->boxClosing() | rpl::on_next([=] {
		if (const auto request = base::take(state->request)) {
			request->cancel();
		}
	}, box->lifetime());
}

void AuthorBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Square::Author author) {
	struct State {
		std::vector<Square::Post> posts;
		QString cursor;
		bool loading = false;
		bool loaded = false;
		Api::RequestPtr request;
		Ui::VerticalLayout *list = nullptr;
		Fn<void()> rebuild;
		Fn<void()> load;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto tx = Tx();
	box->setWidth(st::boxWideWidth);
	box->setTitle(rpl::single(author.displayName));
	const auto container = box->verticalLayout();

	const auto card = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::boxRowPadding);
	const auto size = style::ConvertScale(64);
	card->resize(card->width(), size);
	Ui::CreateChild<Userpic>(card, author, size)->moveToLeft(0, 0);
	auto about = Ui::Text::Bold(author.displayName);
	if (!author.username.isEmpty()) {
		about.append(u"\n@"_q + author.username);
	}
	if (!author.bio.isEmpty()) {
		about.append(u"\n"_q + author.bio);
	}
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		card,
		rpl::single(about),
		st::boxLabel);
	card->widthValue() | rpl::on_next([=](int width) {
		const auto left = size + style::ConvertScale(12);
		label->resizeToWidth(std::max(width - left, 1));
		label->moveToLeft(left, 0);
		card->resize(width, std::max(size, label->height()));
	}, card->lifetime());

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rpl::single(tx.authorPosts()));
	state->list = container->add(
		object_ptr<Ui::VerticalLayout>(container),
		st::boxRowPadding);

	state->rebuild = [=] {
		const auto list = state->list;
		list->clear();
		if (state->posts.empty()) {
			list->add(object_ptr<Ui::FlatLabel>(
				list,
				rpl::single(state->loaded ? tx.empty() : tx.loading()),
				st::boxDividerLabel));
		}
		for (const auto &post : state->posts) {
			list->add(CreatePostWidget(list, controller, post, false));
		}
		if (!state->cursor.isEmpty()) {
			const auto more = list->add(
				object_ptr<Ui::LinkButton>(list, tx.loadMore()),
				style::margins(0, style::ConvertScale(8), 0, 0));
			more->setClickedCallback([=] { state->load(); });
		}
		list->resizeToWidth(list->width());
	};
	state->load = [=] {
		if (state->loading) {
			return;
		}
		state->loading = true;
		const auto path = Square::WithCursor(
			u"/v1/square/users/"_q + Square::PathId(author.id) + u"/posts"_q,
			state->cursor);
		state->request = Request(controller, "GET", path, std::nullopt, [=](
				QJsonObject json) {
			const auto page = Square::ParsePosts(json);
			Square::AppendUnique(state->posts, page.items);
			state->cursor = page.nextCursor;
			state->loading = false;
			state->loaded = true;
			state->rebuild();
		}, [=](Api::Error error) {
			state->loading = false;
			state->loaded = true;
			state->rebuild();
			if (error.status == 404) {
				controller->showToast(Tx().authorGone());
			} else {
				ShowError(controller, error);
			}
		});
	};
	state->rebuild();
	state->load();

	if (!author.username.isEmpty()) {
		box->addButton(rpl::single(tx.sendMessage()), [=] {
			box->closeBox();
			controller->showPeerByLink({ .usernameOrId = author.username });
		});
	}
	if (author.id != MyAuthorId()) {
		box->addLeftButton(rpl::single(tx.more()), [=] {
			const auto menu = box->lifetime().make_state<
				base::unique_qptr<Ui::PopupMenu>>();
			*menu = base::make_unique_q<Ui::PopupMenu>(
				box,
				st::popupMenuWithIcons);
			(*menu)->addAction(Tx().reportAuthor(), [=] {
				Report(controller, u"user"_q, author.id);
			}, &st::menuIconReport);
			(*menu)->addAction(Tx().block(), [=] {
				Block(controller, author);
			}, &st::menuIconBlock);
			(*menu)->popup(QCursor::pos());
		});
	}
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	AuthorBlocked() | rpl::filter([=](const QString &id) {
		return id == author.id;
	}) | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());
	box->boxClosing() | rpl::on_next([=] {
		if (const auto request = base::take(state->request)) {
			request->cancel();
		}
	}, box->lifetime());
}

} // namespace

Square::Texts Tx() {
	return Square::Texts(IsChinese());
}

bool FeatureEnabled() {
	return Settings::IsEnabled(L(Square::kFeatureKey));
}

qint64 MaxPostsPerDay() {
	return qint64(Settings::RemoteNumber(
		L(Square::kConfigMaxPostsPerDay),
		Square::kDefaultMaxPostsPerDay));
}

qint64 MaxVideoSeconds() {
	return qint64(Settings::RemoteNumber(
		L(Square::kConfigMaxVideoSeconds),
		Square::kDefaultMaxVideoSeconds));
}

QString ErrorText(const Api::Error &error) {
	const auto tx = Tx();
	if (error.code == u"rate_limited"_q
		|| error.code == u"rate_limit_unavailable"_q) {
		return tx.waitSeconds(error.retryAfterSeconds);
	} else if (error.code == u"banned"_q) {
		return tx.banned();
	} else if (error.code == u"not_linked"_q) {
		return tx.connectFailed();
	} else if (error.code == u"invalid_video"_q) {
		return tx.videoRejected();
	} else if (error.code == u"square_disabled"_q) {
		return tx.squareDisabled();
	}
	return error.text();
}

void ShowError(
		not_null<Window::SessionController*> controller,
		const Api::Error &error) {
	if (error.code == QString::fromLatin1(Api::kCancelled)) {
		return;
	} else if (error.code == u"not_linked"_q) {
		SilentLink::ClearCache(); // Linked again silently next time.
	} else if (error.code == u"profile_required"_q) {
		Data().profileKnown = false;
		EditProfile(controller);
		return;
	}
	controller->showToast(ErrorText(error));
}

rpl::producer<Square::Post> PostUpdated() {
	return Data().postUpdated.events();
}

rpl::producer<QString> PostRemoved() {
	return Data().postRemoved.events();
}

rpl::producer<QString> AuthorBlocked() {
	return Data().authorBlocked.events();
}

void NotifyPostUpdated(const Square::Post &post) {
	Data().postUpdated.fire_copy(post);
}

void NotifyPostRemoved(const QString &postId) {
	Data().postRemoved.fire_copy(postId);
}

rpl::producer<Square::Post> PostCreated() {
	return Data().postCreated.events();
}

void NotifyPostCreated(const Square::Post &post) {
	Data().postCreated.fire_copy(post);
}

QString MyAuthorId() {
	return Data().myAuthorId;
}

void PrefetchMe(not_null<Window::SessionController*> controller) {
	if (!Data().myAuthorId.isEmpty()) {
		return;
	}
	const auto session = &controller->session();
	const auto weak = base::make_weak(controller);
	const auto fetch = [=] {
		const auto strong = weak.get();
		if (!strong
			|| SilentLink::Cached(&strong->session())
				!= SilentLink::State::Linked) {
			return;
		}
		Request(strong, "GET", u"/v1/square/profile"_q, std::nullopt, [=](
				QJsonObject result) {
			if (!result.value(u"display_name"_q).toString().isEmpty()) {
				SetProfile(result);
			}
		}, nullptr);
	};
	if (SilentLink::Cached(session) == SilentLink::State::Unknown) {
		SilentLink::Refresh(session, fetch);
	} else {
		fetch();
	}
}

void RequireLink(
		not_null<Window::SessionController*> controller,
		Fn<void()> then) {
	const auto session = &controller->session();
	if (SilentLink::Cached(session) == SilentLink::State::Linked) {
		then();
		return;
	}
	const auto weak = base::make_weak(controller);
	const auto show = controller->uiShow();
	const auto start = [=] {
		if (!weak.get()) {
			return;
		} else if (SilentLink::Cached(session) == SilentLink::State::Linked) {
			then();
			return;
		}
		show->showToast(Tx().connecting());
		SilentLink::Ensure(session, show, [=](bool ok) {
			if (!weak.get()) {
				return;
			} else if (ok) {
				then();
			} else {
				show->showToast(Tx().connectFailed());
			}
		});
	};
	if (SilentLink::Cached(session) == SilentLink::State::Unknown) {
		SilentLink::Refresh(session, start);
	} else {
		start();
	}
}

void RequireProfile(
		not_null<Window::SessionController*> controller,
		Fn<void()> then) {
	RequireLink(controller, [=] {
		if (Data().profileKnown
			&& !Data().profile.value(u"display_name"_q).toString().isEmpty()) {
			then();
			return;
		}
		Request(controller, "GET", u"/v1/square/profile"_q, std::nullopt, [=](
				QJsonObject result) {
			if (result.value(u"display_name"_q).toString().isEmpty()) {
				controller->show(Box(ProfileBox, controller, QJsonObject(), then));
			} else {
				SetProfile(result);
				then();
			}
		}, [=](Api::Error error) {
			if (IsNoProfile(error)) {
				controller->show(Box(ProfileBox, controller, QJsonObject(), then));
			} else {
				ShowError(controller, error);
			}
		});
	});
}

void EditProfile(
		not_null<Window::SessionController*> controller,
		Fn<void()> saved) {
	RequireLink(controller, [=] {
		Request(controller, "GET", u"/v1/square/profile"_q, std::nullopt, [=](
				QJsonObject result) {
			if (!result.value(u"display_name"_q).toString().isEmpty()) {
				SetProfile(result);
			}
			controller->show(Box(ProfileBox, controller, result, saved));
		}, [=](Api::Error error) {
			if (IsNoProfile(error)) {
				controller->show(
					Box(ProfileBox, controller, QJsonObject(), saved));
			} else {
				ShowError(controller, error);
			}
		});
	});
}

void Report(
		not_null<Window::SessionController*> controller,
		const QString &target,
		const QString &id) {
	RequireLink(controller, [=] {
		controller->show(Box(ReportBox, controller, target, id));
	});
}

void Block(
		not_null<Window::SessionController*> controller,
		const Square::Author &author) {
	RequireLink(controller, [=] {
		const auto tx = Tx();
		controller->show(Ui::MakeConfirmBox({
			.text = tx.blockConfirm(author.displayName),
			.confirmed = [=](Fn<void()> close) {
				close();
				Request(
					controller,
					"POST",
					QString::fromLatin1(Blocks::kPath),
					Blocks::BlockBody(author.id),
					[=](QJsonObject) {
						const auto key = L(kBlockedNamesKey);
						Settings::SetString(key, Blocks::PutName(
							Settings::GetString(key),
							author.id,
							author.displayName));
						Data().authorBlocked.fire_copy(author.id);
						controller->showToast(Tx().blocked());
					},
					[=](Api::Error error) { ShowError(controller, error); });
			},
			.confirmText = tx.block(),
			.confirmStyle = &st::attentionBoxButton,
			.title = tx.block(),
		}));
	});
}

void ToggleLike(
		not_null<Window::SessionController*> controller,
		Square::Post post) {
	RequireLink(controller, [=] {
		const auto like = !post.liked;
		Request(
			controller,
			like ? "POST" : "DELETE",
			u"/v1/square/posts/"_q + Square::PathId(post.id) + u"/like"_q,
			std::nullopt,
			[=](QJsonObject result) {
				auto updated = post;
				updated.liked = result.value(u"liked"_q).toBool(like);
				updated.likeCount = std::max(0, result.value(
					u"like_count"_q
				).toInt(post.likeCount + (like ? 1 : -1)));
				NotifyPostUpdated(updated);
			},
			[=](Api::Error error) { ShowError(controller, error); });
	});
}

void DeletePost(
		not_null<Window::SessionController*> controller,
		const Square::Post &post) {
	const auto id = post.id;
	controller->show(Ui::MakeConfirmBox({
		.text = Tx().deletePostConfirm(),
		.confirmed = [=](Fn<void()> close) {
			close();
			Request(
				controller,
				"DELETE",
				u"/v1/square/posts/"_q + Square::PathId(id),
				std::nullopt,
				[=](QJsonObject) {
					NotifyPostRemoved(id);
					controller->showToast(Tx().deleted());
				},
				[=](Api::Error error) { ShowError(controller, error); });
		},
		.confirmText = Tx().deleteText(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void DeleteMyData(not_null<Window::SessionController*> controller) {
	RequireLink(controller, [=] {
		controller->show(Ui::MakeConfirmBox({
			.text = Tx().deleteMyDataConfirm(),
			.confirmed = [=](Fn<void()> close) {
				close();
				Request(controller, "DELETE", u"/v1/square/me"_q, std::nullopt, [=](
						QJsonObject) {
					const auto me = Data().myAuthorId;
					Data().profile = QJsonObject();
					Data().profileKnown = false;
					if (!me.isEmpty()) {
						Data().authorBlocked.fire_copy(me); // Own posts go.
					}
					controller->showToast(Tx().deletedMyData());
				}, [=](Api::Error error) { ShowError(controller, error); });
			},
			.confirmText = Tx().deleteText(),
			.confirmStyle = &st::attentionBoxButton,
			.title = Tx().deleteMyData(),
		}));
	});
}

void LoadImage(
		const QString &url,
		not_null<QObject*> guard,
		Fn<void(QImage)> done) {
	auto &data = Data();
	if (const auto i = data.images.find(url); i != data.images.end()) {
		done(i.value());
		return;
	}
	const auto weak = QPointer<QObject>(guard.get());
	auto callback = [=](QImage image) {
		if (weak) {
			done(std::move(image));
		}
	};
	auto &waiting = data.loading[url];
	waiting.push_back(std::move(callback));
	if (waiting.size() > 1) {
		return;
	}
	Api::Download(url, [=](QByteArray bytes) {
		auto image = QImage::fromData(bytes);
		auto &data = Data();
		if (!image.isNull()) {
			data.images.insert(url, image);
			data.imageOrder.push_back(url);
			while (data.imageOrder.size() > kImageCacheMax) {
				data.images.remove(data.imageOrder.takeFirst());
			}
		}
		const auto callbacks = data.loading.take(url);
		if (!image.isNull()) {
			for (const auto &callback : callbacks) {
				callback(image);
			}
		}
	}, [=](Api::Error) {
		Data().loading.remove(url);
	});
}

object_ptr<Ui::RpWidget> CreatePostWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		const Square::Post &post,
		bool full) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	const auto raw = result.data();
	const auto shared = std::make_shared<Square::Post>(post);
	const auto tx = Tx();

	const auto header = raw->add(object_ptr<PostHeader>(raw, post));
	header->setClickedCallback([=] {
		ShowAuthor(controller, shared->author);
	});
	const auto more = Ui::CreateChild<Ui::LinkButton>(
		header,
		u"⋯"_q);
	header->widthValue() | rpl::on_next([=](int width) {
		more->moveToRight(0, style::ConvertScale(8), width);
	}, more->lifetime());
	more->setClickedCallback([=] {
		ShowPostMenu(more, controller, *shared);
	});

	if (post.isHidden()) {
		raw->add(
			object_ptr<Ui::FlatLabel>(
				raw,
				rpl::single(Ui::Text::Bold(tx.hidden()).append(
					u"\n"_q + tx.hiddenInfo(post.hiddenReason))),
				st::boxDividerLabel),
			style::margins(0, style::ConvertScale(4), 0, 0));
	}
	if (!post.text.isEmpty()) {
		const auto text = raw->add(
			object_ptr<Ui::FlatLabel>(raw, post.text, st::boxLabel),
			style::margins(0, style::ConvertScale(6), 0, 0));
		text->setSelectable(full);
		if (!full) {
			// The click goes to the post, which opens it.
			text->setAttribute(Qt::WA_TransparentForMouseEvents);
		}
	}
	if (post.video || !post.images.empty()) {
		raw->add(
			object_ptr<MediaGrid>(raw, post),
			style::margins(0, style::ConvertScale(8), 0, 0));
	}
	AddFooterRow(raw, controller, shared, full);
	if (!full) {
		Ui::AddSkip(raw);
		Ui::AddDivider(raw);
		raw->setCursor(style::cur_pointer);
		base::install_event_filter(raw, [=](not_null<QEvent*> e) {
			if (e->type() == QEvent::MouseButtonRelease
				&& static_cast<QMouseEvent*>(e.get())->button()
					== Qt::LeftButton) {
				ShowPost(controller, *shared);
			}
			return base::EventFilterResult::Continue;
		});
	}
	return object_ptr<Ui::RpWidget>(std::move(result));
}

void ShowPost(
		not_null<Window::SessionController*> controller,
		const Square::Post &post) {
	controller->show(Box(PostBox, controller, post));
}

void ShowAuthor(
		not_null<Window::SessionController*> controller,
		const Square::Author &author) {
	if (author.id.isEmpty()) {
		return;
	}
	controller->show(Box(AuthorBox, controller, author));
}

} // namespace Luckgram::SquareUi
