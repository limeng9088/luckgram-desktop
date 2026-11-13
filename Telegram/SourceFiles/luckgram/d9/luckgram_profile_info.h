/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

class PeerData;
class UserData;

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// D9 / mobile 07 (profile details): the profile rows. Rules live in
// luckgram_profile_info_logic.h.
namespace Luckgram::ProfileInfo {

inline constexpr auto kFeatureKey = "profile_details";

[[nodiscard]] bool Enabled();
[[nodiscard]] rpl::producer<bool> EnabledValue();

// 「ID」 row: label and value (a link that copies; empty while off).
[[nodiscard]] rpl::producer<QString> IdLabelValue();
[[nodiscard]] rpl::producer<TextWithEntities> IdTextValue(
	not_null<PeerData*> peer);
// Click copies and toasts 「ID 已复制」; right click offers 「复制 ID」 and,
// for users and bots, 「复制链接」 (tg://user?id=...).
void SetupIdLine(
	not_null<Ui::FlatLabel*> label,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

// 「注册时间 · 按 ID 估算」 row for users (not bots): 「约 2019 年 10 月」,
// empty while off or when the id is newer than the table.
[[nodiscard]] rpl::producer<QString> RegisteredLabelValue();
[[nodiscard]] rpl::producer<TextWithEntities> RegisteredTextValue(
	not_null<UserData*> user);

// Bio / description: while on, URLs, @usernames and #hashtags are clickable
// in every bio (Telegram only allows links in Premium users' bios).
[[nodiscard]] bool BioLinksEnabled();

} // namespace Luckgram::ProfileInfo
