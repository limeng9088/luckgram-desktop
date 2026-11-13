/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/discover/luckgram_square_compose.h"

#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "luckgram/discover/luckgram_square_logic.h"
#include "luckgram/discover/luckgram_square_ui.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/media/luckgram_image_prepare.h"
#include "luckgram/media/luckgram_video_transcode.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QBuffer>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QPointer>
#include <QtGui/QImageReader>
#include <QtGui/QPainter>

namespace Luckgram::SquareCompose {
namespace {

struct State {
	QStringList images;
	QString video;
	qint64 videoMs = 0;
	bool trimVideo = false;
	bool busy = false;
	rpl::variable<QString> status;
	rpl::variable<int> attachmentsVersion = 0;
	std::vector<Api::RequestPtr> requests;
};

[[nodiscard]] QString L(const char *text) {
	return QString::fromLatin1(text);
}

[[nodiscard]] QByteArray Json(const QJsonObject &o) {
	return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

[[nodiscard]] Square::Texts Tx() {
	return SquareUi::Tx();
}

// Desktop words: there is no gallery or camera on a computer.
[[nodiscard]] QString ChooseImagesText() {
	return Luckgram::IsChinese()
		? QString::fromUtf8("选择图片")
		: u"Choose photos"_q;
}

void Fail(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<State> state,
		const QString &text) {
	state->busy = false;
	state->status = QString();
	box->showToast(text);
}

// POST /v1/square/uploads for one content type, then a PUT of every body.
void UploadAll(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<State> state,
		QByteArray contentType,
		std::vector<QByteArray> bodies,
		Fn<void(int sentPercent)> progress,
		Fn<void(QStringList keys)> done) {
	auto sizes = std::vector<qint64>();
	for (const auto &body : bodies) {
		sizes.push_back(body.size());
	}
	const auto weak = QPointer<Ui::GenericBox>(box.get());
	state->requests.push_back(Api::Send({
		.method = "POST",
		.path = u"/v1/square/uploads"_q,
		.body = Json(Square::UploadsBody(
			QString::fromLatin1(contentType),
			sizes)),
		.contentType = "application/json",
	}, [=](Api::Response response) {
		if (!weak) {
			return;
		}
		const auto slots = Square::ParseUploads(response.json());
		if (slots.size() != bodies.size()) {
			Fail(weak.data(), state, Tx().connectFailed());
			return;
		}
		const auto keys = std::make_shared<QStringList>();
		const auto next = std::make_shared<Fn<void(int)>>();
		auto total = qint64(0);
		for (const auto size : sizes) {
			total += size;
		}
		const auto before = std::make_shared<qint64>(0);
		*next = [=](int index) {
			if (!weak) {
				return;
			} else if (index == int(slots.size())) {
				done(*keys);
				return;
			}
			state->requests.push_back(Api::Upload(
				slots[index].url,
				contentType,
				bodies[index],
				[=](qint64 sent, qint64) {
					if (progress && total > 0) {
						progress(int((*before + sent) * 100 / total));
					}
				},
				[=] {
					keys->push_back(slots[index].key);
					*before += sizes[index];
					(*next)(index + 1);
				},
				[=](Api::Error error) {
					if (weak) {
						Fail(weak.data(), state, Tx().connectFailed());
					}
				}));
		};
		(*next)(0);
	}, [=](Api::Error error) {
		if (weak) {
			Fail(weak.data(), state, SquareUi::ErrorText(error));
		}
	}));
}

void CreatePost(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		std::shared_ptr<State> state,
		QJsonObject body) {
	const auto weak = QPointer<Ui::GenericBox>(box.get());
	state->status = Tx().publishing();
	state->requests.push_back(Api::Send({
		.method = "POST",
		.path = u"/v1/square/posts"_q,
		.body = Json(body),
		.contentType = "application/json",
	}, [=](Api::Response response) {
		const auto post = Square::ParsePost(response.json());
		if (!post.id.isEmpty()) {
			SquareUi::NotifyPostCreated(post);
		}
		controller->showToast(Tx().published());
		if (weak) {
			weak->closeBox();
		}
	}, [=](Api::Error error) {
		if (weak) {
			state->busy = false;
			state->status = QString();
			SquareUi::ShowError(controller, error);
		}
	}));
}

void PublishImages(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		std::shared_ptr<State> state,
		QString text) {
	state->status = Tx().preparingImages();
	const auto paths = state->images;
	const auto weak = QPointer<Ui::GenericBox>(box.get());
	crl::async([=] {
		auto jpegs = std::vector<QByteArray>();
		for (const auto &path : paths) {
			auto prepared = Media::PrepareImage(path);
			if (prepared.jpeg.isEmpty()) {
				jpegs.clear();
				break;
			}
			jpegs.push_back(std::move(prepared.jpeg));
		}
		crl::on_main([=] {
			if (!weak) {
				return;
			} else if (jpegs.size() != size_t(paths.size())) {
				Fail(weak.data(), state, Tx().imageFailed());
				return;
			}
			UploadAll(
				weak.data(),
				state,
				Square::kImageContentType,
				jpegs,
				nullptr,
				[=](QStringList keys) {
					if (weak) {
						CreatePost(
							weak.data(),
							controller,
							state,
							Square::PostBody(text, keys));
					}
				});
		});
	});
}

void PublishVideo(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		std::shared_ptr<State> state,
		QString text) {
	state->status = Tx().preparingVideo();
	const auto weak = QPointer<Ui::GenericBox>(box.get());
	const auto maxSeconds = SquareUi::MaxVideoSeconds();
	const auto maxMb = qint64(Settings::RemoteNumber(
		L(Square::kConfigMaxVideoMb),
		Square::kDefaultMaxVideoMb));
	const auto path = state->video;
	crl::async([=] {
		auto result = Media::Transcode({
			.path = path,
			.maxEdge = Square::kVideoMaxEdge,
			.maxDurationMs = maxSeconds * 1000,
		}, [=](int percent) {
			crl::on_main([=] {
				if (weak) {
					state->status = Tx().preparingVideo()
						+ QChar(' ')
						+ QString::number(percent)
						+ QChar('%');
				}
			});
		});
		crl::on_main([=, result = std::move(result)] {
			if (!weak) {
				return;
			} else if (!result.error.isEmpty()) {
				Fail(weak.data(), state, Tx().videoFailed());
				return;
			} else if (result.mp4.size() > maxMb * 1024 * 1024) {
				Fail(weak.data(), state, Tx().videoTooBig(maxMb));
				return;
			}
			const auto cover = result.coverJpeg;
			UploadAll(
				weak.data(),
				state,
				Square::kVideoContentType,
				{ result.mp4 },
				[=](int percent) {
					state->status = Tx().uploadingVideo(percent);
				},
				[=](QStringList videoKeys) {
					if (!weak) {
						return;
					}
					UploadAll(
						weak.data(),
						state,
						Square::kImageContentType,
						{ cover },
						nullptr,
						[=](QStringList coverKeys) {
							if (weak
								&& !videoKeys.isEmpty()
								&& !coverKeys.isEmpty()) {
								CreatePost(
									weak.data(),
									controller,
									state,
									Square::PostBodyWithVideo(
										text,
										videoKeys.front(),
										coverKeys.front()));
							}
						});
				});
		});
	});
}

void ComposeBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto state = std::make_shared<State>();
	const auto tx = Tx();
	box->setTitle(rpl::single(tx.composeTitle()));
	box->setWidth(st::boxWideWidth);
	box->setCloseByOutsideClick(false);

	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine,
		rpl::single(tx.textHint())));
	field->setMaxLength(Square::kTextMax);
	box->setFocusCallback([=] { field->setFocusFast(); });

