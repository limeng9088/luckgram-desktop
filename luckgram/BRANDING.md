# Luckgram branding in the desktop client

Where "Telegram" / "Telegram Desktop" means **this app**, the UI says
**Luckgram**. Where it means the Telegram service (account, Premium, Stars,
servers, official mobile apps, terms) it stays "Telegram".

## How it works

- `Luckgram::RebrandLangValue()` (`Telegram/SourceFiles/luckgram/luckgram_brand.cpp`)
  runs on every cloud language pack value (e.g. `zh-hans-beta`), and
  `Luckgram::RebrandBuiltinValue()` on the built-in English values.
- Substring replacements in every key: `Telegram Desktop`, `Telegram Desktop UWP`,
  `Telegram桌面版`, `Telegram 桌面版`, `Telegram 桌面端`, `Telegram桌面端`,
  `Telegram 电脑版`, `Nicegram` → `Luckgram`.
- App-name keys (`IsAppNameKey()` in the same file): every `Telegram` → `Luckgram`.
- Hard-coded strings in code use `AppName` (`core/version.h`, "Luckgram").

## App-name keys (every "Telegram" → "Luckgram")

| Area | Key | English after |
|---|---|---|
| Tray | `lng_open_from_tray` | Open Luckgram |
| Tray | `lng_quit_from_tray` | Quit Luckgram |
| Tray | `lng_tray_icon_text` | Luckgram is still running here, … |
| Settings | `lng_settings_auto_start` | Launch Luckgram when system starts |
| Settings | `lng_settings_add_sendto` | Place Luckgram in "Send to" menu |
| Settings | `lng_settings_passcode_lock_app` | Lock Luckgram with passcode |
| Settings | `lng_settings_use_winhello_about` (+ touchid / applewatch / systempwd) | … before unlocking Luckgram with Windows Hello. |
| Settings | `lng_sure_save_language` | Luckgram will restart in order to change the language |
| Settings | `lng_download_path_default_radio` | Luckgram folder in system «Downloads» |
| Settings | `lng_local_storage_device_telegram` | Luckgram cache |
| Settings | `lng_local_storage_device_usage` | Luckgram uses {percent} of your device storage. |
| Settings | `lng_proxy_sponsor_about` | … disable the proxy in Luckgram Settings. |
| Settings | `lng_proxy_web_fallback` | … Luckgram will keep retrying it in the background. … |
| Settings | `lng_export_progress` | … Please don't quit Luckgram until the data export is completed. |
| Settings | `lng_screen_reader_bar_text`, `lng_screen_reader_confirm_text` | Luckgram is working in Screen Reader mode. |
| Settings | `lng_wallet_protection_create_about` | … It does not lock Luckgram until … |
| Passcode | `lng_passcode_winhello_unlock` | Luckgram wants to unlock with Windows Hello. |
| Passcode | `lng_passcode_unlock_about` | Unlock Luckgram to continue. |
| Passcode | `lng_theme_editor_need_unlock` | You need to unlock Luckgram to save your theme. |
| Update prompts | `lng_update_telegram` | Update Luckgram |
| Update prompts | `lng_message_unsupported` | … not supported by your version of Luckgram. … |
| Update prompts | `lng_unsupported_message_text`, `lng_unsupported_block_text` | Update Luckgram to view this message |
| Update prompts | `lng_stories_unsupported` | … by your version of Luckgram. |
| Update prompts | `lng_passport_app_out_of_date` | Sorry, your Luckgram app is out of date … Please update Luckgram. |
| Errors | `lng_bad_photo`, `lng_bad_video` | Sorry, Luckgram can't process that type of image. |
| Errors | `lng_open_link_unsupported` | Luckgram can't open this link. |
| Errors | `lng_no_mic_permission` | Luckgram needs microphone access … |
| macOS only | `lng_group_call_mac_input` / `_accessibility` / `_recording`, `lng_mac_menu_show` | … for Luckgram in Privacy Settings. |

## "Telegram Desktop" keys (substring replacement)

`lng_error_start_minimized_passcoded`, `lng_proxy_unsupported`,
`lng_settings_auto_start_disabled_uwp`, `lng_theme_no_desktop`,
`lng_passcode_about`, `lng_passcode_about2`, `lng_passcode_about3`,
`lng_settings_passkeys_unsigned_error`, `lng_new_version_wrap`,
`lng_language_not_ready_about`, `lng_outdated_soon`, `lng_outdated_now`,
`lng_bot_share_location_unavailable`, `lng_group_call_mac_access`,
`lng_group_call_mac_screencast_access`, `lng_wallet_protection_open_warning_lock`.

## Whole sentences

| Key / place | 中文 | English |
|---|---|---|
| Welcome title (`Luckgram::WelcomeTitle()`) | 欢迎使用 Luckgram | Welcome to Luckgram |
| Welcome text (`Luckgram::WelcomeDescription()`, also `lng_intro_about`) | 基于 Telegram 的开源客户端 | An open-source Telegram client |
| Welcome button (`Luckgram::WelcomeButton()`) | 开始使用 | Get Started |
| `lng_about_text1` (about box) | 基于 {api_link} 的免费即时通讯应用，快速、安全。 | Free messaging app based on {api_link} for speed and security. |

## Hard-coded strings in code

| Place | Before | After |
|---|---|---|
| Window title (`Window::MainWindow`) | Telegram | Luckgram |
| About box title | Telegram Desktop | Luckgram |
| Main menu footer (`Window::MainMenu`) | Telegram Desktop → desktop.telegram.org | Luckgram → GitHub repository |
| Notification title when names are hidden | Telegram Desktop | Luckgram |
| Tray tooltip | `AppName` | Luckgram |
| Autostart / "Send to" shortcut description | Telegram autorun link … Telegram settings | Luckgram … |
| Round video recording ring text | TELEGRAM | LUCKGRAM |
| Welcome cover art | Telegram paper plane and clouds | Black tiles + white Luckgram logo (`Luckgram::PaintIntroCover()`) |

## Kept as "Telegram" (the service)

Telegram Premium / Stars / Business / Gifts / Passport / Ads / API / FAQ /
Support, "Telegram account", "joined Telegram", "Telegram servers",
"official Telegram app" (mobile, for phone number changes and age checks),
"Telegram cloud", "Logged In with Telegram", login code messages
(`lng_code_from_telegram`, `lng_code_call`), QR login ("Open Telegram on your
phone"), Terms of Service, wallet texts about Telegram backups and fees.

## Icons

App, installer, tray, window and taskbar icons come from
`luckgram/branding/icon-1024.png` (the luckgram-site icon) via
`luckgram/tools/make_icons.py`, which also writes the installer wizard images
(`luckgram/windows/wizard-small-*.bmp`). The welcome page logo is
`Telegram/Resources/art/luckgram_logo_white.png` (luckgram-site
`assets/logo-white-144.png`).
