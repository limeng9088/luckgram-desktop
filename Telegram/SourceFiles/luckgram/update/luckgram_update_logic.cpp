/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/update/luckgram_update_logic.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

#include <algorithm>

namespace Luckgram::Update {
namespace {

[[nodiscard]] QString Lower(const QString &value) {
	return value.toLower();
}

[[nodiscard]] QString CleanSha(const QString &value) {
	return IsSha256(value) ? Lower(value) : QString();
}

[[nodiscard]] QString String(const QJsonObject &object, const char *key) {
	const auto value = object.value(QString::fromLatin1(key));
	return value.isString() ? value.toString() : QString();
}

[[nodiscard]] qint64 Number(const QJsonObject &object, const char *key) {
	const auto value = object.value(QString::fromLatin1(key));
	return value.isDouble() ? std::max(qint64(0), qint64(value.toDouble())) : 0;
}

} // namespace

bool IsVersion(const QString &value) {
	static const auto re = QRegularExpression(
		QStringLiteral("^\\d{1,9}(\\.\\d{1,9}){0,3}$"));
	return re.match(value).hasMatch();
}

int CompareVersions(const QString &a, const QString &b) {
	const auto x = IsVersion(a) ? a.split(QChar('.')) : QStringList();
	const auto y = IsVersion(b) ? b.split(QChar('.')) : QStringList();
	for (auto i = 0; i < std::max(x.size(), y.size()); ++i) {
		const auto p = (i < x.size()) ? x[i].toLongLong() : 0LL;
		const auto q = (i < y.size()) ? y[i].toLongLong() : 0LL;
		if (p != q) {
			return (p < q) ? -1 : 1;
		}
	}
	return 0;
}

bool IsNewer(
		const QString &latest,
		qint64 latestBuild,
		const QString &current,
		qint64 currentBuild) {
	const auto c = CompareVersions(latest, current);
	if (c != 0) {
		return c > 0;
	}
	return latestBuild > 0 && currentBuild > 0 && latestBuild > currentBuild;
}

QString AppId(const QString &version, qint64 build) {
	return version + QChar('+') + QString::number(std::max(qint64(0), build));
}

bool ShouldCheck(
		qint64 nowMs,
		qint64 lastSuccessMs,
		qint64 lastFailureMs,
		const QString &checkedBy,
		const QString &running) {
	// A recent failure waits first, also after an update.
	if (lastFailureMs > 0
		&& nowMs >= lastFailureMs
		&& nowMs - lastFailureMs < kRetryAfterFailureMs) {
		return false;
	}
	if (lastSuccessMs > 0 && !running.isEmpty() && running != checkedBy) {
		return true;
	}
	// nowMs < lastSuccessMs: the clock went back.
	return lastSuccessMs <= 0
		|| nowMs < lastSuccessMs
		|| nowMs - lastSuccessMs >= kCheckIntervalMs;
}

QString Path(const QString &platform, const QString &appVersion, qint64 build) {
	auto result = QStringLiteral("/v1/version?platform=") + platform;
	if (IsVersion(appVersion)) {
		result += QStringLiteral("&app_version=") + appVersion;
		if (build > 0) {
			result += QStringLiteral("&build=") + QString::number(build);
		}
	}
	return result;
}

std::optional<Info> Parse(const QByteArray &json) {
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return std::nullopt;
	}
	const auto j = document.object();
	const auto notes = j.value(QStringLiteral("notes")).toObject();
	auto result = Info();
	result.latest = String(j, "latest");
	result.build = Number(j, "build");
	result.min = String(j, "min");
	result.url = String(j, "url");
	result.notesZh = String(notes, "zh");
	result.notesEn = String(notes, "en");
	result.sha256 = CleanSha(String(j, "sha256"));
	result.size = Number(j, "size");
	result.updateUrl = String(j, "update_url");
	result.updateSha256 = CleanSha(String(j, "update_sha256"));
	result.updateSize = Number(j, "update_size");
	return result;
}

