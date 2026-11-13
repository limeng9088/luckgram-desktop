/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/calls/luckgram_call_audio_tap.h"

#include <api/make_ref_counted.h>
#include <modules/audio_device/include/audio_device.h>

#include <mutex>
#include <vector>

namespace Luckgram::CallAudioTap {
namespace {

using webrtc::AudioDeviceModule;
using webrtc::AudioTransport;

// Between WebRTC and the real transport: copies, then hands on unchanged.
class TransportTap final : public AudioTransport {
public:
	TransportTap(AudioTransport *inner, std::shared_ptr<Hub> hub)
	: _inner(inner)
	, _hub(std::move(hub)) {
	}

	int32_t RecordedDataIsAvailable(
			const void *audioSamples,
			size_t nSamples,
			size_t nBytesPerSample,
			size_t nChannels,
			uint32_t samplesPerSec,
			uint32_t totalDelayMS,
			int32_t clockDrift,
			uint32_t currentMicLevel,
			bool keyPressed,
			uint32_t &newMicLevel) override {
		_hub->recorded(
			audioSamples,
			nSamples,
			nBytesPerSample,
			nChannels,
			samplesPerSec);
		return _inner->RecordedDataIsAvailable(
			audioSamples,
			nSamples,
			nBytesPerSample,
			nChannels,
			samplesPerSec,
			totalDelayMS,
			clockDrift,
			currentMicLevel,
			keyPressed,
			newMicLevel);
	}

	int32_t RecordedDataIsAvailable(
			const void *audioSamples,
			size_t nSamples,
			size_t nBytesPerSample,
			size_t nChannels,
			uint32_t samplesPerSec,
			uint32_t totalDelayMS,
			int32_t clockDrift,
			uint32_t currentMicLevel,
			bool keyPressed,
			uint32_t &newMicLevel,
			absl::optional<int64_t> estimatedCaptureTimeNS) override {
		_hub->recorded(
			audioSamples,
			nSamples,
			nBytesPerSample,
			nChannels,
			samplesPerSec);
		return _inner->RecordedDataIsAvailable(
			audioSamples,
			nSamples,
			nBytesPerSample,
			nChannels,
			samplesPerSec,
			totalDelayMS,
			clockDrift,
			currentMicLevel,
			keyPressed,
			newMicLevel,
			estimatedCaptureTimeNS);
	}

	int32_t NeedMorePlayData(
			size_t nSamples,
			size_t nBytesPerSample,
			size_t nChannels,
			uint32_t samplesPerSec,
			void *audioSamples,
			size_t &nSamplesOut,
			int64_t *elapsed_time_ms,
			int64_t *ntp_time_ms) override {
		const auto result = _inner->NeedMorePlayData(
			nSamples,
			nBytesPerSample,
			nChannels,
			samplesPerSec,
			audioSamples,
			nSamplesOut,
			elapsed_time_ms,
			ntp_time_ms);
		if (result == 0) {
			_hub->played(
				audioSamples,
				nSamplesOut,
				nBytesPerSample,
				nChannels,
				samplesPerSec);
		}
		return result;
	}

	void PullRenderData(
			int bits_per_sample,
			int sample_rate,
			size_t number_of_channels,
			size_t number_of_frames,
			void *audio_data,
			int64_t *elapsed_time_ms,
			int64_t *ntp_time_ms) override {
		// Not what is played (used for the echo canceller): not taken.
		_inner->PullRenderData(
			bits_per_sample,
			sample_rate,
			number_of_channels,
			number_of_frames,
			audio_data,
			elapsed_time_ms,
			ntp_time_ms);
	}

private:
	AudioTransport * const _inner = nullptr;
	const std::shared_ptr<Hub> _hub;

};

// Not final: rtc::make_ref_counted derives RefCountedObject<ModuleTap>.
class ModuleTap : public AudioDeviceModule {
public:
	ModuleTap(
		rtc::scoped_refptr<AudioDeviceModule> inner,
		std::shared_ptr<Hub> hub)
	: _inner(std::move(inner))
	, _hub(std::move(hub)) {
	}

	int32_t ActiveAudioLayer(AudioLayer *audioLayer) const override {
		return _inner->ActiveAudioLayer(audioLayer);
	}
	int32_t RegisterAudioCallback(AudioTransport *audioCallback) override {
		auto tap = audioCallback
			? std::make_unique<TransportTap>(audioCallback, _hub)
			: nullptr;
		const auto result = _inner->RegisterAudioCallback(tap.get());
		// The old one stays alive: an audio thread may still be inside it.
		if (_transport) {
			_retired.push_back(std::move(_transport));
		}
		_transport = std::move(tap);
		return result;
	}

