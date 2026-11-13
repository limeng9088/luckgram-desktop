/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/pro/luckgram_pro_logic.h"

namespace Luckgram::Api {
struct Error;
} // namespace Luckgram::Api

namespace Window {
class SessionController;
} // namespace Window

// D12 (mobile 29 需求 1): the one place that says whether the user has
// Luckgram Pro, like Android LuckgramPro / iOS LuckgramPro.shared. Every Pro
// check goes through IsPro(); perks, AI credits and limits come from the
// same State (GET /v1/me, S12).
//
// Refreshed at start, when the app becomes active (at most once a minute),
// after a payment and when the Pro page opens. The last answer is cached in
// the Luckgram settings, so offline the user stays Pro until the cached
// expires_at. Nothing of the user's is sent, so no AI consent is asked.
namespace Luckgram::Pro {

// After Luckgram::Start(): loads the cache, asks the server, follows app
// activation.
void Start();

[[nodiscard]] const State &Current();
[[nodiscard]] bool IsPro();
// Fires when something a user would see changed (and on logout).
[[nodiscard]] rpl::producer<> Changes();
[[nodiscard]] rpl::producer<bool> IsProValue();

// Asks the server now (one request at a time).
void Refresh();
// Only if the last request is older than kMinRefreshIntervalMs.
void RefreshIfStale();

// Remote config: billing.enabled (default off, S7) and the numbers of the
// perk texts.
[[nodiscard]] bool BillingEnabled();
[[nodiscard]] Numbers ConfigNumbers();
// The perk rows to show now (Rows() of the live perks).
[[nodiscard]] std::vector<Row> LiveRows();

// 29 需求 6: the shared "upgrade to Pro" box: one reason line and
// 「查看 Pro」, which opens the Luckgram Pro page.
void ShowUpgrade(
	not_null<Window::SessionController*> controller,
	Reason reason);
// quota_exceeded / not_pro / credits_exhausted open the upgrade box (true);
// anything else is left to the caller (false).
bool HandleError(
	not_null<Window::SessionController*> controller,
	const Api::Error &error);

} // namespace Luckgram::Pro