QByteArray Serialize(const Info &info) {
	auto notes = QJsonObject();
	notes.insert(QStringLiteral("zh"), info.notesZh);
	notes.insert(QStringLiteral("en"), info.notesEn);
	auto j = QJsonObject();
	j.insert(QStringLiteral("latest"), info.latest);
	j.insert(QStringLiteral("build"), double(info.build));
	j.insert(QStringLiteral("min"), info.min);
	j.insert(QStringLiteral("url"), info.url);
	j.insert(QStringLiteral("notes"), notes);
	j.insert(QStringLiteral("sha256"), info.sha256);
	j.insert(QStringLiteral("size"), double(info.size));
	j.insert(QStringLiteral("update_url"), info.updateUrl);
	j.insert(QStringLiteral("update_sha256"), info.updateSha256);
	j.insert(QStringLiteral("update_size"), double(info.updateSize));
	return QJsonDocument(j).toJson(QJsonDocument::Compact);
}

State Evaluate(
		const std::optional<Info> &info,
		const QString &current,
		qint64 currentBuild,
		bool chinese) {
	// A "must update" screen without a way to update would lock the user out.
	if (!info || !info->url.startsWith(QStringLiteral("https://"))
		|| !IsVersion(current)) {
		return State();
	}
	const auto force = IsVersion(info->min)
		&& CompareVersions(current, info->min) < 0;
	const auto available = IsVersion(info->latest)
		&& IsNewer(info->latest, info->build, current, currentBuild);
	if (!force && !available) {
		return State();
	}
	const auto notes = chinese
		? (info->notesZh.isEmpty() ? info->notesEn : info->notesZh)
		: (info->notesEn.isEmpty() ? info->notesZh : info->notesEn);
	// Below min but latest not newer (misconfigured): still force.
	const auto latestIsTarget = IsVersion(info->latest)
		&& (available || CompareVersions(info->latest, info->min) >= 0);
	auto result = State();
	result.updateAvailable = true;
	result.forceUpdate = force;
	result.latest = IsVersion(info->latest) ? info->latest : info->min;
	result.url = info->url;
	result.notes = notes;
	if (latestIsTarget) {
		// They describe the "latest" package only.
		result.build = info->build;
		result.sha256 = info->sha256;
		result.size = info->size;
		result.updateUrl = info->updateUrl;
		result.updateSha256 = info->updateSha256;
		result.updateSize = info->updateSize;
	}
	return result;
}

bool IsSha256(const QString &value) {
	static const auto re = QRegularExpression(
		QStringLiteral("^[0-9a-fA-F]{64}$"));
	return re.match(value).hasMatch();
}

bool IsHttps(const QString &url) {
	return url.size() > 8
		&& url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
}

bool IsTrustedUrl(const QString &url) {
	const auto prefix = QString::fromLatin1(kTrustedPrefix);
	return url.size() > prefix.size()
		&& url.startsWith(prefix, Qt::CaseInsensitive)
		&& !url.contains(QChar('@'))
		&& !url.contains(QChar('\\'));
}

bool CanInstallFrom(
		const QString &url,
		const QString &sha256,
		qint64 size) {
	return IsTrustedUrl(url) && IsSha256(sha256) && size > 0;
}

QString FileName(const QString &version, qint64 build, const QString &suffix) {
	auto clean = QString();
	for (const auto ch : version) {
		if ((ch >= QChar('0') && ch <= QChar('9'))
			|| (ch >= QChar('a') && ch <= QChar('z'))
			|| (ch >= QChar('A') && ch <= QChar('Z'))
			|| ch == QChar('.')) {
			clean += ch;
		}
	}
	return QString::fromLatin1(kFilePrefix)
		+ clean
		+ QChar('-')
		+ QString::number(std::max(qint64(0), build))
		+ suffix;
}

Verdict Verify(
		const QString &expectedSha,
		qint64 expectedSize,
		const QString &actualSha,
		qint64 actualSize) {
	if (expectedSize > 0 && expectedSize != actualSize) {
		return Verdict::SizeMismatch;
	} else if (!IsSha256(expectedSha)) {
		return Verdict::NotChecked;
	}
	return (expectedSha.compare(actualSha, Qt::CaseInsensitive) == 0)
		? Verdict::Ok
		: Verdict::ShaMismatch;
}

