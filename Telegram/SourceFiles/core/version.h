/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/const_string.h"

#define TDESKTOP_REQUESTED_ALPHA_VERSION (0ULL)

#ifdef TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION TDESKTOP_REQUESTED_ALPHA_VERSION
#else // TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION (0ULL)
#endif // TDESKTOP_ALLOW_CLOSED_ALPHA

// used in Updater.cpp and Setup.iss for Windows
// Luckgram: own AppId, so uninstall keys never collide with Telegram Desktop.
constexpr auto AppId = "{6F1C2B7E-4A3D-4C59-9E21-8B5D0A7C3E14}"_cs;
constexpr auto AppNameOld = "Luckgram (Legacy)"_cs;
constexpr auto AppName = "Luckgram"_cs;
constexpr auto AppFile = "Luckgram"_cs;
constexpr auto AppVersion = 7004002;
constexpr auto AppVersionStr = "7.4.2";
constexpr auto AppBetaVersion = false;
constexpr auto AppAlphaVersion = TDESKTOP_ALPHA_VERSION;
