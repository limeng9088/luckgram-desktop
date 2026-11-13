/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/calls/luckgram_call_recording.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "calls/calls_call.h"
#include "core/application.h"
#include "data/data_chat_participant_status.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "luckgram/ai/ai_compose_logic.h"
#include "luckgram/calls/luckgram_call_recording_encoder.h"
#include "luckgram/calls/luckgram_call_recording_logic.h"
#include "luckgram/luckgram_ai.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/show.h"
#include "ui/widgets/buttons.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_widgets.h"

#include <atomic>
#include <map>
#include <mutex>

namespace Luckgram::CallRecording {
namespace {

constexpr auto kPumpInterval = crl::time(200);
// An upload that never shows up as sent (failed, app offline) is given up.
constexpr auto kUploadTimeout = crl::time(60 * 60 * 1000);

[[nodiscard]] QString K(const char *key) {
	return QString::fromLatin1(key);
}

[[nodiscard]] Texts Tx() {
	return Texts(IsChinese());
}

[[nodiscard]] QString TargetKey(not_null<Main::Session*> session) {
	return K(kTargetKey) + QChar('_') + QString::number(session->userId().bare);
}

[[nodiscard]] bool IsOver(Calls::Call::State state) {
	using State = Calls::Call::State;
	switch (state) {
	case State::FailedHangingUp:
	case State::Failed:
	case State::HangingUp:
	case State::MigrationHangingUp:
	case State::Ended:
	case State::EndedByOtherDevice:
	case State::Busy:
		return true;
	default:
		return false;
	}
}

void ShowToast(const QString &text) {
	if (const auto window = Core::App().activePrimaryWindow()) {
		if (const auto controller = window->sessionController()) {
			controller->showToast(text);
		}
	}
}

// Fed on the WebRTC audio threads, read on the main thread.
class Sink final : public CallAudioTap::Sink {
public:
	void setMuted(bool muted) {
		_muted = muted;
	}

	void recorded(
			const int16_t *pcm,
			int frames,
			int sampleRate,
			int channels) override {
		// Muted: the other side hears nothing, so the recording neither.
		auto lock = std::lock_guard(_mutex);
		_mixer.pushNear(pcm, frames, sampleRate, channels, _muted);
	}
	void played(
			const int16_t *pcm,
			int frames,
			int sampleRate,
			int channels) override {
		auto lock = std::lock_guard(_mutex);
		_mixer.pushFar(pcm, frames, sampleRate, channels);
	}

	void drain(std::vector<int16_t> &out) {
		auto lock = std::lock_guard(_mutex);
		_mixer.drain(out);
	}
	void flush(std::vector<int16_t> &out) {
		auto lock = std::lock_guard(_mutex);
		_mixer.flush(out);
	}

private:
	std::mutex _mutex;
	Mixer _mixer;
	std::atomic<bool> _muted = false;

};

struct HubEntry {
	base::weak_ptr<Calls::Call> call;
	std::shared_ptr<CallAudioTap::Hub> hub;
};

[[nodiscard]] std::map<const Calls::Call*, HubEntry> &Hubs() {
	static auto result = std::map<const Calls::Call*, HubEntry>();
	return result;
}

[[nodiscard]] std::shared_ptr<CallAudioTap::Hub> HubFor(
		not_null<Calls::Call*> call) {
	const auto i = Hubs().find(call);
	return (i != end(Hubs()) && i->second.call.get() == call)
		? i->second.hub
		: nullptr;
}

struct Finished {
	base::weak_ptr<Main::Session> session;
	not_null<UserData*> user;
	QDateTime startedAt;
	QByteArray file;
	qint64 durationMs = 0;
	std::vector<QByteArray> segments; // For /v1/ai/transcribe, or empty.
};

void Upload(Finished &&finished);

class Recording final : public base::has_weak_ptr {
public:
	Recording(
		not_null<Calls::Call*> call,
		std::shared_ptr<CallAudioTap::Hub> hub);
	~Recording();

	[[nodiscard]] bool valid() const;
	[[nodiscard]] not_null<Calls::Call*> call() const;
	[[nodiscard]] crl::time elapsed() const;
	void setMuted(bool muted);
	void stop(bool save);

private:
	void pump();
	void push(const std::vector<int16_t> &samples);