bool Accepted(Verdict verdict) {
	return verdict == Verdict::Ok || verdict == Verdict::NotChecked;
}

bool MayInstall(Verdict verdict, qint64 expectedSize) {
	return (verdict == Verdict::Ok) && (expectedSize > 0);
}

bool EnoughSpace(qint64 available, qint64 size) {
	return available >= std::max(qint64(0), size) + kSpaceMargin;
}

bool ShouldAutoDownload(
		bool updateAvailable,
		const QString &url,
		bool switchOn,
		bool unmetered,
		bool alreadyReady,
		bool downloading,
		qint64 nowMs,
		qint64 lastAutoFailureMs) {
	if (!updateAvailable
		|| !IsHttps(url)
		|| !switchOn
		|| !unmetered
		|| alreadyReady
		|| downloading) {
		return false;
	}
	return lastAutoFailureMs <= 0
		|| nowMs < lastAutoFailureMs
		|| nowMs - lastAutoFailureMs >= kAutoRetryAfterMs;
}

bool ShouldAutoInstall(
		bool ready,
		bool switchOn,
		bool inCall,
		bool appInFront,
		qint64 idleMs) {
	if (!ready || !switchOn || inCall) {
		return false;
	}
	return !appInFront || (idleMs >= kAutoInstallIdleMs);
}

QStringList ToDelete(const QStringList &names, const QString &keep) {
	auto result = QStringList();
	const auto prefix = QString::fromLatin1(kFilePrefix);
	const auto part = QString::fromLatin1(kPartSuffix);
	for (const auto &name : names) {
		if (!name.startsWith(prefix)) {
			continue;
		} else if (!keep.isEmpty() && (name == keep || name == keep + part)) {
			continue;
		}
		result.push_back(name);
	}
	return result;
}

QStringList InstallerArguments() {
	return {
		QStringLiteral("/VERYSILENT"),
		QStringLiteral("/SUPPRESSMSGBOXES"),
		QStringLiteral("/NORESTART"),
		QStringLiteral("/luckgramupdate=1"),
	};
}

QString Texts::t(const char *zh, const char *en) const {
	return QString::fromUtf8(_zh ? zh : en);
}

QString Texts::failure(Failure failure, const QString &detail) const {
	switch (failure) {
	case Failure::Verify:
		return t(
			"安装包校验失败，已删除。请稍后重试。",
			"The package failed verification and was deleted. Please try again later.");
	case Failure::Space:
		return t(
			"存储空间不足，请清理后重试。",
			"Not enough storage space. Free some space and try again.");
	case Failure::NotHttps:
		return t("下载地址无效。", "The download link is not valid.");
	case Failure::Network:
		break;
	}
	return t("网络错误，下载失败", "Network error, download failed")
		+ (detail.isEmpty()
			? QString()
			: (t("（", "（") + detail + t("）", "）")));
}

QString Texts::failedTitle() const { return t("下载失败", "Download failed"); }
QString Texts::verifyFailedTitle() const { return t("安装包校验失败", "Package verification failed"); }

QString Texts::label(const QString &version, qint64 build) const {
	if (build <= 0) {
		return version;
	}
	return _zh
		? (version + QString::fromUtf8("（") + QString::number(build) + QString::fromUtf8("）"))
		: (version + QStringLiteral(" (") + QString::number(build) + QChar(')'));
}

QString Texts::availableRow(const QString &version, qint64 build) const {
	return _zh
		? (QString::fromUtf8("有新版本 ") + label(version, build) + QString::fromUtf8(" · 更新"))
		: (QStringLiteral("New version ") + label(version, build) + QStringLiteral(" · Update"));
}

