/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <optional>

// D4 / M2 (mobile S5 UpdateLogic + specs#124 UpdateDownloadLogic): pure
// rules of the in-app update, QtCore only (luckgram_tests), same cases as
// the Android unit tests. GET /v1/version?platform=windows|macos.
namespace Luckgram::Update {

// At start and every 30 minutes (was 6 hours: a merged fix should reach the
// computers within the hour; the request is one small GET).
inline constexpr auto kCheckIntervalMs = qint64(30) * 60 * 1000;
inline constexpr auto kRetryAfterFailureMs = qint64(10) * 60 * 1000;
inline constexpr auto kAutoRetryAfterMs = qint64(6) * 60 * 60 * 1000;
inline constexpr auto kSpaceMargin = qint64(20) * 1024 * 1024;
inline constexpr auto kMaxSize = qint64(500) * 1024 * 1024;
inline constexpr auto kFilePrefix = "luckgram-update-";
inline constexpr auto kPartSuffix = ".part";
inline constexpr auto kAutoDownloadKey = "update_auto_download";
// "空闲时自动重启并更新", off by default: unattended computers update with
// nobody clicking.
inline constexpr auto kAutoInstallKey = "update_auto_install";
// Nobody touched the computer for this long: restarting disturbs no one.
inline constexpr auto kAutoInstallIdleMs = qint64(5) * 60 * 1000;
inline constexpr auto kDownloadPage = "https://luckgram.cc/download/";
// The only host a package is downloaded from for installing (review P1).
inline constexpr auto kTrustedPrefix = "https://dl.luckgram.cc/";

// What GET /v1/version said (persisted, the hint survives restarts).
struct Info {
	QString latest;
	qint64 build = 0;
	QString min;
	QString url; // Windows: the installer; macOS: the .dmg.
	QString notesZh;
	QString notesEn;
	QString sha256; // Lowercase hex or empty.
	qint64 size = 0;
	// macOS (S16): the zip with Luckgram.app for the in-app update.
	QString updateUrl;
	QString updateSha256;
	qint64 updateSize = 0;
};

// What the app shows.
struct State {
	bool updateAvailable = false;
	bool forceUpdate = false;
	QString latest;
	QString url;
	QString notes;
	// Of `latest` (0 / empty when only `min` is the target).
	qint64 build = 0;
	QString sha256;
	qint64 size = 0;
	QString updateUrl;
	QString updateSha256;
	qint64 updateSize = 0;
};

[[nodiscard]] bool IsVersion(const QString &value);
// Segment by segment, missing = 0, not a version = 0.0.0.
[[nodiscard]] int CompareVersions(const QString &a, const QString &b);
// Equal versions with both builds known compare by build.
[[nodiscard]] bool IsNewer(
	const QString &latest,
	qint64 latestBuild,
	const QString &current,
	qint64 currentBuild);

// "7.4.2+23": the installed package, a different one checks at once.
[[nodiscard]] QString AppId(const QString &version, qint64 build);
[[nodiscard]] bool ShouldCheck(
	qint64 nowMs,
	qint64 lastSuccessMs,
	qint64 lastFailureMs,
	const QString &checkedBy = QString(),
	const QString &running = QString());
[[nodiscard]] QString Path(
	const QString &platform,
	const QString &appVersion,
	qint64 build);

[[nodiscard]] std::optional<Info> Parse(const QByteArray &json);
[[nodiscard]] QByteArray Serialize(const Info &info);

// Against the running version every time; nothing without https.
[[nodiscard]] State Evaluate(
	const std::optional<Info> &info,
	const QString &current,
	qint64 currentBuild,
	bool chinese);

[[nodiscard]] bool IsSha256(const QString &value);
[[nodiscard]] bool IsHttps(const QString &url);
// https://dl.luckgram.cc/... (host compared without case).
[[nodiscard]] bool IsTrustedUrl(const QString &url);
// Installed by the app only with a dl.luckgram.cc address, a sha256 and a
// size to check it against; anything else goes to the download page.
[[nodiscard]] bool CanInstallFrom(
	const QString &url,
	const QString &sha256,
	qint64 size);

// luckgram-update-7.4.2-23.exe (only digits, dots and letters).
[[nodiscard]] QString FileName(
	const QString &version,
	qint64 build,
	const QString &suffix);

enum class Verdict {
	Ok,
	NotChecked, // No sha256 from the server: accepted, logged.
	SizeMismatch,
	ShaMismatch,
};
[[nodiscard]] Verdict Verify(
	const QString &expectedSha,
	qint64 expectedSize,
	const QString &actualSha,
	qint64 actualSize);
[[nodiscard]] bool Accepted(Verdict verdict);
// The downloaded package may be run: sha256 and size given and both
// matched (NotChecked is never enough to install, review P1).
[[nodiscard]] bool MayInstall(Verdict verdict, qint64 expectedSize);
[[nodiscard]] bool EnoughSpace(qint64 available, qint64 size);
[[nodiscard]] bool ShouldAutoDownload(
	bool updateAvailable,
	const QString &url,
	bool switchOn,
	bool unmetered,
	bool alreadyReady,
	bool downloading,
	qint64 nowMs,
	qint64 lastAutoFailureMs);
// The verified package is installed without asking: the switch is on, no
// call is running, and the user is away - the app is not the one in front,
// or the computer was not touched for kAutoInstallIdleMs.
[[nodiscard]] bool ShouldAutoInstall(
	bool ready,
	bool switchOn,
	bool inCall,
	bool appInFront,
	qint64 idleMs);
// Packages and partial downloads to delete, except `keep` (and its .part).
[[nodiscard]] QStringList ToDelete(
	const QStringList &names,
	const QString &keep);

// Windows: the per-user installer run by the update, no wizard, no
// reboot; /luckgramupdate=1 makes it start Luckgram again (luckgram.iss).
[[nodiscard]] QStringList InstallerArguments();

enum class Failure {
	Network,
	Verify,
	Space,
	NotHttps,
};

class Texts final {
public:
	explicit Texts(bool zh) : _zh(zh) {
	}