	const not_null<Calls::Call*> _call;
	const not_null<UserData*> _user;
	const std::shared_ptr<CallAudioTap::Hub> _hub;
	const std::shared_ptr<Sink> _sink;
	const QDateTime _startedAt;
	const crl::time _started = 0;
	std::unique_ptr<OggOpusEncoder> _encoder;
	std::unique_ptr<OggOpusEncoder> _segment;
	std::vector<QByteArray> _segments;
	qint64 _segmentLimit = 0;
	bool _failed = false;
	base::Timer _timer;

};

Recording::Recording(
	not_null<Calls::Call*> call,
	std::shared_ptr<CallAudioTap::Hub> hub)
: _call(call)
, _user(call->user())
, _hub(std::move(hub))
, _sink(std::make_shared<Sink>())
, _startedAt(QDateTime::currentDateTime())
, _started(crl::now())
, _timer([=] { pump(); }) {
	const auto name = _user->name();
	_encoder = std::make_unique<OggOpusEncoder>(
		SanitizeFileName(Tx().fileTitle(name, FileDateTime(_startedAt))),
		name);
	if (AiTranscript() && AI::HasConsent()) {
		_segmentLimit = SegmentLimitBytes(qint64(Settings::RemoteNumber(
			u"ai.transcribe_max_mb"_q,
			kDefaultTranscribeMaxMb)));
		_segment = std::make_unique<OggOpusEncoder>();
	}
	if (!_encoder->valid()) {
		return;
	}
	_sink->setMuted(call->muted());
	_hub->setSink(_sink);
	_timer.callEach(kPumpInterval);
}

Recording::~Recording() {
	_hub->setSink(nullptr);
}

bool Recording::valid() const {
	return !_failed && _encoder && _encoder->valid();
}

not_null<Calls::Call*> Recording::call() const {
	return _call;
}

crl::time Recording::elapsed() const {
	return crl::now() - _started;
}

void Recording::setMuted(bool muted) {
	_sink->setMuted(muted);
}

void Recording::push(const std::vector<int16_t> &samples) {
	if (samples.empty() || !valid()) {
		return;
	}
	if (!_encoder->push(samples.data(), int(samples.size()))) {
		_failed = true;
		return;
	}
	if (!_segment) {
		return;
	} else if (!_segment->push(samples.data(), int(samples.size()))) {
		_segment = nullptr; // Only the transcript loses, not the audio.
		_segments.clear();
	} else if (_segment->bytes() >= _segmentLimit) {
		_segments.push_back(_segment->finish());
		_segment = std::make_unique<OggOpusEncoder>();
	}
}

void Recording::pump() {
	auto samples = std::vector<int16_t>();
	_sink->drain(samples);
	push(samples);
}

void Recording::stop(bool save) {
	_timer.cancel();
	_hub->setSink(nullptr);
	auto samples = std::vector<int16_t>();
	_sink->flush(samples);
	push(samples);
	if (!valid()) {
		if (save) {
			ShowToast(Tx().startFailed());
		}
		return;
	}
	const auto durationMs = _encoder->durationMs();
	auto file = _encoder->finish();
	if (_segment && _segment->durationMs() > 0) {
		_segments.push_back(_segment->finish());
	}
	if (!save || file.isEmpty() || durationMs < kMinDurationMs) {
		return;
	}
	if (ranges::any_of(_segments, &QByteArray::isEmpty)) {
		_segments.clear();
	}
	Upload({
		.session = base::make_weak(&_user->session()),
		.user = _user,
		.startedAt = _startedAt,
		.file = std::move(file),
		.durationMs = durationMs,
		.segments = std::move(_segments),
	});
}

[[nodiscard]] std::unique_ptr<Recording> &Current() {
	static auto result = std::unique_ptr<Recording>();
	return result;
}

[[nodiscard]] rpl::event_stream<> &CurrentChanges() {
	static auto result = rpl::event_stream<>();
	return result;
}

void Stop(bool save) {
	if (auto recording = base::take(Current())) {
		CurrentChanges().fire({});
		recording->stop(save);
	}
}

void StopFor(not_null<Calls::Call*> call, bool save) {
	if (Current() && Current()->call() == call) {
		Stop(save);
	}
}

void Start(not_null<Calls::Call*> call, std::shared_ptr<Ui::Show> show) {
	if (Current() || !FeatureEnabled()) {
		return;
	}
	const auto failed = [=] {
		show->showToast(Tx().startFailed());
	};
	const auto hub = HubFor(call);
	if (!hub || call->state() != Calls::Call::State::Established) {
		failed();
		return;
	}
	auto recording = std::make_unique<Recording>(call, hub);
	if (!recording->valid()) {
		failed();
		return;
	}
	Current() = std::move(recording);
	CurrentChanges().fire({});
}

// One finished recording: the audio message, then the AI reply under it.
class Uploader final : public base::has_weak_ptr {
public:
	Uploader(Finished &&finished, Fn<void()> destroy);

