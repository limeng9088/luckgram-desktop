/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/media/luckgram_video_transcode.h"

#include "ffmpeg/ffmpeg_bytes_io_wrap.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "luckgram/discover/luckgram_square_logic.h"

#include <QtCore/QBuffer>
#include <QtGui/QImage>
#include <QtGui/QTransform>

extern "C" {
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
} // extern "C"

namespace Luckgram::Media {
namespace {

using namespace FFmpeg;

constexpr auto kAudioRate = 44100;
constexpr auto kAudioBitrate = 128'000;
constexpr auto kCoverQuality = 85;

struct Input {
	ReadFileWrap file;
	FormatPointer format;
	int videoIndex = -1;
	int audioIndex = -1;
	int rotation = 0;
};

[[nodiscard]] bool OpenInput(Input &input, const QString &path) {
	input.file.file.setFileName(path);
	if (!input.file.file.open(QIODevice::ReadOnly)) {
		return false;
	}
	input.format = MakeFormatPointer(
		&input.file,
		&ReadFileWrap::Read,
		nullptr,
		&ReadFileWrap::Seek);
	if (!input.format) {
		return false;
	}
	const auto error = AvErrorWrap(avformat_find_stream_info(
		input.format.get(),
		nullptr));
	if (error) {
		LogError(u"avformat_find_stream_info"_q, error);
		return false;
	}
	input.videoIndex = av_find_best_stream(
		input.format.get(),
		AVMEDIA_TYPE_VIDEO,
		-1,
		-1,
		nullptr,
		0);
	input.audioIndex = av_find_best_stream(
		input.format.get(),
		AVMEDIA_TYPE_AUDIO,
		-1,
		input.videoIndex,
		nullptr,
		0);
	if (input.videoIndex < 0) {
		return false;
	}
	input.rotation = ReadRotationFromMetadata(
		input.format->streams[input.videoIndex]);
	return true;
}

[[nodiscard]] qint64 DurationMs(const Input &input) {
	const auto stream = input.format->streams[input.videoIndex];
	if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
		return PtsToTime(stream->duration, stream->time_base);
	} else if (input.format->duration != AV_NOPTS_VALUE
		&& input.format->duration > 0) {
		return input.format->duration / 1000;
	}
	return 0;
}

[[nodiscard]] QSize DisplaySize(const Input &input) {
	const auto par = input.format->streams[input.videoIndex]->codecpar;
	return TransposeSizeByRotation(
		QSize(par->width, par->height),
		input.rotation);
}

[[nodiscard]] CodecPointer OpenDecoder(AVStream *stream) {
	const auto codec = avcodec_find_decoder(stream->codecpar->codec_id);
	if (!codec) {
		LogError(u"avcodec_find_decoder"_q);
		return {};
	}
	auto result = CodecPointer(avcodec_alloc_context3(codec));
	if (!result) {
		return {};
	}
	auto error = AvErrorWrap(avcodec_parameters_to_context(
		result.get(),
		stream->codecpar));
	if (error) {
		LogError(u"avcodec_parameters_to_context"_q, error);
		return {};
	}
	result->pkt_timebase = stream->time_base;
	error = AvErrorWrap(avcodec_open2(result.get(), codec, nullptr));
	if (error) {
		LogError(u"avcodec_open2"_q, error);
		return {};
	}
	return result;
}

[[nodiscard]] QByteArray EncodeJpeg(const QImage &image) {
	auto result = QByteArray();
	auto buffer = QBuffer(&result);
	buffer.open(QIODevice::WriteOnly);
	image.convertToFormat(QImage::Format_RGB32).save(
		&buffer,
		"JPEG",
		kCoverQuality);
	return result;
}

class Transcoder final {
public:
	explicit Transcoder(TranscodeArgs args, Fn<void(int)> progress)
	: _args(std::move(args))
	, _progress(std::move(progress)) {
	}
	~Transcoder() {
		if (_sws) {
			sws_freeContext(_sws);
		}
		if (_toYuv) {
			sws_freeContext(_toYuv);
		}
		if (_swr) {
			swr_free(&_swr);
		}
		if (_fifo) {
			av_audio_fifo_free(_fifo);
		}
	}

