/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/luckgram_brand.h"

#include "lang_auto.h"

#include <array>

namespace Luckgram {
namespace {

struct Replacement {
	const char16_t *from;
	const char16_t *to;
};

// Longest first, so "Telegram Desktop app" never becomes "Luckgram app app".
constexpr Replacement kReplacements[] = {
	// Built-in English sentences that call the app "official".
	{ u"Welcome to the official Telegram Desktop app.", u"Welcome to Luckgram." },
	{ u"Official free messaging app based on", u"Free messaging app based on" },
	{ u"Telegram Desktop UWP", u"Luckgram" },
	{ u"Telegram Desktop", u"Luckgram" },
	{ u"Telegram桌面版", u"Luckgram" },
	{ u"Telegram 桌面版", u"Luckgram" },
	{ u"Telegram 桌面端", u"Luckgram" },
	{ u"Telegram桌面端", u"Luckgram" },
	{ u"Telegram 电脑版", u"Luckgram" },
	{ u"Nicegram", u"Luckgram" },
};

// Keys where "Telegram" means this app (tray menu, settings, errors from the
// app), not the Telegram service, account or Premium. In these values every
// "Telegram" becomes "Luckgram", in any language pack. Keep in sync with the
// list in luckgram/BRANDING.md.
[[nodiscard]] bool IsAppNameKey(ushort key) {
	static const auto keys = std::array{
		// Tray.
		tr::lng_open_from_tray.base,
		tr::lng_quit_from_tray.base,
		tr::lng_tray_icon_text.base,
		// Settings.
		tr::lng_settings_auto_start.base,
		tr::lng_settings_add_sendto.base,
		tr::lng_settings_passcode_lock_app.base,
		tr::lng_settings_use_winhello_about.base,
		tr::lng_settings_use_touchid_about.base,
		tr::lng_settings_use_applewatch_about.base,
		tr::lng_settings_use_systempwd_about.base,
		tr::lng_sure_save_language.base,
		tr::lng_download_path_default_radio.base,
		tr::lng_local_storage_device_telegram.base,
		tr::lng_local_storage_device_usage.base,
		tr::lng_proxy_sponsor_about.base,
		tr::lng_proxy_web_fallback.base,
		tr::lng_export_progress.base,
		tr::lng_screen_reader_bar_text.base,
		tr::lng_screen_reader_confirm_text.base,
		tr::lng_wallet_protection_create_about.base,
		// Passcode lock.
		tr::lng_passcode_winhello_unlock.base,
		tr::lng_passcode_unlock_about.base,
		tr::lng_theme_editor_need_unlock.base,
		// Update prompts.
		tr::lng_update_telegram.base,
		tr::lng_message_unsupported.base,
		tr::lng_unsupported_message_text.base,
		tr::lng_unsupported_block_text.base,
		tr::lng_stories_unsupported.base,
		tr::lng_passport_app_out_of_date.base,
		// Errors.
		tr::lng_bad_photo.base,
		tr::lng_bad_video.base,
		tr::lng_open_link_unsupported.base,
		tr::lng_no_mic_permission.base,
		tr::lng_group_call_mac_input.base,
		tr::lng_group_call_mac_accessibility.base,
		tr::lng_group_call_mac_recording.base,
		tr::lng_mac_menu_show.base,
	};
	return ranges::contains(keys, key);
}

} // namespace

QString RebrandBuiltinValue(ushort key, QString value) {
	value = RebrandText(std::move(value));
	if (IsAppNameKey(key)) {
		value.replace(u"Telegram"_q, u"Luckgram"_q);
	}
	return value;
}

bool LooksChinese(const QString &value) {
	for (const auto ch : value) {
		const auto code = ch.unicode();
		if (code >= 0x4E00 && code <= 0x9FFF) {
			return true;
		}
	}
	return false;
}

QString RebrandText(QString value) {
	if (!value.contains(u"Telegram"_q, Qt::CaseSensitive)
		&& !value.contains(u"Nicegram"_q, Qt::CaseSensitive)
		&& !value.contains(u"Official free"_q, Qt::CaseSensitive)) {
		return value;
	}
	for (const auto &replacement : kReplacements) {
		value.replace(
			QString::fromUtf16(replacement.from),
			QString::fromUtf16(replacement.to));
	}
	return value;
}

QString RebrandLangValue(ushort key, QString value) {
	// Whole sentences that call the app "official" are replaced.
	if (key == tr::lng_intro_about.base) {
		return LooksChinese(value)
			? u"基于 Telegram 的开源客户端"_q
			: u"An open-source Telegram client"_q;
	} else if (key == tr::lng_about_text1.base) {
		// Keeps the {api_link} placeholder used by the about box.
		return LooksChinese(value)
			? u"基于 {api_link} 的免费即时通讯应用，\n快速、安全。"_q
			: u"Free messaging app based on {api_link}\nfor speed and security."_q;
	}
	return RebrandBuiltinValue(key, std::move(value));
}

} // namespace Luckgram
