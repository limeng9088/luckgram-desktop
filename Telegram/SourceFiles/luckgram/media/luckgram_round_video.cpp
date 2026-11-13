/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/media/luckgram_round_video.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_chat_participant_status.h"
#include "data/data_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/media/luckgram_round_video_logic.h"
#include "luckgram/media/luckgram_video_transcode.h"
#include "main/main_session.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QFileInfo>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainterPath>
#include <QtGui/QWheelEvent>

#include <cmath>

namespace Luckgram::RoundVideo {
namespace {

[[nodiscard]] Texts Tx() {
	return Texts(IsChinese());
}

[[nodiscard]] QString Time(qint64 ms) {
	const auto s = std::max(qint64(0), ms / 1000);
	return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

// The circle viewfinder: the frame under the circle, dragged and zoomed.
class Viewfinder final : public Ui::RpWidget {
public:
	explicit Viewfinder(QWidget *parent)
	: RpWidget(parent) {
		const auto side = style::ConvertScale(300);
		resize(side, side);
		setCursor(style::cur_sizeall);
	}

	void setFrame(QImage frame) {
		_frame = std::move(frame);
		clamp();
		update();
	}

	[[nodiscard]] QRect crop() const {
		if (_frame.isNull()) {
			return QRect();
		}
		const auto d = double(width());
		const auto base = BaseScale(d, _frame.width(), _frame.height());
		const auto square = CropSquare(
			_frame.width(),
			_frame.height(),
			base,
			_zoom,
			d,
			_offset.x,
			_offset.y);
		return QRect(
			int(std::round(square.x)),
			int(std::round(square.y)),
			int(std::round(square.side)),
			int(std::round(square.side)));
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto d = double(width());
		p.fillRect(rect(), QColor(0, 0, 0));
		if (!_frame.isNull()) {
			const auto source = crop();
			p.drawImage(rect(), _frame, source);
		}
		auto outside = QPainterPath();
		outside.addRect(rect());
		auto circle = QPainterPath();
		circle.addEllipse(QRectF(0, 0, d, d));
		p.fillPath(outside.subtracted(circle), QColor(0, 0, 0, 160));
		p.setPen(QPen(QColor(255, 255, 255, 200), style::ConvertScale(2)));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(QRectF(1, 1, d - 2, d - 2));
	}

	void mousePressEvent(QMouseEvent *e) override {
		_dragging = true;
		_last = e->pos();
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		if (!_dragging) {
			return;
		}
		const auto delta = e->pos() - _last;
		_last = e->pos();
		_offset.x += delta.x();
		_offset.y += delta.y();
		clamp();
		update();
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		_dragging = false;
	}

	void wheelEvent(QWheelEvent *e) override {
		const auto steps = e->angleDelta().y() / 120.;
		_zoom = ClampZoom(_zoom * std::pow(1.1, steps));
		clamp();
		update();
	}

private:
	void clamp() {
		if (_frame.isNull()) {
			return;
		}
		const auto d = double(width());
		const auto base = BaseScale(d, _frame.width(), _frame.height());
		_offset = ClampOffset(
			_frame.width(),
			_frame.height(),
			base,
			_zoom,
			d,
			_offset.x,
			_offset.y);
	}

	QImage _frame;
	double _zoom = 1.;
	Offset _offset;
	bool _dragging = false;
	QPoint _last;

};

void CropBox(
		not_null<Ui::GenericBox*> box,
		QString path,
		Media::VideoInfo info,
		Fn<void(QByteArray, crl::time)> send) {
	struct State {
		double left = 0.;
		bool busy = false;
		std::shared_ptr<std::atomic<bool>> cancelled
			= std::make_shared<std::atomic<bool>>(false);
		rpl::variable<QString> status;
		rpl::variable<QString> range;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto tx = Tx();
	const auto duration = info.durationMs;
	const auto timeline = NeedsTimeline(duration);
	const auto rangeNow = [=] {
		return SelectedRange(
			duration,
			state->left,
			state->left + InitialRightProgress(duration));
	};

	box->setTitle(rpl::single(tx.title()));
	box->setCloseByOutsideClick(false);
	const auto container = box->verticalLayout();
	const auto finder = container->add(
		object_ptr<Ui::RpWidget>(container),
		style::margins(0, style::ConvertScale(8), 0, style::ConvertScale(8)));
	const auto view = Ui::CreateChild<Viewfinder>(finder);
	finder->resize(view->width(), view->height());
	finder->widthValue() | rpl::on_next([=](int width) {
		view->moveToLeft((width - view->width()) / 2, 0);
	}, view->lifetime());

	const auto weak = QPointer<Viewfinder>(view);
	const auto loadFrame = [=](qint64 atMs) {
		crl::async([=] {
			auto frame = Media::ReadFrame(path, atMs);
			crl::on_main([=, frame = std::move(frame)]() mutable {
				if (weak) {
					weak->setFrame(std::move(frame));
				}
			});
		});
	};
	loadFrame(0);

	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tx.hint(),
		st::boxDividerLabel));
	if (timeline) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tx.trimHint(),
			st::boxDividerLabel));
		const auto slider = box->addRow(object_ptr<Ui::MediaSlider>(
			box,
			st::defaultContinuousSlider));
		slider->resize(st::defaultContinuousSlider.seekSize);
		slider->setAlwaysDisplayMarker(true);
		slider->setValue(0.);
		const auto maxLeft = 1. - InitialRightProgress(duration);
		const auto updateRange = [=] {
			const auto range = rangeNow();
			state->range = Tx().selected(
				Time(range.startMs) + u" – "_q + Time(range.endMs));
		};
		slider->setChangeProgressCallback([=](float64 value) {
			state->left = value * maxLeft;
			updateRange();
		});
		slider->setChangeFinishedCallback([=](float64 value) {
			state->left = value * maxLeft;
			updateRange();
			loadFrame(rangeNow().startMs);
		});
		updateRange();
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			state->range.value(),
			st::boxDividerLabel));
	}
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		state->status.value(),
		st::boxDividerLabel));

	box->addButton(rpl::single(tx.send()), [=] {
		if (state->busy) {
			return;
		}
		const auto crop = view->crop();
		if (crop.isEmpty()) {
			return;
		}
		state->busy = true;
		state->status = Tx().converting(0);
		const auto range = rangeNow();
		const auto boxWeak = QPointer<Ui::GenericBox>(box.get());
		auto args = Media::TranscodeArgs{
			.path = path,
			.maxDurationMs = std::max(
				qint64(1000),
				std::min(range.endMs - range.startMs, kMaxDurationMs)),
			.cancelled = state->cancelled,
			.startMs = range.startMs,
			.crop = crop,
			.squareSide = kDesktopOutputSide,
		};
		crl::async([=, args = std::move(args)] {
			auto result = Media::Transcode(args, [=](int percent) {
				crl::on_main([=] {
					if (boxWeak) {
						state->status = Tx().converting(percent);
					}
				});
			});
			crl::on_main([=, result = std::move(result)] {
				if (!boxWeak) {
					return;
				} else if (!result.error.isEmpty()) {
					state->busy = false;
					state->status = QString();
					if (result.error != u"cancelled"_q) {
						boxWeak->showToast(Tx().failed());
					}
					return;
				}
				const auto bytes = result.mp4;
				const auto ms = crl::time(result.durationMs);
				boxWeak->closeBox();
				send(bytes, ms);
			});
		});
	});
	box->addButton(rpl::single(tx.cancel()), [=] { box->closeBox(); });
	box->boxClosing() | rpl::on_next([=] {
		state->cancelled->store(true);
	}, box->lifetime());
}

} // namespace

