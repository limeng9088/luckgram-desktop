/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_screenshots.h"

#include "boxes/about_box.h"
#include "core/application.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_keys.h"
#include "logs.h"
#include "luckgram/luckgram_desktop.h"
#include "mainwindow.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"

#include <QtCore/QDir>
#include <QtCore/QTimer>
#include <QtWidgets/QApplication>

namespace Luckgram {
namespace {

constexpr auto kStartDelay = 8000;
constexpr auto kStepDelay = 1500;
constexpr auto kLangWaitStep = 1000;
constexpr auto kLangWaitSteps = 45;

struct Step {
	int delay = 0;
	Fn<void()> action;
};

void Log(const QString &text) {
	LOG(("Luckgram Screenshots: %1").arg(text));
}

[[nodiscard]] Window::Controller *PrimaryWindow() {
	return Core::App().activePrimaryWindow();
}

void Save(const QString &path, const QPixmap &pixmap) {
	const auto ok = !pixmap.isNull() && pixmap.save(path, "PNG");
	Log(u"%1 %2 (%3x%4)"_q
		.arg(ok ? u"saved"_q : u"FAILED"_q)
		.arg(path)
		.arg(pixmap.width())
		.arg(pixmap.height()));
}

void GrabWindow(const QString &path) {
	if (const auto window = PrimaryWindow()) {
		Save(path, window->widget()->grab());
	} else {
		Log(u"no window for "_q + path);
	}
}

[[nodiscard]] Ui::PopupMenu *TrayMenu() {
	for (const auto widget : QApplication::allWidgets()) {
		if (const auto menu = dynamic_cast<Ui::PopupMenu*>(widget)) {
			return menu;
		}
	}
	return nullptr;
}

void RunSteps(std::shared_ptr<std::vector<Step>> steps, int index) {
	if (index >= int(steps->size())) {
		return;
	}
	QTimer::singleShot((*steps)[index].delay, qApp, [=] {
		(*steps)[index].action();
		RunSteps(steps, index + 1);
	});
}

[[nodiscard]] bool ChineseReady() {
	const auto settings = tr::lng_menu_settings(tr::now);
	return Lang::Id().startsWith(u"zh"_q, Qt::CaseInsensitive)
		&& std::any_of(settings.begin(), settings.end(), [](QChar ch) {
			return ch.unicode() >= 0x4E00 && ch.unicode() <= 0x9FFF;
		});
}

void CaptureSet(const QString &dir, const QString &prefix, Fn<void()> done) {
	const auto base = dir + '/' + prefix + '-';
	auto steps = std::make_shared<std::vector<Step>>();
	steps->push_back({ 0, [] {
		if (const auto window = PrimaryWindow()) {
			window->widget()->showNormal();
			window->widget()->resize(1000, 700);
		}
	} });
	steps->push_back({ kStepDelay, [=] {
		GrabWindow(base + u"1-welcome.png"_q);
	} });
	steps->push_back({ 0, [] {
		if (const auto menu = TrayMenu()) {
			menu->popup(QPoint(200, 200));
		} else {
			Log(u"no tray menu"_q);
		}
	} });
	steps->push_back({ kStepDelay, [=] {
		if (const auto menu = TrayMenu()) {
			Save(base + u"2-tray-menu.png"_q, menu->grab());
			menu->hide();
		}
	} });
	steps->push_back({ 0, [] {
		if (const auto window = PrimaryWindow()) {
			window->show(Box(AboutBox));
		}
	} });
	steps->push_back({ kStepDelay, [=] {
		GrabWindow(base + u"3-about.png"_q);
		if (const auto window = PrimaryWindow()) {
			window->hideLayer(anim::type::instant);
		}
	} });
	steps->push_back({ 0, [] {
		if (const auto window = PrimaryWindow()) {
			window->showSettings();
		}
	} });
	steps->push_back({ kStepDelay, [=] {
		GrabWindow(base + u"4-settings.png"_q);
		if (const auto window = PrimaryWindow()) {
			window->hideSettingsAndLayer(anim::type::instant);
		}
	} });
	steps->push_back({ kStepDelay, done });
	RunSteps(steps, 0);
}

void WaitForChinese(int left, Fn<void()> done) {
	if (ChineseReady() || left <= 0) {
		Log(u"language %1, ready: %2"_q
			.arg(Lang::Id())
			.arg(ChineseReady() ? u"yes"_q : u"no"_q));
		done();
		return;
	}
	QTimer::singleShot(kLangWaitStep, qApp, [=] {
		WaitForChinese(left - 1, done);
	});
}

} // namespace

void RunScreenshotsIfRequested() {
	const auto dir = qEnvironmentVariable("LUCKGRAM_SCREENSHOTS");
	if (dir.isEmpty()) {
		return;
	}
	QDir().mkpath(dir);
	Log(u"into "_q + dir);
	QTimer::singleShot(kStartDelay, qApp, [=] {
		CaptureSet(dir, u"en"_q, [=] {
			Lang::CurrentCloudManager().switchToLanguage(
				QString::fromLatin1(kChineseLangPackId));
			WaitForChinese(kLangWaitSteps, [=] {
				CaptureSet(dir, u"zh"_q, [=] {
					Log(u"done"_q);
					Core::Quit();
				});
			});
		});
	});
}

} // namespace Luckgram
