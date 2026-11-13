/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/transcribe/luckgram_dictation.h"

#include "base/timer.h"
#include "chat_helpers/compose/compose_show.h"
#include "lang/lang_keys.h"
#include "luckgram/luckgram_ai.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/transcribe/luckgram_transcribe_logic.h"
#include "media/audio/media_audio_capture.h"
#include "media/audio/media_audio_capture_common.h"
#include "platform/platform_specific.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_basic.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QPointer>

#include <deque>

namespace Luckgram::Transcribe {
namespace {

constexpr auto kBars = 40;

enum class Phase {
	Recording,
	Working,
	Error,
};

struct BoxState {
	rpl::variable<Phase> phase = Phase::Recording;
	rpl::variable<QString> status;
	std::deque<double> levels;
	QByteArray audio; // OGG Opus from the capture, for 「重试」.
	Api::RequestPtr request;
	rpl::lifetime recording;
	base::Timer limit;
	bool capturing = false;
};

[[nodiscard]] QString ErrorFor(const Api::Error &error) {
	return Api::ErrorText(error.code);
}

void StopCapture(not_null<BoxState*> state) {
	state->recording.destroy();
	state->limit.cancel();
	if (state->capturing) {
		state->capturing = false;
		::Media::Capture::instance()->stop();
	}
}

void DictationBox(
		not_null<Ui::GenericBox*> box,
		Fn<void(QString)> done) {
	const auto state = box->lifetime().make_state<BoxState>();
	box->setTitle(TrValue("语音输入", "Voice input"));
	box->setWidth(st::boxWidth);

	const auto levels = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::boxRowPadding);
	levels->resize(levels->width(), st::historyAttach.height);
	levels->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(levels);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		const auto w = levels->width();
		const auto h = levels->height();
		const auto step = w / float64(kBars);
		const auto bar = std::max(2., step * 0.5);
		auto x = w - step;
		for (auto i = state->levels.rbegin(); i != state->levels.rend(); ++i) {
			const auto height = std::max(bar, (*i) * h);
			p.drawRoundedRect(
				QRectF(x, (h - height) / 2., bar, height),
				bar / 2.,
				bar / 2.);
			x -= step;
		}
	}, levels->lifetime());

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			state->status.value(),
			st::boxLabel),
		st::boxRowPadding);

	const auto finish = [=](QString text) {
		state->request = nullptr;
		const auto weak = QPointer<Ui::GenericBox>(box.get());
		done(text);
		if (weak) {
			weak->closeBox();
		}
	};
	const auto fail = [=](QString text) {
		state->request = nullptr;
		state->status = text;
		state->phase = Phase::Error;
	};
	const auto tidy = [=](QString raw) {
		state->status = Tr("正在整理…", "Tidying up…");
		auto args = AI::ChatArgs{
			.task = u"writing"_q,
			.messages = { { u"user"_q, raw } },
			.stream = false,
		};
		args.extra.insert(
			u"effect"_q,
			QString::fromLatin1(kDictationEffect));
		state->request = AI::Chat(std::move(args), nullptr, [=](QString text) {
			finish(TidyResult(raw, text));
		}, [=](Api::Error) {
			finish(raw); // Keep the plain transcript, as on mobile.
		});
	};
	const auto transcribe = [=] {
		state->phase = Phase::Working;
		state->status = Tr("正在转写…", "Transcribing…");
		state->request = AI::Transcribe(
			state->audio,
			u"audio/ogg"_q,
			QString(),
			[=](QString text) {
				const auto trimmed = text.trimmed();
				if (trimmed.isEmpty()) {
					fail(Tr("没有识别到文字", "No words recognized"));
				} else if (Settings::IsEnabled(
						QString::fromLatin1(kDictationTidyFeature))) {
					tidy(trimmed);
				} else {
					finish(trimmed);
				}
			},
			[=](Api::Error error) {
				if (error.code == QString::fromLatin1(Api::kConsentDenied)
					|| error.code == QString::fromLatin1(Api::kCancelled)) {
					box->closeBox();
				} else {
					fail(ErrorFor(error));
				}
			});
	};
	const auto stop = [=] {
		if (!state->capturing) {
			return;
		}
		state->recording.destroy();
		state->limit.cancel();
		state->capturing = false;
		state->phase = Phase::Working;
		::Media::Capture::instance()->stop(crl::guard(box, [=](
				::Media::Capture::Result &&result) {
			if (TooShort(result.duration) || result.bytes.isEmpty()) {
				box->closeBox();
				return;
			}
			state->audio = std::move(result.bytes);
			transcribe();
		}));
	};
	const auto start = [=] {
		const auto capture = ::Media::Capture::instance();
		if (!capture->available() || capture->started()) {
			fail(Tr("无法使用麦克风", "Can't use the microphone"));
			return;
		}
		state->phase = Phase::Recording;
		state->status = Tr(
			"正在聆听…说完点「停止」",
			"Listening… click Stop when done");
		state->levels.clear();
		state->capturing = true;
		capture->start();
		capture->updated(
		) | rpl::on_next_error([=](const ::Media::Capture::Update &update) {
			state->levels.push_back(LevelRatio(update.level));
			while (state->levels.size() > kBars) {
				state->levels.pop_front();
			}
			levels->update();
		}, [=](::Media::Capture::Error) {
			StopCapture(state);
			fail(Tr("无法使用麦克风", "Can't use the microphone"));
		}, state->recording);
		state->limit.setCallback(stop);
		state->limit.callOnce(kMaxRecordMs);
	};

	state->phase.value() | rpl::on_next([=](Phase phase) {
		box->clearButtons();
		switch (phase) {
		case Phase::Recording:
		case Phase::Working:
			// 「停止」 stays during the request, like mobile (no effect).
			box->addButton(TrValue("停止", "Stop"), stop);
			break;
		case Phase::Error:
			box->addButton(TrValue("重试", "Retry"), [=] {
				if (state->audio.isEmpty()) {
					start();
				} else {
					transcribe();
				}
			});
			break;
		}
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}, box->lifetime());

	box->lifetime().add([=] {
		StopCapture(state);
		if (const auto request = base::take(state->request)) {
			request->cancel();
		}
	});
	start();
}