	void start();

private:
	void sent(not_null<HistoryItem*> item);
	void startAi();
	void transcribeNext();
	void summarize();
	void aiDone(QString reply);
	void maybeFinish();

	Finished _data;
	const Fn<void()> _destroy;
	const not_null<Main::Session*> _session;
	History *_history = nullptr;
	QString _fileName;
	QStringList _transcript;
	QString _partialReason;
	int _segmentIndex = 0;
	Api::RequestPtr _request;
	std::optional<FullMsgId> _sentId;
	std::optional<QString> _reply;
	base::Timer _timeout;
	rpl::lifetime _lifetime;

};

[[nodiscard]] std::vector<std::unique_ptr<Uploader>> &Uploaders() {
	static auto result = std::vector<std::unique_ptr<Uploader>>();
	return result;
}

Uploader::Uploader(Finished &&finished, Fn<void()> destroy)
: _data(std::move(finished))
, _destroy(std::move(destroy))
, _session(_data.session.get())
, _timeout([=] { _destroy(); }) {
}

void Uploader::start() {
	const auto session = _session;
	auto target = Target(session);
	if (!target || !Data::CanSend(target, ChatRestriction::SendFiles)) {
		target = session->user();
	}
	_history = session->data().history(target);
	const auto peerName = [&] {
		const auto name = _data.user->name();
		return name.isEmpty() ? u"?"_q : name;
	}();
	_fileName = SanitizeFileName(Tx().fileTitle(
		peerName,
		FileDateTime(_data.startedAt))) + u".ogg"_q;
	const auto caption = Tx().caption(
		peerName,
		Duration(DurationSeconds(_data.durationMs)));

	// The local message appears when the file is prepared, the server id
	// when the upload is done.
	session->data().newItemAdded(
	) | rpl::filter([=](not_null<HistoryItem*> item) {
		const auto media = item->media();
		const auto document = media ? media->document() : nullptr;
		return !_sentId
			&& item->out()
			&& (item->history() == _history)
			&& document
			&& (document->filename() == _fileName);
	}) | rpl::on_next([=](not_null<HistoryItem*> item) {
		_sentId = item->fullId();
		if (item->isRegular()) {
			sent(item);
		}
	}, _lifetime);
	session->data().itemIdChanged(
	) | rpl::filter([=](const Data::Session::IdChange &change) {
		return _sentId
			&& (_sentId->peer == change.newId.peer)
			&& (_sentId->msg == change.oldId);
	}) | rpl::on_next([=](const Data::Session::IdChange &change) {
		_sentId = change.newId;
		if (const auto item = _session->data().message(change.newId)) {
			sent(item);
		}
	}, _lifetime);

	// Without force_file the server turns an Ogg/Opus upload into a voice
	// message (mobile #79).
	session->api().luckgramSendFile(
		_data.file,
		_fileName,
		TextWithTags{ caption },
		true,
		::Api::SendAction(_history));
	_data.file = QByteArray(); // FileLoadTask has its own copy.
	_timeout.callOnce(kUploadTimeout);
	startAi();
}

void Uploader::sent(not_null<HistoryItem*> item) {
	if (_history->peer->isSelf()) {
		ShowToast(Tx().savedToast(Tx().savedMessages()));
	} else {
		ShowToast(Tx().savedToast(_history->peer->name()));
	}
	_sentId = item->fullId();
	maybeFinish();
}

void Uploader::startAi() {
	if (!AiTranscript() || !AI::HasConsent() || _data.segments.empty()) {
		_reply = QString();
		return;
	}
	transcribeNext();
}

void Uploader::transcribeNext() {
	if (_segmentIndex >= int(_data.segments.size())) {
		summarize();
		return;
	}
	const auto audio = base::take(_data.segments[_segmentIndex++]);
	_request = AI::Transcribe(audio, u"audio/ogg"_q, QString(), crl::guard(
		this,
		[=](QString text) {
			_request = nullptr;
			if (!text.trimmed().isEmpty()) {
				_transcript.push_back(text.trimmed());
			}
			transcribeNext();
		}), crl::guard(this, [=](Api::Error error) {
			_request = nullptr;
			if (_transcript.isEmpty()) {
				aiDone(Tx().transcriptFailed(error.text()));
			} else {
				// A partial transcript keeps the finished segments (#55).
				_partialReason = error.text();
				summarize();
			}
		}));
}

void Uploader::summarize() {
	const auto transcript = _transcript.join(QChar('\n'));
	if (transcript.isEmpty()) {
		aiDone(Tx().transcriptHeader() + QChar('\n') + Tx().transcriptEmpty());
		return;
	}
	const auto maxInput = int(Settings::RemoteNumber(
		u"ai.max_input_chars"_q,
		kDefaultMaxInputChars));
	auto args = AI::ChatArgs{
		.task = QString::fromLatin1(AI::kTaskSummary),
		.messages = { { u"user"_q, Limit(transcript, maxInput) } },
		.stream = false,
	};
	const auto reply = [=](QString summary) {
		aiDone(Tx().reply(summary, transcript, _partialReason));
	};
	_request = AI::Chat(
		std::move(args),
		nullptr,
		crl::guard(this, reply),
		crl::guard(this, [=](Api::Error) { reply(QString()); }));
}

void Uploader::aiDone(QString reply) {
	_request = nullptr;
	_reply = std::move(reply);
	maybeFinish();
}

void Uploader::maybeFinish() {
	if (!_reply || !_sentId) {
		return;
	}
	const auto item = _session->data().message(*_sentId);
	if (!item || !item->isRegular()) {
		return;
	}
	if (!_reply->isEmpty()) {
		const auto parts = Split(
			*_reply,
			kMaxTextChunk,
			Tx().transcriptContinued());
		for (const auto &part : parts) {
			auto message = ::Api::MessageToSend(::Api::SendAction(_history));
			message.action.replyTo = FullReplyTo{ .messageId = item->fullId() };
			message.textWithTags = TextWithTags{ part };
			_session->api().sendMessage(std::move(message));
		}
	}
	_destroy();
}

void Upload(Finished &&finished) {
	const auto session = finished.session.get();
	if (!session) {
		return; // Logged out while the call was ending: nothing to save to.
	}
	auto &list = Uploaders();
	const auto raw = std::make_shared<Uploader*>(nullptr);
	auto owned = std::make_unique<Uploader>(std::move(finished), [=] {
		// Later, not from inside one of its own callbacks.
		crl::on_main([=] {
			auto &list = Uploaders();
			const auto i = ranges::find(
				list,
				*raw,
				&std::unique_ptr<Uploader>::get);
			if (i != end(list)) {
				list.erase(i);
			}
		});
	});
	*raw = owned.get();
	list.push_back(std::move(owned));
	session->lifetime().add([=] {
		auto &list = Uploaders();
		const auto i = ranges::find(list, *raw, &std::unique_ptr<Uploader>::get);
		if (i != end(list)) {
			list.erase(i);
		}
	});
	(*raw)->start();
}

} // namespace

bool FeatureEnabled() {
	return Settings::IsEnabled(K(kFeatureKey));
}

bool AutoRecord() {
	return Settings::GetBool(K(kAutoKey), false);
}

void SetAutoRecord(bool enabled) {
	Settings::SetBool(K(kAutoKey), enabled);
}

bool AiTranscript() {
	return Settings::GetBool(K(kAiTranscriptKey), false);
}

void SetAiTranscript(bool enabled) {
	Settings::SetBool(K(kAiTranscriptKey), enabled);
}

PeerData *Target(not_null<Main::Session*> session) {
	const auto value = Settings::GetString(TargetKey(session));
	auto ok = false;
	const auto serialized = value.toULongLong(&ok);
	if (!ok || !serialized) {
		return nullptr;
	}
	const auto peer = session->data().peerLoaded(
		DeserializePeerId(serialized));
	return (peer && !peer->isSelf()) ? peer : nullptr;
}

void SetTarget(not_null<Main::Session*> session, PeerData *peer) {
	if (!peer || peer->isSelf()) {
		Settings::Remove(TargetKey(session));
	} else {
		Settings::SetString(
			TargetKey(session),
			QString::number(SerializePeerId(peer->id)));
	}
}

CallAudioTap::AudioDeviceModuleCreator WrapAudioDeviceModule(
		not_null<Calls::Call*> call,
		CallAudioTap::AudioDeviceModuleCreator inner) {
	if (!FeatureEnabled() || call->conferenceInvite()) {
		return inner;
	}
	auto &hubs = Hubs();
	for (auto i = begin(hubs); i != end(hubs);) {
		if (!i->second.call) {
			i = hubs.erase(i);
		} else {
			++i;
		}
	}
	auto hub = std::make_shared<CallAudioTap::Hub>();
	hubs[call] = HubEntry{ base::make_weak(call), hub };
	return CallAudioTap::Wrap(std::move(inner), std::move(hub));
}

void SetupPanel(
		not_null<Ui::RpWidget*> parent,
		not_null<Calls::Call*> call,
		not_null<Ui::RpWidget*> status,
		std::shared_ptr<Ui::Show> show,
		rpl::lifetime &lifetime) {
	if (call->conferenceInvite()) {
		return;
	}
	const auto weak = base::make_weak(call);
	lifetime.add([=] {
		// Panel moved to another call, or the call is gone / over. The same
		// call set up again keeps its recording (the button adopts it).
		const auto strong = weak.get();
		if (!strong || IsOver(strong->state())) {
			StopFor(call, true);
		}
	});

	call->mutedValue() | rpl::on_next([=](bool muted) {
		if (Current() && Current()->call() == call) {
			Current()->setMuted(muted);
		}
	}, lifetime);

	const auto autoHandled = lifetime.make_state<bool>(false);
	call->stateValue() | rpl::on_next([=](Calls::Call::State state) {
		if (IsOver(state)) {
			StopFor(call, true);
		} else if (state == Calls::Call::State::Established
			&& !*autoHandled
			&& FeatureEnabled()
			&& AutoRecord()) {
			*autoHandled = true;
			// The module is created with the controller, a moment before.
			crl::on_main(parent, [=] {
				if (call->state() == Calls::Call::State::Established) {
					Start(call, show);
				}
			});
		}
	}, lifetime);

	// Owned by the call's lifetime, deleted with it if the panel is alive.
	const auto owned = lifetime.make_state<base::unique_qptr<Ui::RoundButton>>(
		base::make_unique_q<Ui::RoundButton>(
			parent.get(),
			rpl::single(Tx().record()),
			st::defaultActiveButton));
	const auto button = owned->get();
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);