	TranscodeResult run();

private:
	bool openOutput();
	bool openVideoEncoder();
	bool openAudioEncoder();
	bool handleVideoFrame(AVFrame *frame);
	bool handleAudioFrame(AVFrame *frame);
	bool drainAudio(bool flush);
	bool encode(AVFrame *frame, const CodecPointer &codec, AVStream *stream);
	bool decode(
		const CodecPointer &decoder,
		AVPacket *packet,
		bool video);
	[[nodiscard]] QImage frameToImage(AVFrame *frame);
	void fail(const QString &error);

	const TranscodeArgs _args;
	const Fn<void(int)> _progress;
	Input _input;
	CodecPointer _videoDecoder;
	CodecPointer _audioDecoder;
	WriteBytesWrap _output;
	FormatPointer _format;
	CodecPointer _videoEncoder;
	CodecPointer _audioEncoder;
	AVStream *_videoStream = nullptr;
	AVStream *_audioStream = nullptr;
	FramePointer _videoFrame;
	FramePointer _audioFrame;
	SwsContext *_sws = nullptr;
	SwsContext *_toYuv = nullptr;
	SwrContext *_swr = nullptr;
	AVAudioFifo *_fifo = nullptr;
	QSize _size;
	qint64 _durationMs = 0;
	qint64 _limitMs = 0;
	qint64 _lastVideoMs = 0;
	int64_t _audioPts = 0;
	bool _videoStarted = false;
	bool _videoDone = false;
	bool _audioDone = false;
	int _lastProgress = -1;
	QByteArray _cover;
	QString _error;

};

void Transcoder::fail(const QString &error) {
	if (_error.isEmpty()) {
		_error = error;
	}
}

bool Transcoder::openOutput() {
	_format = MakeWriteFormatPointer(
		static_cast<void*>(&_output),
		nullptr,
		&WriteBytesWrap::Write,
		&WriteBytesWrap::Seek,
		"mp4"_q);
	return _format != nullptr;
}

bool Transcoder::openVideoEncoder() {
	auto codec = avcodec_find_encoder_by_name("libopenh264");
	if (!codec) {
		codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	}
	if (!codec) {
		LogError(u"avcodec_find_encoder"_q, u"H264"_q);
		return false;
	}
	_videoStream = avformat_new_stream(_format.get(), codec);
	_videoEncoder = CodecPointer(avcodec_alloc_context3(codec));
	if (!_videoStream || !_videoEncoder) {
		return false;
	}
	const auto source = _input.format->streams[_input.videoIndex];
	_videoEncoder->codec_id = codec->id;
	_videoEncoder->codec_type = AVMEDIA_TYPE_VIDEO;
	_videoEncoder->width = _size.width();
	_videoEncoder->height = _size.height();
	_videoEncoder->time_base = AVRational{ 1, 1000 }; // Milliseconds.
	_videoEncoder->framerate = source->avg_frame_rate;
	_videoEncoder->pix_fmt = AV_PIX_FMT_YUV420P;
	_videoEncoder->bit_rate = _args.videoBitrate
		? _args.videoBitrate
		: Square::VideoBitrate(
			_size.width(),
			_size.height(),
			int(std::min<int64_t>(
				source->codecpar->bit_rate,
				std::numeric_limits<int>::max())));
	if (_format->oformat->flags & AVFMT_GLOBALHEADER) {
		_videoEncoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	auto error = AvErrorWrap(avcodec_open2(
		_videoEncoder.get(),
		codec,
		nullptr));
	if (error) {
		LogError(u"avcodec_open2"_q, error, u"video"_q);
		return false;
	}
	error = AvErrorWrap(avcodec_parameters_from_context(
		_videoStream->codecpar,
		_videoEncoder.get()));
	if (error) {
		return false;
	}
	_videoStream->time_base = _videoEncoder->time_base;
	_videoFrame = MakeFramePointer();
	if (!_videoFrame) {
		return false;
	}
	_videoFrame->format = AV_PIX_FMT_YUV420P;
	_videoFrame->width = _size.width();
	_videoFrame->height = _size.height();
	error = AvErrorWrap(av_frame_get_buffer(_videoFrame.get(), 0));
	return !error;
}

bool Transcoder::openAudioEncoder() {
	const auto codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (!codec) {
		LogError(u"avcodec_find_encoder"_q, u"AAC"_q);
		return false;
	}
	_audioStream = avformat_new_stream(_format.get(), codec);
	_audioEncoder = CodecPointer(avcodec_alloc_context3(codec));
	if (!_audioStream || !_audioEncoder) {
		return false;
	}
	const auto channels = (_audioDecoder->ch_layout.nb_channels >= 2) ? 2 : 1;
	av_channel_layout_default(&_audioEncoder->ch_layout, channels);
	_audioEncoder->sample_fmt = AV_SAMPLE_FMT_FLTP;
	_audioEncoder->sample_rate = kAudioRate;
	_audioEncoder->bit_rate = kAudioBitrate;
	_audioEncoder->time_base = AVRational{ 1, kAudioRate };
	if (_format->oformat->flags & AVFMT_GLOBALHEADER) {
		_audioEncoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	auto error = AvErrorWrap(avcodec_open2(
		_audioEncoder.get(),
		codec,
		nullptr));
	if (error) {
		LogError(u"avcodec_open2"_q, error, u"audio"_q);
		return false;
	}
	error = AvErrorWrap(avcodec_parameters_from_context(
		_audioStream->codecpar,
		_audioEncoder.get()));
	if (error) {
		return false;
	}
	_audioStream->time_base = _audioEncoder->time_base;
	error = AvErrorWrap(swr_alloc_set_opts2(
		&_swr,
		&_audioEncoder->ch_layout,
		_audioEncoder->sample_fmt,
		_audioEncoder->sample_rate,
		&_audioDecoder->ch_layout,
		_audioDecoder->sample_fmt,
		_audioDecoder->sample_rate,
		0,
		nullptr));
	if (error || !_swr) {
		LogError(u"swr_alloc_set_opts2"_q, error);
		return false;
	}
	error = AvErrorWrap(swr_init(_swr));
	if (error) {
		LogError(u"swr_init"_q, error);
		return false;
	}
	_fifo = av_audio_fifo_alloc(
		_audioEncoder->sample_fmt,
		_audioEncoder->ch_layout.nb_channels,
		_audioEncoder->frame_size ? _audioEncoder->frame_size : 1024);
	_audioFrame = MakeFramePointer();
	if (!_fifo || !_audioFrame) {
		return false;
	}
	_audioFrame->nb_samples = _audioEncoder->frame_size
		? _audioEncoder->frame_size
		: 1024;
	_audioFrame->format = _audioEncoder->sample_fmt;
	_audioFrame->sample_rate = _audioEncoder->sample_rate;
	av_channel_layout_copy(&_audioFrame->ch_layout, &_audioEncoder->ch_layout);
	error = AvErrorWrap(av_frame_get_buffer(_audioFrame.get(), 0));
	return !error;
}

QImage Transcoder::frameToImage(AVFrame *frame) {
	auto image = QImage(
		frame->width,
		frame->height,
		QImage::Format_ARGB32);
	_sws = sws_getCachedContext(
		_sws,
		frame->width,
		frame->height,
		AVPixelFormat(frame->format),
		frame->width,
		frame->height,
		AV_PIX_FMT_BGRA,
		SWS_BICUBIC,
		nullptr,
		nullptr,
		nullptr);
	if (!_sws) {
		return QImage();
	}
	uint8_t *data[AV_NUM_DATA_POINTERS] = { image.bits() };
	int linesize[AV_NUM_DATA_POINTERS] = { int(image.bytesPerLine()) };
	sws_scale(
		_sws,
		frame->data,
		frame->linesize,
		0,
		frame->height,
		data,
		linesize);
	if (_input.rotation) {
		image = image.transformed(QTransform().rotate(_input.rotation));
	}
	if (_args.crop) {
		image = image.copy(_args.crop->intersected(image.rect()));
	}
	return image.scaled(
		_size,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
}

bool Transcoder::handleVideoFrame(AVFrame *frame) {
	const auto stream = _input.format->streams[_input.videoIndex];
	const auto pts = (frame->best_effort_timestamp != AV_NOPTS_VALUE)
		? frame->best_effort_timestamp
		: frame->pts;
	auto ms = (pts == AV_NOPTS_VALUE)
		? (_lastVideoMs + _args.startMs)
		: qint64(PtsToTime(pts - std::max<int64_t>(
			stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time,
			0), stream->time_base));
	if (ms < _args.startMs) {
		return true; // Before the chosen part (after a backward seek).
	}
	ms -= _args.startMs;
	if (ms >= _limitMs) {
		_videoDone = true;
		return true;
	} else if (_videoStarted && ms <= _lastVideoMs) {
		return true; // Out of order / same millisecond: dropped.
	}
	_videoStarted = true;
	_lastVideoMs = ms;

	const auto image = frameToImage(frame);
	if (image.isNull()) {
		fail(u"convert"_q);
		return false;
	}
	if (_cover.isEmpty()) {
		_cover = EncodeJpeg(image);
	}
	auto error = AvErrorWrap(av_frame_make_writable(_videoFrame.get()));
	if (error) {
		return false;
	}
	_toYuv = sws_getCachedContext(
		_toYuv,
		image.width(),
		image.height(),
		AV_PIX_FMT_BGRA,
		_size.width(),
		_size.height(),
		AV_PIX_FMT_YUV420P,
		SWS_BICUBIC,
		nullptr,
		nullptr,
		nullptr);
	if (!_toYuv) {
		return false;
	}
	const uint8_t *src[AV_NUM_DATA_POINTERS] = { image.constBits() };
	const int srcLinesize[AV_NUM_DATA_POINTERS] = {
		int(image.bytesPerLine()),
	};
	sws_scale(
		_toYuv,
		src,
		srcLinesize,
		0,
		image.height(),
		_videoFrame->data,
		_videoFrame->linesize);
	_videoFrame->pts = ms;
	if (_progress && _durationMs > 0) {
		const auto percent = int(std::clamp(
			ms * 100 / std::min(_durationMs, _limitMs),
			qint64(0),
			qint64(99)));
		if (percent != _lastProgress) {
			_lastProgress = percent;
			_progress(percent);
		}
	}
	return encode(_videoFrame.get(), _videoEncoder, _videoStream);
}

bool Transcoder::drainAudio(bool flush) {
	const auto frameSize = _audioFrame->nb_samples;
	while (av_audio_fifo_size(_fifo) >= frameSize
		|| (flush && av_audio_fifo_size(_fifo) > 0)) {
		if (_audioPts * 1000 / kAudioRate >= _limitMs) {
			_audioDone = true;
			av_audio_fifo_reset(_fifo);
			return true;
		}
		auto error = AvErrorWrap(av_frame_make_writable(_audioFrame.get()));
		if (error) {
			return false;
		}
		const auto read = av_audio_fifo_read(
			_fifo,
			reinterpret_cast<void**>(_audioFrame->data),
			frameSize);
		if (read <= 0) {
			return false;
		}
		_audioFrame->nb_samples = read;
		_audioFrame->pts = _audioPts;
		_audioPts += read;
		if (!encode(_audioFrame.get(), _audioEncoder, _audioStream)) {
			return false;
		}
		_audioFrame->nb_samples = frameSize;
	}
	return true;
}

bool Transcoder::handleAudioFrame(AVFrame *frame) {
	if (_audioDone) {
		return true;
	} else if (_args.startMs > 0) {
		const auto stream = _input.format->streams[_input.audioIndex];
		const auto pts = (frame->best_effort_timestamp != AV_NOPTS_VALUE)
			? frame->best_effort_timestamp
			: frame->pts;
		const auto start = (stream->start_time == AV_NOPTS_VALUE)
			? int64_t(0)
			: stream->start_time;
		if (pts != AV_NOPTS_VALUE
			&& PtsToTime(pts - start, stream->time_base) < _args.startMs) {
			return true;
		}
	}
	const auto outSamples = int(av_rescale_rnd(
		swr_get_delay(_swr, _audioDecoder->sample_rate) + frame->nb_samples,
		kAudioRate,
		_audioDecoder->sample_rate,
		AV_ROUND_UP));
	uint8_t **converted = nullptr;
	auto error = AvErrorWrap(av_samples_alloc_array_and_samples(
		&converted,
		nullptr,
		_audioEncoder->ch_layout.nb_channels,
		outSamples,
		_audioEncoder->sample_fmt,
		0));
	if (error) {
		return false;
	}
	const auto count = swr_convert(
		_swr,
		converted,
		outSamples,
		const_cast<const uint8_t**>(frame->extended_data),
		frame->nb_samples);
	auto ok = (count >= 0);
	if (ok && count > 0) {
		ok = (av_audio_fifo_write(
			_fifo,
			reinterpret_cast<void**>(converted),
			count) == count);
	}
	av_freep(&converted[0]);
	av_freep(&converted);
	return ok && drainAudio(false);
}

bool Transcoder::encode(
		AVFrame *frame,
		const CodecPointer &codec,
		AVStream *stream) {
	auto error = AvErrorWrap(avcodec_send_frame(codec.get(), frame));
	if (error && error.code() != AVERROR_EOF) {
		LogError(u"avcodec_send_frame"_q, error);
		return false;
	}
	auto packet = av_packet_alloc();
	const auto guard = gsl::finally([&] { av_packet_free(&packet); });
	while (true) {
		error = AvErrorWrap(avcodec_receive_packet(codec.get(), packet));
		if (error.code() == AVERROR(EAGAIN) || error.code() == AVERROR_EOF) {
			return true;
		} else if (error) {
			LogError(u"avcodec_receive_packet"_q, error);
			return false;
		}
		packet->stream_index = stream->index;
		av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
		error = AvErrorWrap(av_interleaved_write_frame(_format.get(), packet));
		if (error) {
			LogError(u"av_interleaved_write_frame"_q, error);
			return false;
		}
	}
}

bool Transcoder::decode(
		const CodecPointer &decoder,
		AVPacket *packet,
		bool video) {
	auto error = AvErrorWrap(avcodec_send_packet(decoder.get(), packet));
	if (error && error.code() != AVERROR_EOF) {
		// A broken packet is skipped, like the players do.
		return true;
	}
	auto frame = MakeFramePointer();
	while (true) {
		error = AvErrorWrap(avcodec_receive_frame(decoder.get(), frame.get()));
		if (error.code() == AVERROR(EAGAIN) || error.code() == AVERROR_EOF) {
			return true;
		} else if (error) {
			return true;
		}
		const auto ok = video
			? handleVideoFrame(frame.get())
			: handleAudioFrame(frame.get());
		av_frame_unref(frame.get());
		if (!ok) {
			return false;
		}
	}
}

TranscodeResult Transcoder::run() {
	auto result = TranscodeResult();
	if (!OpenInput(_input, _args.path)) {
		result.error = u"open"_q;
		return result;
	}
	const auto display = DisplaySize(_input);
	const auto scaled = Square::ScaledSize(
		display.width(),
		display.height(),
		_args.maxEdge);
	_size = (_args.squareSide > 0)
		? QSize(_args.squareSide / 2 * 2, _args.squareSide / 2 * 2)
		: QSize(
			std::max(2, scaled.width / 2 * 2),
			std::max(2, scaled.height / 2 * 2));
	_durationMs = DurationMs(_input);
	_limitMs = (_args.maxDurationMs > 0)
		? _args.maxDurationMs
		: std::numeric_limits<qint64>::max();

	_videoDecoder = OpenDecoder(_input.format->streams[_input.videoIndex]);
	if (!_videoDecoder) {
		result.error = u"decoder"_q;
		return result;
	}
	if (_args.withAudio && _input.audioIndex >= 0) {
		_audioDecoder = OpenDecoder(
			_input.format->streams[_input.audioIndex]);
	}
	if (!openOutput()
		|| !openVideoEncoder()
		|| (_audioDecoder && !openAudioEncoder())) {
		result.error = u"encoder"_q;
		return result;
	}
	auto error = AvErrorWrap(avformat_write_header(_format.get(), nullptr));
	if (error) {
		LogError(u"avformat_write_header"_q, error);
		result.error = u"header"_q;
		return result;
	}
	_audioDone = !_audioDecoder;
	if (_args.startMs > 0) {
		av_seek_frame(
			_input.format.get(),
			-1,
			_args.startMs * (AV_TIME_BASE / 1000),
			AVSEEK_FLAG_BACKWARD);
		_durationMs = std::max(qint64(0), _durationMs - _args.startMs);
	}

	auto packet = av_packet_alloc();
	const auto guard = gsl::finally([&] { av_packet_free(&packet); });
	auto ok = true;
	const auto cancelled = [&] {
		return _args.cancelled && _args.cancelled->load();
	};
	while (ok && !(_videoDone && _audioDone)) {
		if (cancelled()) {
			result.error = u"cancelled"_q;
			return result;
		} else if (av_read_frame(_input.format.get(), packet) < 0) {
			break;
		}
		if (packet->stream_index == _input.videoIndex && !_videoDone) {
			ok = decode(_videoDecoder, packet, true);
		} else if (_audioDecoder
			&& packet->stream_index == _input.audioIndex
			&& !_audioDone) {
			ok = decode(_audioDecoder, packet, false);
		}
		av_packet_unref(packet);
	}
	if (ok && !_videoDone) {
		ok = decode(_videoDecoder, nullptr, true);
	}
	if (ok && _audioDecoder && !_audioDone) {
		ok = decode(_audioDecoder, nullptr, false) && drainAudio(true);
	}
	if (ok) {
		ok = encode(nullptr, _videoEncoder, _videoStream)
			&& (!_audioEncoder
				|| encode(nullptr, _audioEncoder, _audioStream));
	}
	if (ok) {
		error = AvErrorWrap(av_write_trailer(_format.get()));
		ok = !error;
	}
	if (!ok || _cover.isEmpty()) {
		result.error = _error.isEmpty() ? u"encode"_q : _error;
		return result;
	}
	_format = nullptr;
	if (_progress) {
		_progress(100);
	}
	result.mp4 = base::take(_output.content);
	result.coverJpeg = base::take(_cover);
	result.width = _size.width();
	result.height = _size.height();
	result.durationMs = std::min(_durationMs ? _durationMs : _lastVideoMs, _limitMs);
	return result;
}

} // namespace

VideoInfo Probe(const QString &path) {
	auto input = Input();
	if (!OpenInput(input, path)) {
		return {};
	}
	const auto size = DisplaySize(input);
	return {
		.width = size.width(),
		.height = size.height(),
		.durationMs = DurationMs(input),
		.ok = (size.width() > 0 && size.height() > 0),
	};
}

QImage ReadFrame(const QString &path, qint64 atMs) {
	auto input = Input();
	if (!OpenInput(input, path)) {
		return QImage();
	}
	const auto stream = input.format->streams[input.videoIndex];
	auto decoder = OpenDecoder(stream);
	if (!decoder) {
		return QImage();
	}
	if (atMs > 0) {
		av_seek_frame(
			input.format.get(),
			-1,
			atMs * (AV_TIME_BASE / 1000),
			AVSEEK_FLAG_BACKWARD);
	}
	const auto start = (stream->start_time == AV_NOPTS_VALUE)
		? int64_t(0)
		: stream->start_time;
	auto packet = av_packet_alloc();
	auto frame = MakeFramePointer();
	const auto guard = gsl::finally([&] { av_packet_free(&packet); });
	auto result = QImage();
	auto sws = (SwsContext*)nullptr;
	while (result.isNull() && av_read_frame(input.format.get(), packet) >= 0) {
		if (packet->stream_index == input.videoIndex
			&& avcodec_send_packet(decoder.get(), packet) >= 0) {
			while (result.isNull()
				&& avcodec_receive_frame(decoder.get(), frame.get()) >= 0) {
				const auto pts = (frame->best_effort_timestamp != AV_NOPTS_VALUE)
					? frame->best_effort_timestamp
					: frame->pts;
				if (pts != AV_NOPTS_VALUE
					&& PtsToTime(pts - start, stream->time_base) < atMs) {
					av_frame_unref(frame.get());
					continue;
				}
				auto image = QImage(
					frame->width,
					frame->height,
					QImage::Format_ARGB32);
				sws = sws_getCachedContext(
					sws,
					frame->width,
					frame->height,
					AVPixelFormat(frame->format),
					frame->width,
					frame->height,
					AV_PIX_FMT_BGRA,
					SWS_BICUBIC,
					nullptr,
					nullptr,
					nullptr);
				if (sws) {
					uint8_t *data[AV_NUM_DATA_POINTERS] = { image.bits() };
					int linesize[AV_NUM_DATA_POINTERS] = {
						int(image.bytesPerLine()),
					};
					sws_scale(
						sws,
						frame->data,
						frame->linesize,
						0,
						frame->height,
						data,
						linesize);
					result = input.rotation
						? image.transformed(
							QTransform().rotate(input.rotation))
						: image;
				}
				av_frame_unref(frame.get());
			}
		}
		av_packet_unref(packet);
	}
	if (sws) {
		sws_freeContext(sws);
	}
	return result;
}

TranscodeResult Transcode(const TranscodeArgs &args, Fn<void(int)> progress) {
	auto transcoder = Transcoder(args, std::move(progress));
	return transcoder.run();
}

} // namespace Luckgram::Media