	int32_t Init() override { return _inner->Init(); }
	int32_t Terminate() override { return _inner->Terminate(); }
	bool Initialized() const override { return _inner->Initialized(); }

	int16_t PlayoutDevices() override { return _inner->PlayoutDevices(); }
	int16_t RecordingDevices() override {
		return _inner->RecordingDevices();
	}
	int32_t PlayoutDeviceName(
			uint16_t index,
			char name[webrtc::kAdmMaxDeviceNameSize],
			char guid[webrtc::kAdmMaxGuidSize]) override {
		return _inner->PlayoutDeviceName(index, name, guid);
	}
	int32_t RecordingDeviceName(
			uint16_t index,
			char name[webrtc::kAdmMaxDeviceNameSize],
			char guid[webrtc::kAdmMaxGuidSize]) override {
		return _inner->RecordingDeviceName(index, name, guid);
	}

	int32_t SetPlayoutDevice(uint16_t index) override {
		return _inner->SetPlayoutDevice(index);
	}
	int32_t SetPlayoutDevice(WindowsDeviceType device) override {
		return _inner->SetPlayoutDevice(device);
	}
	int32_t SetRecordingDevice(uint16_t index) override {
		return _inner->SetRecordingDevice(index);
	}
	int32_t SetRecordingDevice(WindowsDeviceType device) override {
		return _inner->SetRecordingDevice(device);
	}

	int32_t PlayoutIsAvailable(bool *available) override {
		return _inner->PlayoutIsAvailable(available);
	}
	int32_t InitPlayout() override { return _inner->InitPlayout(); }
	bool PlayoutIsInitialized() const override {
		return _inner->PlayoutIsInitialized();
	}
	int32_t RecordingIsAvailable(bool *available) override {
		return _inner->RecordingIsAvailable(available);
	}
	int32_t InitRecording() override { return _inner->InitRecording(); }
	bool RecordingIsInitialized() const override {
		return _inner->RecordingIsInitialized();
	}

	int32_t StartPlayout() override { return _inner->StartPlayout(); }
	int32_t StopPlayout() override { return _inner->StopPlayout(); }
	bool Playing() const override { return _inner->Playing(); }
	int32_t StartRecording() override { return _inner->StartRecording(); }
	int32_t StopRecording() override { return _inner->StopRecording(); }
	bool Recording() const override { return _inner->Recording(); }

	int32_t InitSpeaker() override { return _inner->InitSpeaker(); }
	bool SpeakerIsInitialized() const override {
		return _inner->SpeakerIsInitialized();
	}
	int32_t InitMicrophone() override { return _inner->InitMicrophone(); }
	bool MicrophoneIsInitialized() const override {
		return _inner->MicrophoneIsInitialized();
	}

	int32_t SpeakerVolumeIsAvailable(bool *available) override {
		return _inner->SpeakerVolumeIsAvailable(available);
	}
	int32_t SetSpeakerVolume(uint32_t volume) override {
		return _inner->SetSpeakerVolume(volume);
	}
	int32_t SpeakerVolume(uint32_t *volume) const override {
		return _inner->SpeakerVolume(volume);
	}
	int32_t MaxSpeakerVolume(uint32_t *maxVolume) const override {
		return _inner->MaxSpeakerVolume(maxVolume);
	}
	int32_t MinSpeakerVolume(uint32_t *minVolume) const override {
		return _inner->MinSpeakerVolume(minVolume);
	}

	int32_t MicrophoneVolumeIsAvailable(bool *available) override {
		return _inner->MicrophoneVolumeIsAvailable(available);
	}
	int32_t SetMicrophoneVolume(uint32_t volume) override {
		return _inner->SetMicrophoneVolume(volume);
	}
	int32_t MicrophoneVolume(uint32_t *volume) const override {
		return _inner->MicrophoneVolume(volume);
	}
	int32_t MaxMicrophoneVolume(uint32_t *maxVolume) const override {
		return _inner->MaxMicrophoneVolume(maxVolume);
	}
	int32_t MinMicrophoneVolume(uint32_t *minVolume) const override {
		return _inner->MinMicrophoneVolume(minVolume);
	}

	int32_t SpeakerMuteIsAvailable(bool *available) override {
		return _inner->SpeakerMuteIsAvailable(available);
	}
	int32_t SetSpeakerMute(bool enable) override {
		return _inner->SetSpeakerMute(enable);
	}
	int32_t SpeakerMute(bool *enabled) const override {
		return _inner->SpeakerMute(enabled);
	}

	int32_t MicrophoneMuteIsAvailable(bool *available) override {
		return _inner->MicrophoneMuteIsAvailable(available);
	}
	int32_t SetMicrophoneMute(bool enable) override {
		return _inner->SetMicrophoneMute(enable);
	}
	int32_t MicrophoneMute(bool *enabled) const override {
		return _inner->MicrophoneMute(enabled);
	}

