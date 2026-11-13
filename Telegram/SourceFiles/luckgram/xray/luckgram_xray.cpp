/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/xray/luckgram_xray.h"

#include "base/timer.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "luckgram/luckgram_secure_store.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_storage.h"
#include "luckgram/luckgram_strings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QPointer>
#include <QtCore/QProcess>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>

#include <openssl/rand.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif // Q_OS_WIN

namespace Luckgram::Xray {
namespace {

constexpr auto kLocalHost = "127.0.0.1";
constexpr auto kUser = "lg";
// Settings-level storage (docs/desktop.md): needed before login and before
// the local passcode, encrypted like upstream's proxy list.
constexpr auto kStorageName = "xray_nodes";
// Before luckgram-desktop#35 (never shipped): own AES key in the secure
// store, data in luckgram.json. Removed when found.
constexpr auto kOldStoreKey = "_xray_nodes";
constexpr auto kOldSecretName = "xray_key";
// The core counts as running once it is still alive after this long.
constexpr auto kStartedAfter = crl::time(800);

struct Rec {
	Node node;
	int localPort = 0;
	QJsonObject outbound;
};

struct State {
	bool started = false;
	bool loaded = false;
	bool enabled = false; // Enabled() as of the last Apply().
	bool applyScheduled = false;
	QString password;
	int portBase = 0;
	std::vector<Rec> nodes;