	const auto recordingHere = [=] {
		return Current() && (Current()->call() == call);
	};
	const auto timer = lifetime.make_state<base::Timer>();
	const auto refresh = [=] {
		const auto here = recordingHere();
		button->setText(rpl::single(here
			? Tx().recording(Duration(Current()->elapsed() / 1000))
			: Tx().record()));
		button->setAccessibleName(here
			? Tx().stopRecordingDescription()
			: Tx().record());
		if (here && !timer->isActive()) {
			timer->callEach(500);
		} else if (!here) {
			timer->cancel();
		}
	};
	timer->setCallback(refresh);

	const auto updateVisibility = [=] {
		const auto established = (call->state()
			== Calls::Call::State::Established);
		button->setVisible(FeatureEnabled()
			&& (established || recordingHere()));
	};
	rpl::merge(
		Settings::Changes() | rpl::to_empty,
		call->stateValue() | rpl::to_empty,
		CurrentChanges().events()
	) | rpl::on_next([=] {
		refresh();
		updateVisibility();
	}, lifetime);
	refresh();
	updateVisibility();

	rpl::combine(
		parent->widthValue(),
		status->geometryValue(),
		button->widthValue()
	) | rpl::on_next([=](int width, QRect status, int buttonWidth) {
		button->moveToLeft(
			(width - buttonWidth) / 2,
			status.y() + status.height() + style::ConvertScale(12));
	}, button->lifetime());

	button->setClickedCallback([=] {
		if (recordingHere()) {
			Stop(true);
		} else {
			Start(call, show);
		}
	});
}

void ConfirmEnable(
		not_null<Window::SessionController*> controller,
		Fn<void(bool)> done) {
	const auto tx = Tx();
	const auto answered = std::make_shared<bool>(false);
	const auto answer = [=](bool value) {
		if (!*answered) {
			*answered = true;
			done(value);
		}
	};
	controller->show(Ui::MakeConfirmBox({
		.text = tx.legalText(),
		.confirmed = [=](Fn<void()> close) {
			answer(true);
			close();
		},
		.cancelled = [=](Fn<void()> close) {
			answer(false);
			close();
		},
		.confirmText = tx.gotIt(),
		.cancelText = tx.cancel(),
		.title = tx.legalTitle(),
	}));
}

} // namespace Luckgram::CallRecording
