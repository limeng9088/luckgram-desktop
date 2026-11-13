/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/proxy/luckgram_proxy_exit.h"

#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/proxy/luckgram_proxy_exit_logic.h"
#include "mtproto/mtproto_proxy_data.h"

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <deque>

namespace Luckgram::ProxyExit {
namespace {

constexpr auto kDirectKey = "direct";

struct Entry {
	std::optional<Info> info;
	bool checking = false;
	bool failed = false;
	crl::time checkedAt = 0;
};

struct Job {
	QString key;
	std::optional<MTP::ProxyData> proxy; // nullopt: direct.
};

struct State {
	base::flat_map<QString, Entry> entries;
	std::deque<Job> queue;
	int running = 0;
	rpl::event_stream<> updates;
};

[[nodiscard]] State &Current() {
	static auto result = State();
	return result;
}

[[nodiscard]] QString KeyFor(const MTP::ProxyData *proxy) {
	if (!proxy) {
		return QString::fromLatin1(kDirectKey);
	} else if (!Supports(proxy)) {
		return QString();
	}
	// The password only as a hash: an edited proxy is checked again.
	return u"s5:%1:%2:%3:%4"_q
		.arg(proxy->host.toLower())
		.arg(proxy->port)
		.arg(proxy->user)
		.arg(quint64(qHash(proxy->password)));
}

void RunNext();

void Finish(const QString &key, std::optional<Info> info) {
	auto &state = Current();
	--state.running;
	auto &entry = state.entries[key];
	entry.checking = false;
	entry.info = std::move(info);
	entry.failed = !entry.info;
	entry.checkedAt = crl::now();
	state.updates.fire({});
	RunNext();
}

void Start(Job job, int baseIndex) {
	const auto bases = Api::Bases();
	if (baseIndex >= bases.size()) {
		Finish(job.key, std::nullopt);
		return;
	}
	const auto manager = new QNetworkAccessManager();
	if (job.proxy) {
		manager->setProxy(QNetworkProxy(
			QNetworkProxy::Socks5Proxy,
			job.proxy->host,
			quint16(job.proxy->port),
			job.proxy->user,
			job.proxy->password));
	} else {
		manager->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
	}
	auto request = QNetworkRequest(QUrl(bases[baseIndex] + kPath));
	request.setTransferTimeout(kTimeoutMs);
	request.setRawHeader("Accept", "application/json");
	const auto reply = manager->get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto body = reply->readAll();
		reply->deleteLater();
		manager->deleteLater();
		if (status > 0) {
			// Any HTTP answer stops (mobile specs#81 rule): only a network
			// failure moves on to the next entry.
			Finish(job.key, (status == 200) ? Parse(body) : std::nullopt);
		} else {
			Start(job, baseIndex + 1);
		}
	});
}

void RunNext() {
	auto &state = Current();
	while (state.running < kMaxParallel && !state.queue.empty()) {
		auto job = std::move(state.queue.front());
		state.queue.pop_front();
		++state.running;
		Start(std::move(job), 0);
	}
}

[[nodiscard]] QString ExitPrefix() {
	return Tr("出口：", "Exit: ");
}

[[nodiscard]] QString CheckingText() {
	return Tr("正在检测出口…", "Checking exit…");
}

[[nodiscard]] QString FailedText() {
	return Tr("出口检测失败", "Exit check failed");
}

} // namespace

bool Supports(const MTP::ProxyData *proxy) {
	return !proxy
		|| (proxy->type == MTP::ProxyData::Type::Socks5
			&& !proxy->host.isEmpty()
			&& proxy->port > 0);
}

void Check(const MTP::ProxyData *proxy, bool force) {
	const auto key = KeyFor(proxy);
	if (key.isEmpty()) {
		return;
	}
	auto &state = Current();
	auto &entry = state.entries[key];
	if (entry.checking) {
		return;
	} else if (!force
		&& !entry.failed
		&& entry.info
		&& IsFresh(entry.checkedAt, crl::now())) {
		return;
	}
	entry.checking = true;
	state.queue.push_back({
		.key = key,
		.proxy = proxy ? std::make_optional(*proxy) : std::nullopt,
	});
	state.updates.fire({});
	RunNext();
}

QString Text(const MTP::ProxyData *proxy) {
	const auto key = KeyFor(proxy);
	if (key.isEmpty()) {
		return QString();
	}
	const auto &entries = Current().entries;
	const auto i = entries.find(key);
	if (i == entries.end()) {
		return QString();
	}
	const auto &entry = i->second;
	if (entry.checking && !entry.info) {
		return CheckingText();
	} else if (entry.info) {
		return ExitPrefix() + Label(*entry.info, IsChinese());
	} else if (entry.failed) {
		return FailedText();
	}
	return QString();
}

QString DirectTitle() {
	return Tr("直连（不使用代理）", "Direct (no proxy)");
}

rpl::producer<> Updates() {
	return Current().updates.events();
}

} // namespace Luckgram::ProxyExit