void AddSendFilesMenuAction(
		not_null<Ui::PopupMenu*> menu,
		std::shared_ptr<ChatHelpers::Show> show,
		const Ui::PreparedList &list,
		not_null<PeerData*> peer,
		const FullReplyTo &replyTo,
		bool scheduled,
		Fn<void()> closeBox) {
	const auto single = (list.files.size() == 1) ? &list.files.front() : nullptr;
	const auto path = single ? single->path : QString();
	const auto isVideo = single
		&& (single->type == Ui::PreparedFile::Type::Video);
	const auto canSend = !Data::RestrictionError(
		peer,
		ChatRestriction::SendVideoMessages);
	if (!CanOffer(
			Settings::IsEnabled(QString::fromLatin1(kFeatureKey)),
			false,
			int(list.files.size()),
			isVideo,
			!path.isEmpty() && QFileInfo::exists(path),
			peer->starsPerMessageChecked(),
			scheduled,
			canSend)) {
		return;
	}
	const auto session = &peer->session();
	const auto reply = replyTo;
	menu->addAction(Tx().sendAsRound(), [=] {
		const auto info = Media::Probe(path);
		if (!info.ok) {
			show->showToast(Tx().failed());
			return;
		}
		if (closeBox) {
			closeBox();
		}
		const auto weak = base::make_weak(session);
		show->showBox(Box(CropBox, path, info, [=](
				QByteArray bytes,
				crl::time duration) {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			auto action = Api::SendAction(strong->data().history(peer));
			action.replyTo = reply;
			strong->api().sendVoiceMessage(
				bytes,
				VoiceWaveform(),
				duration,
				true,
				action);
		}));
	}, &st::menuIconVideoChat);
}

} // namespace Luckgram::RoundVideo
