/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/update/luckgram_update_logic.h"

// Cases ported from mobile UpdateLogicTest / UpdateDownloadLogicTest; the
// check interval is the desktop one (6 h, spec D4 §4).

namespace {

using namespace Luckgram::Update;

constexpr auto H = qint64(60) * 60 * 1000;
constexpr auto kNow = qint64(1'000'000'000'000);
const auto kSha = QStringLiteral(
	"91a90e09aabbccddeeff00112233445566778899aabbccddeeff001122334455");

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] Info MakeInfo(
		const char *latest,
		qint64 build,
		const char *min,
		const char *url) {
	auto result = Info();
	result.latest = U(latest);
	result.build = build;
	result.min = U(min);
	result.url = U(url);
	result.notesZh = U("修复问题");
	result.notesEn = U("Bug fixes");
	return result;
}

} // namespace

LUCKGRAM_TEST(UpdateCompareVersions) {
	LG_EQUAL(CompareVersions(U("13.0"), U("13.0.0")), 0);
	LG_EQUAL(CompareVersions(U("13.0.3"), U("13.0.10")), -1);
	LG_EQUAL(CompareVersions(U("13.1"), U("13.0.99")), 1);
	LG_EQUAL(CompareVersions(U("2"), U("1.9.9.9")), 1);
	LG_EQUAL(CompareVersions(U("garbage"), U("0")), 0);
	LG_EQUAL(CompareVersions(QString(), U("0.0.1")), -1);
}

LUCKGRAM_TEST(UpdateNewerByBuildOnlyWhenBothKnown) {
	LG_CHECK(IsNewer(U("7.4.2"), 36, U("7.4.2"), 6));
	LG_CHECK(!IsNewer(U("7.4.2"), 6, U("7.4.2"), 36));
	LG_CHECK(!IsNewer(U("7.4.2"), 36, U("7.4.2"), 0));
	LG_CHECK(!IsNewer(U("7.4.2"), 0, U("7.4.2"), 6));
	LG_CHECK(IsNewer(U("7.4.3"), 1, U("7.4.2"), 99));
	LG_CHECK(!IsNewer(U("7.4.1"), 99, U("7.4.2"), 1));
}

