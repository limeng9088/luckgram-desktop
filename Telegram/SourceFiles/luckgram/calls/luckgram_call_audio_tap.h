/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace webrtc {
class AudioDeviceModule;
class TaskQueueFactory;
template <class T>
class scoped_refptr;
} // namespace webrtc

// D11 (mobile 04): what a one-on-one call says and hears, taken where
// WebRTC hands audio to and from the sound card. The upstream audio device
// module is wrapped (one-line hook in Calls::Call); every call goes on to it
// unchanged and only while a sink is set the PCM is copied to it.
//
// Own static library (lib_luckgram_call_tap): the only Luckgram code built
// against tg_owt headers, std only.
namespace Luckgram::CallAudioTap {

// PCM16 interleaved, `frames` per channel. Called on WebRTC audio threads.
struct Sink {
	virtual ~Sink() = default;
	virtual void recorded(
		const int16_t *pcm,
		int frames,
		int sampleRate,
		int channels) = 0;
	virtual void played(
		const int16_t *pcm,
		int frames,
		int sampleRate,
		int channels) = 0;
};

// Shared between the module and the recording, any thread.
class Hub final {
public:
	Hub();
	~Hub();

	void setSink(std::shared_ptr<Sink> sink); // nullptr detaches.

	void recorded(const void *pcm, size_t frames, size_t bytesPerFrame,
		size_t channels, uint32_t sampleRate);
	void played(const void *pcm, size_t frames, size_t bytesPerFrame,
		size_t channels, uint32_t sampleRate);

private:
	struct Private;
	const std::unique_ptr<Private> _private;

};

using AudioDeviceModuleCreator = std::function<
	webrtc::scoped_refptr<webrtc::AudioDeviceModule>(
		webrtc::TaskQueueFactory*)>;

[[nodiscard]] AudioDeviceModuleCreator Wrap(
	AudioDeviceModuleCreator inner,
	std::shared_ptr<Hub> hub);

} // namespace Luckgram::CallAudioTap
