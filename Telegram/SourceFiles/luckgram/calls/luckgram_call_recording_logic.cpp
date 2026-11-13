/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/calls/luckgram_call_recording_logic.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Luckgram::CallRecording {
namespace {

void Push(
		std::deque<int16_t> &fifo,
		const int16_t *pcm,
		int frames,
		int sampleRate,
		int channels,
		bool muted) {
	if (!pcm || frames <= 0 || sampleRate <= 0) {
		return;
	}
	auto mono = muted
		? std::vector<int16_t>(frames, 0)
		: ToMono(pcm, frames, std::max(1, channels));
	if (sampleRate != kRate) {
		mono = Resample(mono, sampleRate);
	}
	fifo.insert(fifo.end(), mono.begin(), mono.end());
}

} // namespace

std::vector<int16_t> ToMono(const int16_t *pcm, int frames, int channels) {
	auto result = std::vector<int16_t>(std::max(frames, 0));
	channels = std::max(1, channels);
	for (auto i = 0; i < frames; ++i) {
		auto sum = 0;
		for (auto c = 0; c != channels; ++c) {
			sum += pcm[i * channels + c];
		}
		result[i] = int16_t(sum / channels);
	}
	return result;
}

std::vector<int16_t> Resample(const std::vector<int16_t> &in, int fromRate) {
	const auto count = int(in.size());
	const auto outCount = int(int64_t(count) * kRate / std::max(fromRate, 1));
	auto out = std::vector<int16_t>(std::max(outCount, 0));
	if (!count || !outCount) {
		return out;
	}
	const auto step = double(fromRate) / kRate;
	for (auto i = 0; i != outCount; ++i) {
		const auto pos = i * step;
		const auto i0 = std::min(int(pos), count - 1);
		const auto i1 = std::min(i0 + 1, count - 1);
		const auto t = pos - int(pos);
		out[i] = int16_t(std::lround(in[i0] * (1 - t) + in[i1] * t));
	}
	return out;
}

void Mixer::pushNear(
		const int16_t *pcm,
		int frames,
		int sampleRate,
		int channels,
		bool muted) {
	Push(_near, pcm, frames, sampleRate, channels, muted);
}

void Mixer::pushFar(
		const int16_t *pcm,
		int frames,
		int sampleRate,
		int channels) {
	Push(_far, pcm, frames, sampleRate, channels, false);
}

int Mixer::nearSize() const {
	return int(_near.size());
}

int Mixer::farSize() const {
	return int(_far.size());
}

void Mixer::mix(
		bool useNear,
		bool useFar,
		int count,
		std::vector<int16_t> &out) {
	for (auto i = 0; i != count; ++i) {
		auto a = 0;
		auto b = 0;
		if (useNear && !_near.empty()) {
			a = _near.front();
			_near.pop_front();
		}
		if (useFar && !_far.empty()) {
			b = _far.front();
			_far.pop_front();
		}
		out.push_back(int16_t(std::clamp(
			a + b,
			int(std::numeric_limits<int16_t>::min()),
			int(std::numeric_limits<int16_t>::max()))));
	}
}

void Mixer::drain(std::vector<int16_t> &out) {
	while (true) {
		const auto n = nearSize();
		const auto f = farSize();
		const auto haveNear = (n >= kFrame);
		const auto haveFar = (f >= kFrame);
		if (haveNear && haveFar) {
			mix(true, true, kFrame, out);
		} else if (haveNear && n >= kMaxLagSamples) {
			mix(true, false, kFrame, out);
		} else if (haveFar && f >= kMaxLagSamples) {
			mix(false, true, kFrame, out);
		} else {
			return;
		}
	}
}

void Mixer::flush(std::vector<int16_t> &out) {
	while (std::max(nearSize(), farSize()) >= kFrame) {
		mix(nearSize() > 0, farSize() > 0, kFrame, out);
	}
	const auto count = std::max(nearSize(), farSize());
	if (count > 0) {
		mix(nearSize() > 0, farSize() > 0, count, out);
	}
}

QString Limit(const QString &text, int maxChars) {
	if (maxChars <= 0 || text.size() <= maxChars) {
		return text;
	}
	auto end = maxChars;
	const auto floor = maxChars - maxChars / 5;
	const auto nl = int(text.lastIndexOf(QChar('\n'), end));
	const auto sp = int(text.lastIndexOf(QChar(' '), end));
	if (nl >= floor) {
		end = nl;
	} else if (sp >= floor) {
		end = sp;
	}
	return text.left(end).trimmed();
}

QStringList Split(
		const QString &text,
		int maxChunk,
		const QString &continuedPrefix) {
	auto result = QStringList();
	auto start = 0;
	const auto size = int(text.size());
	while (start < size) {
		auto end = std::min(size, start + maxChunk);
		if (end < size) {
			const auto nl = int(text.lastIndexOf(QChar('\n'), end));
			const auto sp = int(text.lastIndexOf(QChar(' '), end));
			const auto half = start + maxChunk / 2;
			const auto brk = (nl > half) ? nl : (sp > half) ? sp : -1;
			if (brk > start) {
				end = brk;
			}
		}
		const auto part = text.mid(start, end - start).trimmed();
		if (!part.isEmpty()) {
			result.push_back(result.isEmpty()
				? part
				: (continuedPrefix + QChar('\n') + part));
		}
		start = end;
	}
	return result;
}

QString Duration(qint64 seconds) {
	const auto s = std::max(qint64(0), seconds);
	const auto two = [](qint64 v) {
		return QStringLiteral("%1").arg(v, 2, 10, QChar('0'));
	};
	if (s >= 3600) {
		return QString::number(s / 3600)
			+ QChar(':') + two((s / 60) % 60)
			+ QChar(':') + two(s % 60);
	}
	return QString::number(s / 60) + QChar(':') + two(s % 60);
}

QString FileDateTime(const QDateTime &when) {
	return when.toString(QStringLiteral("yyyy-MM-dd HH.mm.ss"));
}

QString SanitizeFileName(const QString &name) {
	auto result = name;
	for (auto &ch : result) {
		switch (ch.unicode()) {
		case '\\': case '/': case ':': case '*': case '?': case '"':
		case '<': case '>': case '|': case '\n': case '\r': case '\t':
			ch = QChar('_');
			break;
		}
	}
	return result.trimmed();
}

qint64 SegmentLimitBytes(qint64 transcribeMaxMb) {
	const auto mb = (transcribeMaxMb > 0)
		? transcribeMaxMb
		: qint64(kDefaultTranscribeMaxMb);
	return mb * 1024 * 1024 * 9 / 10;
}

qint64 DurationSeconds(qint64 durationMs) {
	return std::max(qint64(1), (durationMs + 500) / 1000);
}

QString Texts::t(const char *zh, const char *en) const {
	return QString::fromUtf8(_zh ? zh : en);
}

QString Texts::featureName() const { return t("通话录音", "Call Recording"); }
QString Texts::featureDescription() const { return t("一对一通话中录音，挂断后保存到收藏夹", "Record one-on-one calls and save them to Saved Messages"); }
QString Texts::enable() const { return t("启用通话录音", "Enable Call Recording"); }
QString Texts::enableInfo() const { return t("开启后，一对一语音和视频通话界面会出现「录音」按钮。群组通话和直播不支持录音。", "Adds a Record button to one-on-one voice and video calls. Group calls and live streams can't be recorded."); }
QString Texts::legalTitle() const { return t("录音前请注意", "Before You Record"); }
QString Texts::legalText() const { return t("各国和地区对通话录音的法律规定不同，录音前请先告知对方并取得同意。", "Laws on recording calls differ between countries and regions. Tell the other person and get their consent before recording."); }
QString Texts::gotIt() const { return t("我知道了", "Got It"); }
QString Texts::cancel() const { return t("取消", "Cancel"); }
QString Texts::autoRecord() const { return t("通话接通后自动录音", "Record Automatically"); }
QString Texts::autoRecordInfo() const { return t("一对一通话接通后自动开始录音，随时可以点击按钮停止。", "Starts recording as soon as a one-on-one call connects. Click the button to stop at any time."); }
QString Texts::aiTranscript() const { return t("AI 文字稿", "AI Transcript"); }
QString Texts::aiTranscriptInfo() const { return t("挂断后生成文字稿和 3–6 条要点总结，回复在录音下面。录音会发送到 Luckgram 的 AI 服务处理。", "After the call, a transcript and a 3–6 point summary are posted as a reply to the recording. The audio is sent to the Luckgram AI service for processing."); }
QString Texts::saveTo() const { return t("保存到", "Save To"); }
QString Texts::savedMessages() const { return t("收藏夹", "Saved Messages"); }
QString Texts::chooseChat() const { return t("选择聊天", "Choose a Chat"); }
QString Texts::resetToSaved() const { return t("改回收藏夹", "Use Saved Messages"); }
QString Texts::saveToInfo() const { return t("录音只保存在你自己的 Telegram 云端（收藏夹或你选的聊天），本机不保存录音文件。", "Recordings are kept only in your own Telegram cloud (Saved Messages or the chat you pick). No recording file is kept on this computer."); }
QString Texts::record() const { return t("录音", "Record"); }

QString Texts::recording(const QString &elapsed) const {
	return t("录音 ", "Recording ") + elapsed;
}

QString Texts::stopRecordingDescription() const { return t("停止录音", "Stop recording"); }
QString Texts::startFailed() const { return t("无法开始录音", "Couldn't start recording"); }

QString Texts::fileTitle(const QString &peerName, const QString &dateTime) const {
	return t("通话 ", "Call ") + peerName + QChar(' ') + dateTime;
}

QString Texts::caption(const QString &peerName, const QString &duration) const {
	return _zh
		? (QString::fromUtf8("与 ") + peerName + QString::fromUtf8(" 的通话 · ") + duration)
		: (QStringLiteral("Call with ") + peerName + QStringLiteral(" · ") + duration);
}

QString Texts::summaryHeader() const { return t("要点总结", "Summary"); }
QString Texts::transcriptHeader() const { return t("文字稿", "Transcript"); }
QString Texts::transcriptContinued() const { return t("文字稿（续）", "Transcript (continued)"); }
QString Texts::transcriptEmpty() const { return t("（没有识别到说话内容）", "(No speech detected)"); }

QString Texts::transcriptPartial(const QString &reason) const {
	return _zh
		? (QString::fromUtf8("（后面的部分未转写：") + reason + QString::fromUtf8("）"))
		: (QStringLiteral("(The rest wasn't transcribed: ") + reason + QChar(')'));
}

QString Texts::transcriptFailed(const QString &reason) const {
	return t("文字稿生成失败：", "Couldn't make a transcript: ") + reason;
}

QString Texts::reply(
		const QString &summary,
		const QString &transcript,
		const QString &partialReason) const {
	auto result = QString();
	if (!summary.trimmed().isEmpty()) {
		result += summaryHeader() + QChar('\n') + summary.trimmed() + QStringLiteral("\n\n");
	}
	result += transcriptHeader() + QChar('\n') + transcript.trimmed();
	if (!partialReason.isEmpty()) {
		result += QStringLiteral("\n\n") + transcriptPartial(partialReason);
	}
	return result;
}

QString Texts::savedToast(const QString &target) const {
	return _zh
		? (QString::fromUtf8("通话录音已保存到「") + target + QString::fromUtf8("」"))
		: (QStringLiteral("Call recording saved to ") + target);
}

} // namespace Luckgram::CallRecording