	QPointer<QProcess> process;
	bool running = false;
	QString lastError;
	base::Timer startedTimer;
	rpl::event_stream<> changes;
	rpl::lifetime lifetime;
};

[[nodiscard]] State &Data() {
	static auto result = State();
	return result;
}

[[nodiscard]] QString BinaryPath() {
#ifdef Q_OS_WIN
	const auto name = u"xray.exe"_q;
#else // Q_OS_WIN
	const auto name = u"xray"_q;
#endif // Q_OS_WIN
	return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}

[[nodiscard]] QByteArray RandomBytes(int size) {
	auto result = QByteArray(size, Qt::Uninitialized);
	if (RAND_bytes(
			reinterpret_cast<unsigned char*>(result.data()),
			size) != 1) {
		return QByteArray();
	}
	return result;
}

void Save() {
	auto &data = Data();
	if (!data.loaded) {
		// The stored list was never read (settings storage not ready):
		// writing now would replace it with an incomplete one.
		return;
	}
	auto nodes = QJsonArray();
	for (const auto &rec : data.nodes) {
		nodes.push_back(QJsonObject{
			{ u"link"_q, rec.node.link },
			{ u"local"_q, rec.localPort },
		});
	}
	const auto plain = QJsonDocument(QJsonObject{
		{ u"pass"_q, data.password },
		{ u"base"_q, data.portBase },
		{ u"nodes"_q, nodes },
	}).toJson(QJsonDocument::Compact);
	if (!Storage::WriteSettings(QString::fromLatin1(kStorageName), plain)) {
		LOG(("Luckgram Xray: could not save the nodes."));
	}
}

void Load() {
	auto &data = Data();
	if (data.loaded || !Storage::SettingsAvailable()) {
		return;
	}
	data.loaded = true;
	data.nodes.clear();
	if (Settings::Has(QString::fromLatin1(kOldStoreKey))) {
		Settings::Remove(QString::fromLatin1(kOldStoreKey));
		SecureStore::Remove(kOldSecretName);
	}
	const auto plain = Storage::ReadSettings(
		QString::fromLatin1(kStorageName));
	const auto json = QJsonDocument::fromJson(plain).object();
	data.password = json.value(u"pass"_q).toString();
	data.portBase = json.value(u"base"_q).toInt();
	for (const auto &value : json.value(u"nodes"_q).toArray()) {
		const auto item = value.toObject();
		const auto node = Parse(item.value(u"link"_q).toString());
		const auto local = item.value(u"local"_q).toInt();
		auto outbound = node ? Outbound(node->link) : QJsonObject();
		if (node && local > 0 && !outbound.isEmpty()) {
			data.nodes.push_back({ *node, local, std::move(outbound) });
		}
	}
	if (data.password.isEmpty() || data.portBase < 20000) {
		data.password = QString::fromLatin1(RandomBytes(18).toBase64(
			QByteArray::Base64UrlEncoding
				| QByteArray::OmitTrailingEquals));
		const auto random = RandomBytes(4);
		const auto value = (random.size() == 4)
			? (quint32(uchar(random[0])) << 24
				| quint32(uchar(random[1])) << 16
				| quint32(uchar(random[2])) << 8
				| quint32(uchar(random[3])))
			: quint32(0);
		data.portBase = 20000 + int(value % 35000);
		Save();
	}
}

[[nodiscard]] MTP::ProxyData ProxyFor(const Rec &rec) {
	auto result = MTP::ProxyData();
	result.type = MTP::ProxyData::Type::Socks5;
	result.host = QString::fromLatin1(kLocalHost);
	result.port = uint32(rec.localPort);
	result.user = QString::fromLatin1(kUser);
	result.password = Data().password;
	return result;
}

[[nodiscard]] bool IsLoopbackOfOurs(const MTP::ProxyData &proxy) {
	return (proxy.type == MTP::ProxyData::Type::Socks5)
		&& (proxy.host == QString::fromLatin1(kLocalHost))
		&& (proxy.user == QString::fromLatin1(kUser));
}

#ifdef Q_OS_WIN
// All cores are in one job that dies with the app (even after a crash or
// a kill), so no orphan xray keeps the ports.
void AddToKillOnCloseJob(qint64 pid) {
	static const auto job = [] {
		const auto result = CreateJobObjectW(nullptr, nullptr);
		if (result) {
			auto info = JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
			info.BasicLimitInformation.LimitFlags
				= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(
				result,
				JobObjectExtendedLimitInformation,
				&info,
				sizeof(info));
		}
		return result; // Kept open for the app's lifetime on purpose.
	}();
	if (!job || pid <= 0) {
		return;
	}
	const auto process = OpenProcess(
		PROCESS_SET_QUOTA | PROCESS_TERMINATE,
		FALSE,
		DWORD(pid));
	if (process) {
		if (!AssignProcessToJobObject(job, process)) {
			LOG(("Luckgram Xray: could not add the core to the job."));
		}
		CloseHandle(process);
	}
}
#endif // Q_OS_WIN

// A loopback port some other program (or a left-over core) already holds.
[[nodiscard]] int FirstBusyPort(const std::vector<Run> &runs) {
	for (const auto &run : runs) {
		auto server = QTcpServer();
		if (!server.listen(QHostAddress::LocalHost, quint16(run.localPort))) {
			return run.localPort;
		}
		server.close();
	}
	return 0;
}

void SetState(bool running, QString error) {
	auto &data = Data();
	data.running = running;
	data.lastError = std::move(error);
	data.changes.fire({});
}

void Stop() {
	auto &data = Data();
	data.startedTimer.cancel();
	if (const auto process = data.process.data()) {
		data.process = nullptr;
		QObject::disconnect(process, nullptr, nullptr, nullptr);
		process->kill();
		process->waitForFinished(1000);
		delete process;
	}
}

} // namespace

void Start() {
	auto &data = Data();
	if (data.started) {
		return;
	}
	data.started = true;
	Load();
	data.startedTimer.setCallback([] {
		auto &data = Data();
		if (data.process && data.process->state() == QProcess::Running) {
			SetState(true, QString());
		}
	});
	// The switch in Luckgram Features, or the server turning the feature
	// off (remote config merged: empty key). Restart only on a change.
	Settings::Changes(
	) | rpl::filter([](const QString &key) {
		return (key.isEmpty() || key == QString::fromLatin1(kFeatureKey))
			&& (Enabled() != Data().enabled);
	}) | rpl::on_next([] {
		Apply();
	}, data.lifetime);
	QObject::connect(
		QCoreApplication::instance(),
		&QCoreApplication::aboutToQuit,
		[] { Stop(); });
	Reconcile();
	Apply();
}

bool Available() {
	return QFileInfo(BinaryPath()).isExecutable();
}

bool Enabled() {
	return Settings::IsEnabled(QString::fromLatin1(kFeatureKey))
		&& Available();
}

bool Running() {
	return Data().running;
}

QString Problem() {
	auto &data = Data();
	if (!Available()) {
		return Tr("这个版本没有内置代理内核", "This build has no built-in proxy core");
	} else if (!Settings::IsEnabled(QString::fromLatin1(kFeatureKey))) {
		return Tr(
			"内置代理内核已关闭（设置 → Luckgram 功能）",
			"Built-in proxy core is off (Settings → Luckgram features)");
	} else if (data.running) {
		return QString();
	} else if (data.lastError.isEmpty()) {
		return Tr("内核启动中…", "Starting core…");
	}
	return Tr("内核未运行：", "Core not running: ") + data.lastError;
}

rpl::producer<> StateChanges() {
	return Data().changes.events();
}

std::optional<MTP::ProxyData> Register(const Node &node) {
	Load();
	auto &data = Data();
	const auto key = node.key();
	for (auto &rec : data.nodes) {
		if (rec.node.key() == key) {
			if (rec.node.name != node.name) {
				rec.node = node; // Renamed in the subscription.
				Save();
			}
			return ProxyFor(rec);
		}
	}
	auto outbound = Outbound(node.link);
	if (outbound.isEmpty()) {
		return std::nullopt;
	}
	auto used = std::set<int>();
	for (const auto &rec : data.nodes) {
		used.emplace(rec.localPort);
	}
	const auto ports = PickPorts(data.portBase, used, 1);
	if (ports.empty()) {
		return std::nullopt;
	}
	data.nodes.push_back({ node, ports.front(), std::move(outbound) });
	Save();
	const auto result = ProxyFor(data.nodes.back());
	if (!data.applyScheduled) {
		// Once, after the whole import batch.
		data.applyScheduled = true;
		crl::on_main(&Core::App(), [] {
			Data().applyScheduled = false;
			Apply();
		});
	}
	return result;
}

std::optional<Node> Find(const MTP::ProxyData &proxy) {
	if (!IsLoopbackOfOurs(proxy)) {
		return std::nullopt;
	}
	Load();
	for (const auto &rec : Data().nodes) {
		if (rec.localPort == int(proxy.port)) {
			return rec.node;
		}
	}
	return std::nullopt;
}

void Reconcile() {
	Load();
	auto &data = Data();
	auto live = std::set<int>();
	for (const auto &proxy : Core::App().settings().proxy().list()) {
		if (IsLoopbackOfOurs(proxy)) {
			live.emplace(int(proxy.port));
		}
	}
	const auto before = data.nodes.size();
	data.nodes.erase(ranges::remove_if(data.nodes, [&](const Rec &rec) {
		return !live.contains(rec.localPort);
	}), end(data.nodes));
	if (data.nodes.size() != before) {
		Save();
		Apply();
	}
}

void Apply() {
	Load();
	auto &data = Data();
	Stop();
	data.enabled = Enabled();
	if (!data.enabled || data.nodes.empty()) {
		SetState(false, QString());
		return;
	}
	auto runs = std::vector<Run>();
	for (const auto &rec : data.nodes) {
		runs.push_back({ rec.localPort, rec.outbound });
	}
	if (const auto busy = FirstBusyPort(runs)) {
		// Don't start: xray would exit anyway. Say which port.
		LOG(("Luckgram Xray: local port %1 is in use.").arg(busy));
		SetState(false, Luckgram::IsChinese()
			? (u"本机端口 "_q + QString::number(busy) + u" 被占用"_q)
			: (u"local port "_q + QString::number(busy) + u" is in use"_q));
		return;
	}
	const auto config = BuildConfig(
		runs,
		QString::fromLatin1(kUser),
		data.password);

	const auto process = new QProcess();
	data.process = process;
	process->setProgram(BinaryPath());
	process->setArguments({ u"run"_q, u"-c"_q, u"stdin:"_q });
	process->setWorkingDirectory(QFileInfo(BinaryPath()).absolutePath());
	// xray's output may name hosts: never kept or logged.
	process->setStandardOutputFile(QProcess::nullDevice());
	process->setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
	process->setCreateProcessArgumentsModifier([](
			QProcess::CreateProcessArguments *args) {
		args->flags |= CREATE_NO_WINDOW;
	});
#endif // Q_OS_WIN
	QObject::connect(process, &QProcess::started, [=] {
#ifdef Q_OS_WIN
		AddToKillOnCloseJob(process->processId());
#endif // Q_OS_WIN
		process->write(config);
		process->closeWriteChannel();
		Data().startedTimer.callOnce(kStartedAfter);
	});
	QObject::connect(process, &QProcess::errorOccurred, [=](
			QProcess::ProcessError error) {
		if (Data().process != process
			|| error != QProcess::FailedToStart) {
			return;
		}
		LOG(("Luckgram Xray: the core could not start."));
		SetState(false, Tr("无法启动", "could not start"));
	});
	QObject::connect(process, &QProcess::finished, [=](
			int exitCode,
			QProcess::ExitStatus) {
		if (Data().process != process) {
			return;
		}
		LOG(("Luckgram Xray: the core exited, code %1.").arg(exitCode));
		SetState(false, Luckgram::IsChinese()
			? (u"已退出（代码 "_q + QString::number(exitCode) + u"）"_q)
			: (u"exited (code "_q + QString::number(exitCode) + u")"_q));
	});
	SetState(false, QString()); // "Starting core…"
	process->start();
}

QString CopyLinkText() {
	return Tr("复制分享链接", "Copy Share Link");
}

QString LinkCopiedText() {
	return Tr("链接已复制", "Link copied");
}

QString DeleteText() {
	return Tr("删除代理", "Delete Proxy");
}

QString UnavailableText() {
	return Tr("这个节点现在用不了", "This node can't be used now");
}

} // namespace Luckgram::Xray
