/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/update/luckgram_update.h"

#include "base/call_delayed.h"
#include "base/timer.h"
#include "base/weak_qptr.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "core/version.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_desktop.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/platform/luckgram_update_platform.h"
#include "luckgram/update/luckgram_update_logic.h"
#include "settings.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>

#include <algorithm>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QStorageInfo>
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
#include <QtNetwork/QNetworkInformation>
#endif // Qt >= 6.3

namespace Luckgram::Update {

void InstallNow();

namespace {

constexpr auto kInfoKey = "update_info";
constexpr auto kLastCheckKey = "update_last_check";
constexpr auto kLastFailureKey = "update_last_failure";
constexpr auto kLastCheckAppKey = "update_last_check_app";
constexpr auto kAutoFailureKey = "update_auto_failure";
// How often "is a check due" is asked; the check itself is every 6 hours.
constexpr auto kTimerInterval = crl::time(30 * 60 * 1000);

enum class Phase {
	None,
	Available,
	Downloading,
	Ready,
};

struct Data {
	base::Timer timer;
	Api::RequestPtr check;
	Api::RequestPtr download;
	bool downloadManual = false;
	int percent = -1;
	bool ready = false; // The package for the current state is verified.
	rpl::event_stream<> changes;
	base::weak_qptr<Ui::BoxContent> forceBox;
	base::weak_qptr<Ui::BoxContent> progressBox;
	bool started = false;
};

[[nodiscard]] Data &Shared() {
	static auto result = Data();
	return result;
}

[[nodiscard]] QString K(const char *key) {
	return QString::fromLatin1(key);
}

[[nodiscard]] qint64 NowMs() {
	return QDateTime::currentMSecsSinceEpoch();
}

[[nodiscard]] Texts Tx() {
	return Texts(IsChinese());
}

[[nodiscard]] QString CurrentVersion() {
	return QString::fromLatin1(AppVersionStr);
}

[[nodiscard]] qint64 CurrentBuild() {
	return BuildNumber();
}

[[nodiscard]] State Current() {
	return Evaluate(
		Parse(Settings::GetString(K(kInfoKey)).toUtf8()),
		CurrentVersion(),
		CurrentBuild(),
		IsChinese());
}

// Windows: the installer (url); macOS: the zip (update_url).
[[nodiscard]] QString PackageUrl(const State &state) {
	return (Platform::PackageSuffix() == u".zip"_q)
		? state.updateUrl
		: state.url;
}

[[nodiscard]] QString PackageSha(const State &state) {
	return (Platform::PackageSuffix() == u".zip"_q)
		? state.updateSha256
		: state.sha256;
}

[[nodiscard]] qint64 PackageSize(const State &state) {
	return (Platform::PackageSuffix() == u".zip"_q)
		? state.updateSize
		: state.size;
}

[[nodiscard]] QString Folder() {
	return cWorkingDir() + u"luckgram_update/"_q;
}

[[nodiscard]] QString PackageName(const State &state) {
	return FileName(state.latest, state.build, Platform::PackageSuffix());
}

[[nodiscard]] QString PackagePath(const State &state) {
	return Folder() + PackageName(state);
}

// Review P1: a dl.luckgram.cc address with sha256 and size, else only the
// download page is offered.
[[nodiscard]] bool Installable(const State &state) {
	return Platform::CanInstall()
		&& CanInstallFrom(
			PackageUrl(state),
			PackageSha(state),
			PackageSize(state));
}

[[nodiscard]] Phase CurrentPhase() {
	const auto state = Current();
	if (!state.updateAvailable) {
		return Phase::None;
	} else if (Shared().download) {
		return Phase::Downloading;
	} else if (Shared().ready && QFile::exists(PackagePath(state))) {
		return Phase::Ready;
	}
	return Phase::Available;
}

[[nodiscard]] QString RowText() {
	const auto state = Current();
	switch (CurrentPhase()) {
	case Phase::None: return QString();
	case Phase::Downloading:
		return Tx().downloadingRow(state.latest, state.build, Shared().percent);
	case Phase::Ready: return Tx().readyRow(state.latest, state.build);
	case Phase::Available: break;
	}
	return Tx().availableRow(state.latest, state.build);
}

[[nodiscard]] rpl::producer<> ChangesValue() {
	return rpl::single(rpl::empty) | rpl::then(rpl::merge(
		Shared().changes.events(),
		Settings::Changes() | rpl::to_empty));
}

[[nodiscard]] rpl::producer<QString> RowTextValue() {
	return ChangesValue() | rpl::map([] { return RowText(); });
}

[[nodiscard]] rpl::producer<bool> ShownValue() {
	return ChangesValue() | rpl::map([] {
		return CurrentPhase() != Phase::None;
	}) | rpl::distinct_until_changed();
}

void Changed() {
	Shared().changes.fire({});
}

[[nodiscard]] Window::SessionController *ActiveController() {
	const auto window = Core::App().activePrimaryWindow();
	return window ? window->sessionController() : nullptr;
}

void ShowInfo(const QString &title, const QString &text) {
	if (const auto controller = ActiveController()) {
		controller->show(Ui::MakeInformBox({
			.text = text,
			.title = title,
		}));
	}
}

[[nodiscard]] bool Unmetered() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
	static const auto loaded = QNetworkInformation::loadBackendByFeatures(
		QNetworkInformation::Feature::Metered);
	if (loaded) {
		if (const auto instance = QNetworkInformation::instance()) {
			return !instance->isMetered();
		}
	}
#endif // Qt >= 6.3
	return true; // No way to tell: desktops are rarely metered.
}

void Cleanup() {
	const auto state = Current();
	const auto keep = state.updateAvailable ? PackageName(state) : QString();
	const auto dir = QDir(Folder());
	for (const auto &name : ToDelete(dir.entryList(QDir::Files), keep)) {
		QFile::remove(dir.filePath(name));
	}
}

void MaybeShowForce();
void StartDownload(bool manual);
void ShowProgressBox();

void DownloadFailed(Failure failure, const QString &detail) {
	auto &data = Shared();
	data.download = nullptr;
	data.percent = -1;
	const auto manual = base::take(data.downloadManual);
	if (const auto box = data.progressBox.get()) {
		box->closeBox();
	}
	Changed();
	if (!manual && !Current().forceUpdate) {
		Settings::SetInt(K(kAutoFailureKey), NowMs());
		return;
	}
	ShowInfo(
		(failure == Failure::Verify
			? Tx().verifyFailedTitle()
			: Tx().failedTitle()),
		Tx().failure(failure, detail));
}

void Verified(const State &state, bool manual) {
	auto &data = Shared();
	data.download = nullptr;
	data.ready = true;
	data.percent = -1;
	Settings::Remove(K(kAutoFailureKey));
	if (const auto box = data.progressBox.get()) {
		box->closeBox();
	}
	Changed();
	if (manual || state.forceUpdate) {
		// Asked for it: "重启并更新" right away.
		if (const auto controller = ActiveController()) {
			controller->show(Ui::MakeConfirmBox({
				.text = Tx().readyRow(state.latest, state.build),
				.confirmed = [=](Fn<void()> close) {
					close();
					InstallNow();
				},
				.confirmText = Tx().restartAndUpdate(),
				.cancelText = Tx().later(),
				.title = Tx().notesTitle(state.latest, state.build),
			}));
		}
	}
}

void StartDownload(bool manual) {
	auto &data = Shared();
	const auto state = Current();
	if (!state.updateAvailable || data.download) {
		data.downloadManual = data.downloadManual || manual;
		return;
	}
	const auto url = PackageUrl(state);
	if (!Installable(state)) {
		return; // OnClicked shows the download page instead.
	}
	const auto size = PackageSize(state);
	if (size > kMaxSize) {
		data.downloadManual = manual;
		DownloadFailed(Failure::Verify, QString());
		return;
	}
	QDir().mkpath(Folder());
	if (!EnoughSpace(QStorageInfo(Folder()).bytesAvailable(), size)) {
		data.downloadManual = manual;
		DownloadFailed(Failure::Space, QString());
		return;
	}
	const auto path = PackagePath(state);
	data.ready = false;
	data.downloadManual = manual;
	data.percent = 0;
	data.download = Api::DownloadToFile(url, path, [=](
			qint64 received,
			qint64 total) {
		const auto all = (total > 0) ? total : size;
		const auto percent = (all > 0)
			? int(std::clamp(received * 100 / all, qint64(0), qint64(100)))
			: -1;
		if (Shared().percent != percent) {
			Shared().percent = percent;
			Changed();
		}
	}, [=](QString sha, qint64 got) {
		const auto verdict = Verify(PackageSha(state), size, sha, got);
		if (!MayInstall(verdict, size)) {
			LOG(("Luckgram Update Error: %1 failed verification (%2)."
				).arg(PackageName(state)
				).arg(int(verdict)));
			QFile::remove(path);
			DownloadFailed(Failure::Verify, QString());
			return;
		}
		// macOS: unpack and check the bundle; off the main thread.
		crl::async([=] {
			const auto ok = Platform::Prepare(path);
			crl::on_main([=] {
				if (!ok) {
					QFile::remove(path);
					DownloadFailed(Failure::Verify, QString());
				} else {
					Verified(state, base::take(Shared().downloadManual));
				}
			});
		});
	}, [=](Api::Error error) {
		if (error.code == QString::fromLatin1(Api::kWriteFailed)) {
			DownloadFailed(Failure::Space, QString());
			return;
		}
		DownloadFailed(
			Failure::Network,
			error.status ? QString::number(error.status) : QString());
	});
	Changed();
}

void CancelDownload() {
	auto &data = Shared();
	if (const auto download = base::take(data.download)) {
		download->cancel();
	}
	data.percent = -1;
	data.downloadManual = false;
	Changed();
}

void MaybeAutoDownload() {
	const auto state = Current();
	if (!Installable(state)) {
		return;
	}
	const auto ready = (CurrentPhase() == Phase::Ready);
	if (ShouldAutoDownload(
			state.updateAvailable,
			PackageUrl(state),
			AutoDownload(),
			Unmetered(),
			ready,
			Shared().download != nullptr,
			NowMs(),
			Settings::GetInt(K(kAutoFailureKey), 0))) {
		StartDownload(false);
	}
}

void Check() {
	auto &data = Shared();
	if (data.check) {
		return;
	}
	data.check = Api::Send({
		.path = Path(Platform::Name(), CurrentVersion(), CurrentBuild()),
	}, [=](Api::Response response) {
		Shared().check = nullptr;
		const auto info = Parse(response.body);
		if (!info) {
			Settings::SetInt(K(kLastFailureKey), NowMs());
			return;
		}
		Settings::SetString(K(kInfoKey), QString::fromUtf8(Serialize(*info)));
		Settings::SetInt(K(kLastCheckKey), NowMs());
		Settings::Remove(K(kLastFailureKey));
		Settings::SetString(
			K(kLastCheckAppKey),
			AppId(CurrentVersion(), CurrentBuild()));
		Cleanup();
		Changed();
		MaybeShowForce();
		MaybeAutoDownload();
	}, [=](Api::Error) {
		Shared().check = nullptr;
		Settings::SetInt(K(kLastFailureKey), NowMs());
	});
}

void CheckIfDue() {
	if (ShouldCheck(
			NowMs(),
			Settings::GetInt(K(kLastCheckKey), 0),
			Settings::GetInt(K(kLastFailureKey), 0),
			Settings::GetString(K(kLastCheckAppKey)),
			AppId(CurrentVersion(), CurrentBuild()))) {
		Check();
	}
}

void OpenDownloadPage() {
	File::OpenUrl(QString::fromLatin1(kDownloadPage));
}

enum class Manual {
	Portable,
	NoAccess,
	Unverified,
};

void ShowManual(Manual reason) {
	const auto state = Current();
	const auto controller = ActiveController();
	if (!controller) {
		return;
	}
	controller->show(Ui::MakeConfirmBox({
		.text = [&] {
			switch (reason) {
			case Manual::Portable:
				return Tx().portableText(state.latest, state.build);
			case Manual::NoAccess:
				return Tx().manualText(state.latest, state.build);
			case Manual::Unverified: break;
			}
			return Tx().unverifiedText(state.latest, state.build);
		}(),
		.confirmed = [=](Fn<void()> close) {
			close();
			OpenDownloadPage();
		},
		.confirmText = Tx().openDownloadPage(),
		.cancelText = Tx().later(),
		.title = Tx().notesTitle(state.latest, state.build),
	}));
}

void ShowProgressBox() {
	const auto controller = ActiveController();
	if (!controller || Shared().progressBox) {
		return;
	}
	const auto state = Current();
	auto box = Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(
			Tx().downloadingTitle(state.latest, state.build)));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			RowTextValue(),
			st::boxLabel));
		box->addButton(rpl::single(Tx().cancel()), [=] {
			CancelDownload();
			box->closeBox();
		});
		// Escape / outside: hidden, the download goes on.
	});
	Shared().progressBox = base::weak_qptr<Ui::BoxContent>(box.data());
	controller->show(std::move(box));
}