QString Texts::downloadingRow(
		const QString &version,
		qint64 build,
		int percent) const {
	const auto tail = (percent >= 0)
		? (QStringLiteral(" · ") + QString::number(percent) + QChar('%'))
		: QString();
	return (_zh
		? (QString::fromUtf8("正在下载新版本 ") + label(version, build))
		: (QStringLiteral("Downloading version ") + label(version, build))) + tail;
}

QString Texts::downloadingTitle(const QString &version, qint64 build) const {
	return t("正在下载 ", "Downloading ") + label(version, build);
}

QString Texts::notesTitle(const QString &version, qint64 build) const {
	return t("新版本 ", "Version ") + label(version, build);
}

QString Texts::forceText(
		const QString &current,
		qint64 currentBuild,
		const QString &target,
		qint64 targetBuild) const {
	return _zh
		? (QString::fromUtf8("当前版本 ") + label(current, currentBuild)
			+ QString::fromUtf8(" 已停止支持，请更新到 ") + label(target, targetBuild)
			+ QString::fromUtf8(" 后继续使用。"))
		: (QStringLiteral("Version ") + label(current, currentBuild)
			+ QStringLiteral(" is no longer supported. Update to ")
			+ label(target, targetBuild) + QStringLiteral(" to continue."));
}

QString Texts::forceTitle() const { return t("需要更新", "Update required"); }
QString Texts::updateNow() const { return t("立即更新", "Update now"); }
QString Texts::later() const { return t("以后再说", "Later"); }
QString Texts::cancel() const { return t("取消", "Cancel"); }
QString Texts::retry() const { return t("重试", "Retry"); }
QString Texts::ok() const { return t("好", "OK"); }
QString Texts::autoSwitch() const { return t("Wi-Fi 下自动下载更新", "Auto-download updates on Wi-Fi"); }

QString Texts::autoInstallSwitch() const {
	return t("空闲时自动重启并更新", "Restart and update automatically when idle");
}

QString Texts::autoSwitchInfo() const {
	return t(
		"有新版本时，在 Wi-Fi 等不计流量的网络下自动下载安装包，安装仍需你确认。",
		"When a new version is out, download it automatically on Wi-Fi or other unmetered networks. Installing still needs your confirmation.");
}

QString Texts::readyRow(const QString &version, qint64 build) const {
	return _zh
		? (QString::fromUtf8("新版本 ") + label(version, build) + QString::fromUtf8(" 已下载 · 重启以完成更新"))
		: (QStringLiteral("New version ") + label(version, build) + QStringLiteral(" downloaded · Restart to update"));
}

QString Texts::restartAndUpdate() const { return t("重启并更新", "Restart and update"); }
QString Texts::quit() const { return t("退出", "Quit"); }

QString Texts::portableText(const QString &version, qint64 build) const {
	return _zh
		? (QString::fromUtf8("免安装版不能自动更新。请下载新版本 ") + label(version, build) + QString::fromUtf8("，解压后替换当前文件夹里的文件（聊天数据在 tdata 中，会保留）。"))
		: (QStringLiteral("The portable version can't update itself. Download version ") + label(version, build) + QStringLiteral(" and replace the files in this folder with it (chat data in tdata is kept)."));
}

QString Texts::manualText(const QString &version, qint64 build) const {
	return _zh
		? (QString::fromUtf8("Luckgram 没有权限替换当前位置的程序。请下载新版本 ") + label(version, build) + QString::fromUtf8(" 手动安装。"))
		: (QStringLiteral("Luckgram can't replace the app where it is installed. Download version ") + label(version, build) + QStringLiteral(" and install it yourself."));
}

QString Texts::unverifiedText(const QString &version, qint64 build) const {
	return _zh
		? (QString::fromUtf8("无法验证这个更新包，Luckgram 不会自动安装。请从官网下载新版本 ") + label(version, build) + QString::fromUtf8("。"))
		: (QStringLiteral("This update can't be verified, so Luckgram won't install it. Download version ") + label(version, build) + QStringLiteral(" from the website."));
}

QString Texts::openDownloadPage() const { return t("打开下载页面", "Open download page"); }

} // namespace Luckgram::Update