LUCKGRAM_TEST(UpdateCheckEverySixHours) {
	LG_CHECK(ShouldCheck(kNow, 0, 0));
	LG_CHECK(!ShouldCheck(kNow, kNow - 6 * H + 60'000, 0));
	LG_CHECK(ShouldCheck(kNow, kNow - 6 * H, 0));
	LG_CHECK(ShouldCheck(kNow, kNow + H, 0)); // Clock went back.
	LG_CHECK(!ShouldCheck(kNow, 0, kNow - H / 2)); // Recent failure.
	LG_CHECK(ShouldCheck(kNow, 0, kNow - H));
}

LUCKGRAM_TEST(UpdateCheckAgainAfterTheInstalledBuildChanged) {
	const auto b359 = AppId(U("7.4.2"), 359);
	LG_EQUAL(b359, U("7.4.2+359"));
	LG_CHECK(ShouldCheck(kNow, kNow - 60'000, 0, AppId(U("7.4.2"), 345), b359));
	LG_CHECK(!ShouldCheck(kNow, kNow - 60'000, 0, b359, b359));
	LG_CHECK(ShouldCheck(kNow, kNow - 60'000, 0, QString(), b359));
	LG_CHECK(ShouldCheck(kNow, 0, 0, QString(), b359));
	LG_CHECK(!ShouldCheck(kNow, 0, kNow - 60'000, QString(), b359));
	LG_CHECK(!ShouldCheck(kNow, kNow - 60'000, kNow - 60'000, AppId(U("7.4.2"), 345), b359));
	LG_CHECK(ShouldCheck(kNow, kNow - 60'000, kNow - H, AppId(U("7.4.2"), 345), b359));
}

LUCKGRAM_TEST(UpdatePath) {
	LG_EQUAL(Path(U("windows"), U("7.4.2"), 36), U("/v1/version?platform=windows&app_version=7.4.2&build=36"));
	LG_EQUAL(Path(U("macos"), U("7.4.2"), 0), U("/v1/version?platform=macos&app_version=7.4.2"));
	LG_EQUAL(Path(U("windows"), U("7.4.2-beta&x=1"), 36), U("/v1/version?platform=windows"));
}

LUCKGRAM_TEST(UpdateEvaluate) {
	const auto url = "https://dl.luckgram.cc/windows/a.exe";
	auto s = Evaluate(MakeInfo("7.4.3", 40, "7.4.0", url), U("7.4.2"), 36, true);
	LG_CHECK(s.updateAvailable);
	LG_CHECK(!s.forceUpdate);
	LG_EQUAL(s.latest, U("7.4.3"));
	LG_EQUAL(s.url, U(url));
	LG_EQUAL(s.notes, U("修复问题"));
	LG_EQUAL(Evaluate(MakeInfo("7.4.3", 40, "7.4.0", url), U("7.4.2"), 36, false).notes, U("Bug fixes"));

	LG_CHECK(!Evaluate(MakeInfo("7.4.2", 36, "7.4.0", url), U("7.4.2"), 36, true).updateAvailable);
	LG_CHECK(!Evaluate(MakeInfo("7.4.3", 40, "", url), U("7.4.3"), 40, true).updateAvailable);

	s = Evaluate(MakeInfo("7.5.0", 50, "7.5", url), U("7.4.2"), 36, true);
	LG_CHECK(s.forceUpdate);
	LG_CHECK(s.updateAvailable);
	LG_CHECK(!Evaluate(MakeInfo("7.5.0", 50, "7.4.2", url), U("7.4.2"), 36, true).forceUpdate);

	LG_CHECK(!Evaluate(MakeInfo("7.5.0", 50, "7.5", "http://dl.luckgram.cc/a.exe"), U("7.4.2"), 36, true).forceUpdate);
	LG_CHECK(!Evaluate(MakeInfo("7.5.0", 50, "7.5", ""), U("7.4.2"), 36, true).updateAvailable);
	LG_CHECK(!Evaluate(std::nullopt, U("7.4.2"), 36, true).updateAvailable);
}

LUCKGRAM_TEST(UpdateParseAndPersist) {
	const auto json = QByteArray(
		"{\"platform\":\"windows\",\"latest\":\"7.4.3\",\"build\":40,\"min\":\"7.4.0\","
		"\"url\":\"https://dl.luckgram.cc/windows/a.exe\",\"notes\":{\"zh\":\"\xe4\xb8\xad\",\"en\":\"en\"},"
		"\"update_available\":true,\"force_update\":false}");
	const auto parsed = Parse(json);
	LG_CHECK(parsed.has_value());
	const auto i = Parse(Serialize(*parsed));
	LG_CHECK(i.has_value());
	LG_EQUAL(i->latest, U("7.4.3"));
	LG_EQUAL(i->build, qint64(40));
	LG_EQUAL(i->min, U("7.4.0"));
	LG_EQUAL(i->url, U("https://dl.luckgram.cc/windows/a.exe"));
	LG_EQUAL(i->notesZh, U("中"));
	LG_EQUAL(i->notesEn, U("en"));
	LG_CHECK(!Parse(QByteArray("{not json")).has_value());
	LG_CHECK(!Parse(QByteArray()).has_value());
	const auto empty = Parse(QByteArray("{}"));
	LG_CHECK(empty.has_value());
	LG_EQUAL(empty->latest, QString());
	LG_EQUAL(empty->build, qint64(0));
}

LUCKGRAM_TEST(UpdateMacZipFields) {
	const auto json = QByteArray(
		"{\"latest\":\"7.4.3\",\"build\":40,\"url\":\"https://dl.luckgram.cc/macos/a.dmg\","
		"\"update_url\":\"https://dl.luckgram.cc/macos/a-mac.zip\",\"update_sha256\":\"")
		+ kSha.toUpper().toLatin1()
		+ QByteArray("\",\"update_size\":77}");
	const auto s = Evaluate(Parse(Serialize(*Parse(json))), U("7.4.2"), 36, false);
	LG_CHECK(s.updateAvailable);
	LG_EQUAL(s.updateUrl, U("https://dl.luckgram.cc/macos/a-mac.zip"));
	LG_EQUAL(s.updateSha256, kSha);
	LG_EQUAL(s.updateSize, qint64(77));
}

LUCKGRAM_TEST(UpdateVerify) {
	LG_CHECK(Verify(kSha, 10, kSha.toUpper(), 10) == Verdict::Ok);
	LG_CHECK(Verify(kSha, 0, kSha, 10) == Verdict::Ok);
	auto other = kSha;
	other.replace(QChar('9'), QChar('8'));
	LG_CHECK(Verify(kSha, 10, other, 10) == Verdict::ShaMismatch);
	LG_CHECK(Verify(kSha, 11, kSha, 10) == Verdict::SizeMismatch);
	LG_CHECK(Verify(QString(), 0, kSha, 10) == Verdict::NotChecked);
	LG_CHECK(Verify(U("garbage"), 0, kSha, 10) == Verdict::NotChecked);
	LG_CHECK(Verify(QString(), 5, kSha, 10) == Verdict::SizeMismatch);
	LG_CHECK(Accepted(Verdict::NotChecked));
	LG_CHECK(!Accepted(Verdict::ShaMismatch));
}

LUCKGRAM_TEST(UpdateShaAndHttps) {
	LG_CHECK(IsSha256(kSha));
	LG_CHECK(!IsSha256(kSha.mid(1)));
	LG_CHECK(!IsSha256(QString()));
	LG_CHECK(IsHttps(U("https://dl.luckgram.cc/a.exe")));
	LG_CHECK(IsHttps(U("HTTPS://dl.luckgram.cc/a.exe")));
	LG_CHECK(!IsHttps(U("http://dl.luckgram.cc/a.exe")));
	LG_CHECK(!IsHttps(U("https://")));
}

LUCKGRAM_TEST(UpdateTrustedSource) {
	LG_CHECK(IsTrustedUrl(U("https://dl.luckgram.cc/windows/windows-7.4.2-23/a.exe")));
	LG_CHECK(IsTrustedUrl(U("HTTPS://DL.LUCKGRAM.CC/windows/a.exe")));
	LG_CHECK(!IsTrustedUrl(U("https://dl.luckgram.cc/")));
	LG_CHECK(!IsTrustedUrl(U("http://dl.luckgram.cc/windows/a.exe")));
	LG_CHECK(!IsTrustedUrl(U("https://dl.luckgram.cc.evil.com/a.exe")));
	LG_CHECK(!IsTrustedUrl(U("https://evil.com/dl.luckgram.cc/a.exe")));
	LG_CHECK(!IsTrustedUrl(U("https://github.com/limeng9088/x/releases/a.exe")));
	LG_CHECK(!IsTrustedUrl(U("https://dl.luckgram.cc/@evil.com/a.exe")));
	LG_CHECK(CanInstallFrom(U("https://dl.luckgram.cc/windows/a.exe"), kSha, 10));
	LG_CHECK(!CanInstallFrom(U("https://dl.luckgram.cc/windows/a.exe"), QString(), 10));
	LG_CHECK(!CanInstallFrom(U("https://dl.luckgram.cc/windows/a.exe"), kSha, 0));
	LG_CHECK(!CanInstallFrom(U("https://evil.com/a.exe"), kSha, 10));
	LG_CHECK(MayInstall(Verdict::Ok, 10));
	LG_CHECK(!MayInstall(Verdict::Ok, 0));
	LG_CHECK(!MayInstall(Verdict::NotChecked, 10));
	LG_CHECK(!MayInstall(Verdict::ShaMismatch, 10));
	LG_CHECK(!MayInstall(Verdict::SizeMismatch, 10));
}

LUCKGRAM_TEST(UpdateAutoDownload) {
	const auto url = U("https://dl.luckgram.cc/a.exe");
	LG_CHECK(ShouldAutoDownload(true, url, true, true, false, false, kNow, 0));
	LG_CHECK(!ShouldAutoDownload(true, url, false, true, false, false, kNow, 0));
	LG_CHECK(!ShouldAutoDownload(true, url, true, false, false, false, kNow, 0));
	LG_CHECK(!ShouldAutoDownload(false, url, true, true, false, false, kNow, 0));
	LG_CHECK(!ShouldAutoDownload(true, url, true, true, true, false, kNow, 0));
	LG_CHECK(!ShouldAutoDownload(true, url, true, true, false, true, kNow, 0));
	LG_CHECK(!ShouldAutoDownload(true, U("http://x/a.exe"), true, true, false, false, kNow, 0));
	LG_CHECK(!ShouldAutoDownload(true, url, true, true, false, false, kNow, kNow - H));
	LG_CHECK(ShouldAutoDownload(true, url, true, true, false, false, kNow, kNow - 6 * H));
	LG_CHECK(ShouldAutoDownload(true, url, true, true, false, false, kNow, kNow + H));
}

LUCKGRAM_TEST(UpdateCleanup) {
	const auto keep = FileName(U("7.4.3"), 330, U(".exe"));
	LG_EQUAL(keep, U("luckgram-update-7.4.3-330.exe"));
	const auto old = U("luckgram-update-7.4.2-324.exe");
	const auto names = QStringList{ keep, keep + U(".part"), old, old + U(".part"), U("other.txt") };
	LG_EQUAL(ToDelete(names, keep), (QStringList{ old, old + U(".part") }));
	LG_EQUAL(ToDelete(names, QString()), (QStringList{ keep, keep + U(".part"), old, old + U(".part") }));
	LG_EQUAL(ToDelete(QStringList(), keep), QStringList());
	LG_CHECK(!FileName(U("7.4.3/../x"), 0, U(".exe")).contains(QChar('/')));
}

LUCKGRAM_TEST(UpdateSpace) {
	LG_CHECK(EnoughSpace(qint64(100) << 20, qint64(50) << 20));
	LG_CHECK(!EnoughSpace(qint64(60) << 20, qint64(50) << 20));
	LG_CHECK(EnoughSpace(qint64(30) << 20, 0));
}

LUCKGRAM_TEST(UpdateStateCarriesShaAndSizeOnlyForLatest) {
	const auto json = QByteArray("{\"latest\":\"7.4.3\",\"build\":330,\"min\":\"7.4.0\","
		"\"url\":\"https://dl.luckgram.cc/a.exe\",\"sha256\":\"")
		+ kSha.toUpper().toLatin1()
		+ QByteArray("\",\"size\":123}");
	const auto info = Parse(Serialize(*Parse(json)));
	LG_EQUAL(info->sha256, kSha);
	LG_EQUAL(info->size, qint64(123));
	const auto s = Evaluate(info, U("7.4.2"), 324, false);
	LG_CHECK(s.updateAvailable);
	LG_EQUAL(s.sha256, kSha);
	LG_EQUAL(s.size, qint64(123));
	LG_EQUAL(s.build, qint64(330));
	const auto old = Evaluate(
		Parse(QByteArray("{\"latest\":\"7.4.3\",\"url\":\"https://dl.luckgram.cc/a.exe\",\"sha256\":null}")),
		U("7.4.2"),
		324,
		false);
	LG_CHECK(old.updateAvailable);
	LG_EQUAL(old.sha256, QString());
	LG_EQUAL(old.size, qint64(0));
	LG_EQUAL(Parse(QByteArray("{\"sha256\":\"abc\"}"))->sha256, QString());
}

LUCKGRAM_TEST(UpdateTextsCarryBuild) {
	const auto zh = Texts(true);
	const auto en = Texts(false);
	LG_EQUAL(zh.label(U("7.4.2"), 23), U("7.4.2（23）"));
	LG_EQUAL(en.label(U("7.4.2"), 23), U("7.4.2 (23)"));
	LG_EQUAL(zh.label(U("7.4.2"), 0), U("7.4.2"));
	LG_EQUAL(zh.availableRow(U("7.4.2"), 23), U("有新版本 7.4.2（23） · 更新"));
	LG_EQUAL(en.availableRow(U("7.4.2"), 23), U("New version 7.4.2 (23) · Update"));
	LG_EQUAL(zh.readyRow(U("7.4.2"), 23), U("新版本 7.4.2（23） 已下载 · 重启以完成更新"));
	LG_EQUAL(zh.downloadingRow(U("7.4.2"), 23, 45), U("正在下载新版本 7.4.2（23） · 45%"));
	LG_EQUAL(zh.downloadingRow(U("7.4.2"), 23, -1), U("正在下载新版本 7.4.2（23）"));
	LG_EQUAL(zh.downloadingTitle(U("7.4.2"), 23), U("正在下载 7.4.2（23）"));
	LG_EQUAL(zh.notesTitle(U("7.4.2"), 23), U("新版本 7.4.2（23）"));
	LG_EQUAL(en.notesTitle(U("7.4.2"), 23), U("Version 7.4.2 (23)"));
	LG_EQUAL(zh.forceText(U("7.4.2"), 19, U("7.4.2"), 23), U("当前版本 7.4.2（19） 已停止支持，请更新到 7.4.2（23） 后继续使用。"));
	LG_EQUAL(en.forceText(U("7.4.2"), 19, U("7.5.0"), 40), U("Version 7.4.2 (19) is no longer supported. Update to 7.5.0 (40) to continue."));
	LG_EQUAL(zh.availableRow(U("7.5.0"), 0), U("有新版本 7.5.0 · 更新"));
	LG_EQUAL(zh.failure(Failure::Network, U("500")), U("网络错误，下载失败（500）"));
}

LUCKGRAM_TEST(UpdateInstallerArguments) {
	const auto args = InstallerArguments();
	LG_CHECK(args.contains(U("/VERYSILENT")));
	LG_CHECK(args.contains(U("/luckgramupdate=1")));
}