void OnClicked() {
	const auto state = Current();
	if (!state.updateAvailable) {
		return;
	} else if (!Platform::CanInstall()) {
		ShowManual(IsPortable() ? Manual::Portable : Manual::NoAccess);
		return;
	} else if (!Installable(state)) {
		ShowManual(Manual::Unverified);
		return;
	}
	switch (CurrentPhase()) {
	case Phase::Ready: InstallNow(); return;
	case Phase::Downloading:
		Shared().downloadManual = true;
		ShowProgressBox();
		return;
	case Phase::None: return;
	case Phase::Available: break;
	}
	const auto start = [] {
		StartDownload(true);
		ShowProgressBox();
	};
	const auto controller = ActiveController();
	if (!controller || state.notes.isEmpty() || state.forceUpdate) {
		start();
		return;
	}
	// Notes first (mobile onUpdateTapped).
	controller->show(Ui::MakeConfirmBox({
		.text = state.notes,
		.confirmed = [=](Fn<void()> close) {
			close();
			start();
		},
		.confirmText = Tx().updateNow(),
		.cancelText = Tx().later(),
		.title = Tx().notesTitle(state.latest, state.build),
	}));
}

void MaybeShowForce() {
	auto &data = Shared();
	const auto state = Current();
	if (!state.forceUpdate) {
		if (const auto box = data.forceBox.get()) {
			box->closeBox();
		}
		return;
	} else if (data.forceBox) {
		return;
	}
	const auto controller = ActiveController();
	if (!controller) {
		return;
	}
	// D4 §8: only update or quit.
	auto box = Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Tx().forceTitle()));
		const auto text = Tx().forceText(
			CurrentVersion(),
			CurrentBuild(),
			state.latest,
			state.build) + (state.notes.isEmpty()
				? QString()
				: (u"\n\n"_q + state.notes));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(text),
			st::boxLabel));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			ChangesValue() | rpl::map([] {
				return (CurrentPhase() == Phase::Downloading)
					? RowText()
					: QString();
			}),
			st::boxLabel));
		box->setCloseByEscape(false);
		box->setCloseByOutsideClick(false);
		box->addButton(rpl::single(Tx().updateNow()), [] {
			OnClicked();
		});
		box->addButton(rpl::single(Tx().quit()), [] {
			Core::Quit();
		});
		box->boxClosing() | rpl::on_next([] {
			// Shown again while still below min (the box is gone by then).
			base::call_delayed(crl::time(1000), [] { MaybeShowForce(); });
		}, box->lifetime());
	});
	data.forceBox = base::weak_qptr<Ui::BoxContent>(box.data());
	controller->show(std::move(box));
}

} // namespace

