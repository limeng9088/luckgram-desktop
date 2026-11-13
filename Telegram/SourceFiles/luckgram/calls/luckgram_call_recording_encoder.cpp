/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/calls/luckgram_call_recording_encoder.h"

#include "ffmpeg/ffmpeg_bytes_io_wrap.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "luckgram/calls/luckgram_call_recording_logic.h"

extern "C" {
#include <libavutil/channel_layout.h>
} // extern "C"

#include <vector>

namespace Luckgram::CallRecording {

using namespace FFmpeg;

struct OggOpusEncoder::Private {
	bool open(const QString &title, const QString &artist);
	bool encodeFrame(int count); // count == 0: drain the codec.
	bool writePackets();

	WriteBytesWrap output;
	FormatPointer format;
	CodecPointer codec;
	FramePointer frame;
	AVStream *stream = nullptr;
	std::vector<float> pending;
	int frameSize = kFrame;
	bool smallLastFrame = false;
	int64_t pts = 0;
	qint64 samples = 0;
	bool failed = false;
	bool finished = false;
};

bool OggOpusEncoder::Private::open(
		const QString &title,
		const QString &artist) {
	format = MakeWriteFormatPointer(
		static_cast<void*>(&output),
		nullptr,
		&WriteBytesWrap::Write,
		&WriteBytesWrap::Seek,
		"opus"_q);
	if (!format) {
		return false;
	}
	const auto encoder = avcodec_find_encoder(format->oformat->audio_codec);
	if (!encoder) {
		LogError(u"avcodec_find_encoder"_q, u"opus"_q);
		return false;
	}
	stream = avformat_new_stream(format.get(), encoder);
	codec = CodecPointer(avcodec_alloc_context3(encoder));
	if (!stream || !codec) {
		return false;
	}
	codec->sample_fmt = AV_SAMPLE_FMT_FLTP;
	codec->bit_rate = kBitrate;
	codec->ch_layout = AV_CHANNEL_LAYOUT_MONO;
	codec->sample_rate = kRate;
	codec->time_base = AVRational{ 1, kRate };
	if (format->oformat->flags & AVFMT_GLOBALHEADER) {
		codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	auto error = AvErrorWrap(avcodec_open2(codec.get(), encoder, nullptr));
	if (error) {
		LogError(u"avcodec_open2"_q, error);
		return false;
	}
	if (codec->frame_size > 0) {
		frameSize = codec->frame_size;
	}
	smallLastFrame = (encoder->capabilities
		& (AV_CODEC_CAP_SMALL_LAST_FRAME | AV_CODEC_CAP_VARIABLE_FRAME_SIZE));
	error = AvErrorWrap(avcodec_parameters_from_context(
		stream->codecpar,
		codec.get()));
	if (error) {
		LogError(u"avcodec_parameters_from_context"_q, error);
		return false;
	}
	stream->time_base = codec->time_base;
	if (!title.isEmpty()) {
		av_dict_set(&format->metadata, "title", title.toUtf8().constData(), 0);
	}
	if (!artist.isEmpty()) {
		av_dict_set(&format->metadata, "artist", artist.toUtf8().constData(), 0);
	}
	error = AvErrorWrap(avformat_write_header(format.get(), nullptr));
	if (error) {
		LogError(u"avformat_write_header"_q, error);
		return false;
	}
	frame = MakeFramePointer();
	return frame != nullptr;
}

bool OggOpusEncoder::Private::writePackets() {
	auto packet = av_packet_alloc();
	const auto guard = gsl::finally([&] { av_packet_free(&packet); });
	while (true) {
		auto error = AvErrorWrap(avcodec_receive_packet(codec.get(), packet));
		if (error.code() == AVERROR(EAGAIN) || error.code() == AVERROR_EOF) {
			return true;
		} else if (error) {
			LogError(u"avcodec_receive_packet"_q, error);
			return false;
		}
		packet->stream_index = stream->index;
		av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
		error = AvErrorWrap(av_interleaved_write_frame(format.get(), packet));
		if (error) {
			LogError(u"av_interleaved_write_frame"_q, error);
			return false;
		}
	}
}

bool OggOpusEncoder::Private::encodeFrame(int count) {
	if (!count) {
		const auto error = AvErrorWrap(avcodec_send_frame(codec.get(), nullptr));
		if (error && error.code() != AVERROR_EOF) {
			LogError(u"avcodec_send_frame"_q, error);
			return false;
		}
		return writePackets();
	}
	av_frame_unref(frame.get());
	frame->format = codec->sample_fmt;
	av_channel_layout_copy(&frame->ch_layout, &codec->ch_layout);
	frame->sample_rate = kRate;
	frame->nb_samples = count;
	auto error = AvErrorWrap(av_frame_get_buffer(frame.get(), 0));
	if (error) {
		LogError(u"av_frame_get_buffer"_q, error);
		return false;
	}
	memcpy(frame->data[0], pending.data(), count * sizeof(float));
	frame->pts = pts;
	pts += count;
	error = AvErrorWrap(avcodec_send_frame(codec.get(), frame.get()));
	if (error.code() == AVERROR(EAGAIN)) {
		if (!writePackets()) {
			return false;
		}
		error = AvErrorWrap(avcodec_send_frame(codec.get(), frame.get()));
	}
	if (error) {
		LogError(u"avcodec_send_frame"_q, error);
		return false;
	}
	pending.erase(pending.begin(), pending.begin() + count);
	return writePackets();
}

OggOpusEncoder::OggOpusEncoder(const QString &title, const QString &artist)
: _private(std::make_unique<Private>()) {
	_private->failed = !_private->open(title, artist);
}

OggOpusEncoder::~OggOpusEncoder() = default;

bool OggOpusEncoder::valid() const {
	return !_private->failed && !_private->finished;
}

bool OggOpusEncoder::push(const int16_t *samples, int count) {
	if (!valid()) {
		return false;
	} else if (!samples || count <= 0) {
		return true;
	}
	auto &d = *_private;
	d.pending.reserve(d.pending.size() + count);
	for (auto i = 0; i != count; ++i) {
		d.pending.push_back(samples[i] / 32768.f);
	}
	d.samples += count;
	while (int(d.pending.size()) >= d.frameSize) {
		if (!d.encodeFrame(d.frameSize)) {
			d.failed = true;
			return false;
		}
	}
	return true;
}

QByteArray OggOpusEncoder::finish() {
	if (!valid()) {
		return QByteArray();
	}
	auto &d = *_private;
	d.finished = true;
	if (!d.pending.empty()) {
		if (!d.smallLastFrame) {
			d.pending.resize(d.frameSize, 0.f);
		}
		if (!d.encodeFrame(int(d.pending.size()))) {
			return QByteArray();
		}
	}
	if (!d.encodeFrame(0)) {
		return QByteArray();
	}
	const auto error = AvErrorWrap(av_write_trailer(d.format.get()));
	if (error) {
		LogError(u"av_write_trailer"_q, error);
		return QByteArray();
	}
	d.format = nullptr;
	return std::move(d.output.content);
}

qint64 OggOpusEncoder::bytes() const {
	return _private->output.content.size();
}

qint64 OggOpusEncoder::durationMs() const {
	return _private->samples * 1000 / kRate;
}

} // namespace Luckgram::CallRecording
