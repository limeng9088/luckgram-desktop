/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/calls/luckgram_call_audio_tap.h"

namespace Calls {
class Call;
} // namespace Calls

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
class Show;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// D11 (mobile 04, spec limeng9088/luckgram-specs): call recording of
// one-on-one calls. Recorded mono into Ogg/Opus in memory, sent after the
// call as an audio file to Saved Messages (or the chosen chat), optional
// AI transcript and summary as a reply. No recording file on this computer.
namespace Luckgram::CallRecording {

[[nodiscard]] bool FeatureEnabled();
[[nodiscard]] bool AutoRecord();
void SetAutoRecord(bool enabled);
[[nodiscard]] bool AiTranscript();
void SetAiTranscript(bool enabled);

// Saved Messages when nullptr (or when the chat can't take files).
[[nodiscard]] PeerData *Target(not_null<Main::Session*> session);
void SetTarget(not_null<Main::Session*> session, PeerData *peer);

// Hook in Calls::Call::createAndStartController (upstream unchanged when
// the feature is off).
[[nodiscard]] CallAudioTap::AudioDeviceModuleCreator WrapAudioDeviceModule(
	not_null<Calls::Call*> call,
	CallAudioTap::AudioDeviceModuleCreator inner);

// Hook at the end of Calls::Panel::reinitWithCall: the Record button under
// the status line, auto-record, stop on hang-up. `lifetime` ends with the
// call (or when the panel switches to another one).
void SetupPanel(
	not_null<Ui::RpWidget*> parent,
	not_null<Calls::Call*> call,
	not_null<Ui::RpWidget*> status,
	std::shared_ptr<Ui::Show> show,
	rpl::lifetime &lifetime);

// Registry confirmEnable: the legal reminder (mobile "录音前请注意").
void ConfirmEnable(
	not_null<Window::SessionController*> controller,
	Fn<void(bool)> done);

} // namespace Luckgram::CallRecording