void InstallNow() {
	const auto state = Current();
	const auto path = PackagePath(state);
	if (!state.updateAvailable || !QFile::exists(path)) {
		Shared().ready = false;
		Changed();
		return;
	} else if (!Platform::CanInstall()) {
		ShowManual(IsPortable() ? Manual::Portable : Manual::NoAccess);
		return;
	} else if (!Installable(state)) {
		ShowManual(Manual::Unverified);
		return;
	}
	// Checked again right before it runs: the file may be from an earlier
	// start, or changed on disk since (review P1).
	const auto sha = PackageSha(state);
	const auto size = PackageSize(state);
	crl::async([=] {
		auto file = QFile(path);
		auto hash = QCryptographicHash(QCryptographicHash::Sha256);
		const auto read = file.open(QIODevice::ReadOnly) && hash.addData(&file);
		const auto actual = read
			? QString::fromLatin1(hash.result().toHex())
			: QString();
		const auto actualSize = read ? file.size() : qint64(-1);
		crl::on_main([=] {
			if (!MayInstall(Verify(sha, size, actual, actualSize), size)) {
				LOG(("Luckgram Update Error: %1 failed verification before "
					"installing.").arg(path));
				QFile::remove(path);
				Shared().ready = false;
				Changed();
				ShowInfo(
					Tx().verifyFailedTitle(),
					Tx().failure(Failure::Verify, QString()));
				return;
			}
			if (!Platform::Install(path)) {
				LOG(("Luckgram Update Error: could not start %1.").arg(path));
				ShowManual(Manual::NoAccess);
				return;
			}
			Core::Quit();
		});
	});
}