	// Mobile UpdateDownloadLogic.Texts, verbatim.
	[[nodiscard]] QString failure(Failure failure, const QString &detail) const;
	[[nodiscard]] QString failedTitle() const;
	[[nodiscard]] QString verifyFailedTitle() const;
	[[nodiscard]] QString label(const QString &version, qint64 build) const;
	[[nodiscard]] QString availableRow(const QString &version, qint64 build) const;
	[[nodiscard]] QString downloadingRow(
		const QString &version,
		qint64 build,
		int percent) const;
	[[nodiscard]] QString downloadingTitle(const QString &version, qint64 build) const;
	[[nodiscard]] QString notesTitle(const QString &version, qint64 build) const;
	[[nodiscard]] QString forceText(
		const QString &current,
		qint64 currentBuild,
		const QString &target,
		qint64 targetBuild) const;
	[[nodiscard]] QString forceTitle() const;
	[[nodiscard]] QString updateNow() const;
	[[nodiscard]] QString later() const;
	[[nodiscard]] QString cancel() const;
	[[nodiscard]] QString retry() const;
	[[nodiscard]] QString ok() const;
	[[nodiscard]] QString autoSwitch() const;
	[[nodiscard]] QString autoSwitchInfo() const;
	[[nodiscard]] QString autoInstallSwitch() const;

	// Desktop (spec D4 §6-8, M2 §6), not on mobile.
	[[nodiscard]] QString readyRow(const QString &version, qint64 build) const;
	[[nodiscard]] QString restartAndUpdate() const;
	[[nodiscard]] QString quit() const;
	[[nodiscard]] QString portableText(const QString &version, qint64 build) const;
	[[nodiscard]] QString manualText(const QString &version, qint64 build) const;
	[[nodiscard]] QString unverifiedText(const QString &version, qint64 build) const;
	[[nodiscard]] QString openDownloadPage() const;

private:
	[[nodiscard]] QString t(const char *zh, const char *en) const;

	bool _zh = false;

};

} // namespace Luckgram::Update