	int32_t StereoPlayoutIsAvailable(bool *available) const override {
		return _inner->StereoPlayoutIsAvailable(available);
	}
	int32_t SetStereoPlayout(bool enable) override {
		return _inner->SetStereoPlayout(enable);
	}
	int32_t StereoPlayout(bool *enabled) const override {
		return _inner->StereoPlayout(enabled);
	}
	int32_t StereoRecordingIsAvailable(bool *available) const override {
		return _inner->StereoRecordingIsAvailable(available);
	}
	int32_t SetStereoRecording(bool enable) override {
		return _inner->SetStereoRecording(enable);
	}
	int32_t StereoRecording(bool *enabled) const override {
		return _inner->StereoRecording(enabled);
	}

	int32_t PlayoutDelay(uint16_t *delayMS) const override {
		return _inner->PlayoutDelay(delayMS);
	}

	bool BuiltInAECIsAvailable() const override {
		return _inner->BuiltInAECIsAvailable();
	}
	bool BuiltInAGCIsAvailable() const override {
		return _inner->BuiltInAGCIsAvailable();
	}
	bool BuiltInNSIsAvailable() const override {
		return _inner->BuiltInNSIsAvailable();
	}
	int32_t EnableBuiltInAEC(bool enable) override {
		return _inner->EnableBuiltInAEC(enable);
	}
	int32_t EnableBuiltInAGC(bool enable) override {
		return _inner->EnableBuiltInAGC(enable);
	}
	int32_t EnableBuiltInNS(bool enable) override {
		return _inner->EnableBuiltInNS(enable);
	}

	int32_t GetPlayoutUnderrunCount() const override {
		return _inner->GetPlayoutUnderrunCount();
	}
	absl::optional<Stats> GetStats() const override {
		return _inner->GetStats();
	}

#if defined(WEBRTC_IOS)
	int GetPlayoutAudioParameters(
			webrtc::AudioParameters *params) const override {
		return _inner->GetPlayoutAudioParameters(params);
	}
	int GetRecordAudioParameters(
			webrtc::AudioParameters *params) const override {
		return _inner->GetRecordAudioParameters(params);
	}
#endif // WEBRTC_IOS

	~ModuleTap() override {
		// The real module (and its audio threads) goes first, while the
		// transports it may still call are alive.
		_inner = nullptr;
	}

private:
	rtc::scoped_refptr<AudioDeviceModule> _inner;
	const std::shared_ptr<Hub> _hub;
	std::unique_ptr<TransportTap> _transport;
	std::vector<std::unique_ptr<TransportTap>> _retired;

};

} // namespace

struct Hub::Private {
	std::mutex mutex;
	std::shared_ptr<Sink> sink;

	[[nodiscard]] std::shared_ptr<Sink> current() {
		auto lock = std::lock_guard(mutex);
		return sink;
	}
};

Hub::Hub() : _private(std::make_unique<Private>()) {
}

Hub::~Hub() = default;

void Hub::setSink(std::shared_ptr<Sink> sink) {
	auto lock = std::lock_guard(_private->mutex);
	_private->sink = std::move(sink);
}

void Hub::recorded(
		const void *pcm,
		size_t frames,
		size_t bytesPerFrame,
		size_t channels,
		uint32_t sampleRate) {
	if (!pcm || !frames || !channels || bytesPerFrame != 2 * channels) {
		return; // Only PCM16, which is what WebRTC hands over.
	}
	if (const auto sink = _private->current()) {
		sink->recorded(
			static_cast<const int16_t*>(pcm),
			int(frames),
			int(sampleRate),
			int(channels));
	}
}

void Hub::played(
		const void *pcm,
		size_t frames,
		size_t bytesPerFrame,
		size_t channels,
		uint32_t sampleRate) {
	if (!pcm || !frames || !channels || bytesPerFrame != 2 * channels) {
		return;
	}
	if (const auto sink = _private->current()) {
		sink->played(
			static_cast<const int16_t*>(pcm),
			int(frames),
			int(sampleRate),
			int(channels));
	}
}

AudioDeviceModuleCreator Wrap(
		AudioDeviceModuleCreator inner,
		std::shared_ptr<Hub> hub) {
	return [=](webrtc::TaskQueueFactory *factory)
	-> rtc::scoped_refptr<AudioDeviceModule> {
		if (!inner) {
			return rtc::scoped_refptr<AudioDeviceModule>();
		}
		auto result = inner(factory);
		if (!result || !hub) {
			return result;
		}
		return rtc::make_ref_counted<ModuleTap>(std::move(result), hub);
	};
}

} // namespace Luckgram::CallAudioTap