void Start(not_null<Window::SessionController*>) {
	auto &data = Shared();
	if (data.started) {
		return;
	}
	data.started = true;
	data.ready = QFile::exists(PackagePath(Current()))
		&& Current().updateAvailable;
	data.timer.setCallback([] {
		MaybeShowForce();
		CheckIfDue();
	});
	data.timer.callEach(kTimerInterval);
	Cleanup();
	MaybeShowForce();
	CheckIfDue();
	MaybeAutoDownload();
}

void AddMainMenuEntry(
		not_null<Ui::VerticalLayout*> menu,
		not_null<Window::SessionController*> controller) {
	const auto wrap = menu->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			menu,
			::Settings::CreateButtonWithIcon(
				menu,
				RowTextValue(),
				st::mainMenuButton,
				{ &st::menuIconDownload })));
	wrap->toggleOn(ShownValue());
	wrap->finishAnimating();
	wrap->entity()->setClickedCallback(OnClicked);
}

void AddSettingsButton(::Settings::Builder::SectionBuilder &builder) {
	builder.addButton({
		.id = u"main/luckgram_update"_q,
		.title = RowTextValue(),
		.icon = { &st::menuIconDownload },
		.onClick = OnClicked,
		.keywords = { u"update"_q, u"更新"_q, u"version"_q },
		.shown = ShownValue(),
	});
}

rpl::producer<QString> AutoDownloadTitle() {
	return rpl::single(Tx().autoSwitch());
}

bool AutoDownload() {
	return Settings::GetBool(K(kAutoDownloadKey), true);
}

void BindAutoDownloadCheckbox(not_null<Ui::Checkbox*> checkbox) {
	checkbox->checkedChanges(
	) | rpl::filter([](bool checked) {
		return (checked != AutoDownload());
	}) | rpl::on_next([](bool checked) {
		Settings::SetBool(K(kAutoDownloadKey), checked);
		if (checked) {
			MaybeAutoDownload();
		} else if (!Shared().downloadManual) {
			CancelDownload();
		}
	}, checkbox->lifetime());
}

} // namespace Luckgram::Update