	const auto attachments = box->addRow(
		object_ptr<Ui::VerticalLayout>(box));
	const auto rebuild = [=] {
		attachments->clear();
		const auto tx = Tx();
		if (!state->video.isEmpty()) {
			attachments->add(object_ptr<Ui::FlatLabel>(
				attachments,
				tx.videoLabel(Square::WholeSeconds(std::min(
					state->videoMs,
					SquareUi::MaxVideoSeconds() * 1000))),
				st::boxLabel));
			const auto remove = attachments->add(
				object_ptr<Ui::LinkButton>(attachments, tx.removeVideo()));
			remove->setClickedCallback([=] {
				state->video = QString();
				state->attachmentsVersion = state->attachmentsVersion.current() + 1;
			});
		} else {
			if (!state->images.isEmpty()) {
				auto names = QStringList();
				for (const auto &path : state->images) {
					names.push_back(QFileInfo(path).fileName());
				}
				attachments->add(object_ptr<Ui::FlatLabel>(
					attachments,
					names.join(u", "_q),
					st::boxDividerLabel));
			}
			if (state->images.size() < Square::kImagesMax) {
				const auto add = attachments->add(object_ptr<Ui::LinkButton>(
					attachments,
					state->images.isEmpty()
						? ChooseImagesText()
						: tx.addImages(int(state->images.size()))));
				add->setClickedCallback([=] {
					FileDialog::GetOpenPaths(
						box.get(),
						ChooseImagesText(),
						u"Image files (*.jpg *.jpeg *.png *.webp *.bmp *.gif *.heic)"_q,
						crl::guard(box.get(), [=](FileDialog::OpenResult &&result) {
							for (const auto &path : result.paths) {
								if (state->images.size() < Square::kImagesMax) {
									state->images.push_back(path);
								}
							}
							state->attachmentsVersion
								= state->attachmentsVersion.current() + 1;
						}));
				});
			}
			if (!state->images.isEmpty()) {
				const auto remove = attachments->add(
					object_ptr<Ui::LinkButton>(attachments, tx.removeImages()));
				remove->setClickedCallback([=] {
					state->images.clear();
					state->attachmentsVersion
						= state->attachmentsVersion.current() + 1;
				});
			} else {
				const auto video = attachments->add(object_ptr<Ui::LinkButton>(
					attachments,
					Luckgram::IsChinese()
						? QString::fromUtf8("选择视频")
						: u"Choose a video"_q));
				video->setClickedCallback([=] {
					FileDialog::GetOpenPath(
						box.get(),
						tx.videoTitle(),
						u"Video files (*.mp4 *.mov *.m4v *.webm *.mkv *.avi)"_q,
						crl::guard(box.get(), [=](FileDialog::OpenResult &&result) {
							if (result.paths.isEmpty()) {
								return;
							}
							const auto path = result.paths.front();
							const auto info = Media::Probe(path);
							if (!info.ok) {
								box->showToast(Tx().videoFailed());
								return;
							}
							const auto apply = [=](bool trim) {
								state->video = path;
								state->videoMs = info.durationMs;
								state->trimVideo = trim;
								state->attachmentsVersion
									= state->attachmentsVersion.current() + 1;
							};
							const auto max = SquareUi::MaxVideoSeconds();
							if (info.durationMs > max * 1000) {
								box->getDelegate()->show(Ui::MakeConfirmBox({
									.text = Tx().videoTooLong(
										max,
										Square::WholeSeconds(info.durationMs)),
									.confirmed = [=](Fn<void()> close) {
										close();
										apply(true);
									},
									.confirmText = Tx().trimAndContinue(max),
									.title = Tx().videoTooLongTitle(),
								}));
							} else {
								apply(false);
							}
						}));
				});
			}
		}
		attachments->resizeToWidth(attachments->width());
	};
	state->attachmentsVersion.value() | rpl::on_next([=] {
		rebuild();
	}, attachments->lifetime());

	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(tx.attachmentsInfo(SquareUi::MaxVideoSeconds())
			+ QChar(' ')
			+ tx.dailyLimitInfo(SquareUi::MaxPostsPerDay())),
		st::boxDividerLabel));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		state->status.value(),
		st::boxDividerLabel));

	box->addButton(rpl::single(tx.publish()), [=] {
		if (state->busy) {
			return;
		}
		const auto text = field->getLastText().trimmed();
		const auto images = int(state->images.size());
		const auto hasVideo = !state->video.isEmpty();
		if (Square::Length(text) > Square::kTextMax) {
			box->showToast(Tx().tooLong());
			return;
		} else if (!Square::IsValidPost(text, images, hasVideo)) {
			box->showToast(Tx().needContent());
			return;
		}
		state->busy = true;
		SquareUi::RequireProfile(controller, crl::guard(box.get(), [=] {
			if (hasVideo) {
				PublishVideo(box, controller, state, text);
			} else if (images > 0) {
				PublishImages(box, controller, state, text);
			} else {
				CreatePost(box, controller, state, Square::PostBody(text, {}));
			}
		}));
	});
	box->addButton(rpl::single(tx.cancel()), [=] { box->closeBox(); });
	box->boxClosing() | rpl::on_next([=] {
		for (const auto &request : base::take(state->requests)) {
			request->cancel();
		}
	}, box->lifetime());
}

} // namespace

void Open(not_null<Window::SessionController*> controller) {
	SquareUi::RequireProfile(controller, [=] {
		controller->show(Box(ComposeBox, controller));
	});
}

} // namespace Luckgram::SquareCompose