void WithMicrophone(
		std::shared_ptr<ChatHelpers::Show> show,
		Fn<void()> granted) {
	using namespace Platform;
	const auto type = PermissionType::Microphone;
	const auto status = GetPermissionStatus(type);
	if (status == PermissionStatus::Granted) {
		granted();
	} else if (status == PermissionStatus::CanRequest) {
		RequestPermission(type, [=](PermissionStatus status) {
			if (status == PermissionStatus::Granted) {
				crl::on_main(granted);
			}
		});
	} else {
		show->showBox(Ui::MakeConfirmBox({
			.text = tr::lng_no_mic_permission(),
			.confirmed = [=](Fn<void()> &&close) {
				OpenSystemSettingsForPermission(type);
				close();
			},
			.confirmText = tr::lng_menu_settings(),
		}));
	}
}

} // namespace

Compose::Slot CreateDictationSlot(const Compose::SlotContext &context) {
	auto button = object_ptr<Ui::IconButton>(
		context.parent,
		st::historyLuckgramDictation);
	const auto raw = button.data();
	const auto field = QPointer<Ui::InputField>(context.field.get());
	const auto show = context.show;
	const auto start = [=] {
		if (!Settings::IsEnabled(QString::fromLatin1(kDictationFeature))) {
			return;
		}
		const auto insert = [=](QString text) {
			if (!field || text.isEmpty()) {
				return;
			}
			auto cursor = field->textCursor();
			cursor.insertText(text); // 17.5: at the cursor.
			field->setTextCursor(cursor);
			field->setFocus();
		};
		// Consent before recording, so nobody talks into a dialog that is
		// then declined (mobile).
		AI::WithConsent(show, [=] {
			WithMicrophone(show, [=] {
				show->showBox(Box(DictationBox, insert));
			});
		});
	};
	raw->setClickedCallback(start);
	return {
		.widget = std::move(button),
		.wanted = Settings::EnabledValue(
			QString::fromLatin1(kDictationFeature)),
		// specs#94: 「语音输入」 in the 「+」 menu (compact bar).
		.plus = [=](not_null<Ui::DropdownMenu*> menu) {
			if (Settings::IsEnabled(QString::fromLatin1(kDictationFeature))) {
				menu->addAction(
					Tr("语音输入", "Voice input"),
					start,
					&st::luckgramMenuIconDictation);
			}
		},
	};
}

} // namespace Luckgram::Transcribe
