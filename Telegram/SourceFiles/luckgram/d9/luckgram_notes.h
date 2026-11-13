/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// D9 / mobile 05: private notes about users, groups and channels. Only on
// this device, per account, encrypted (luckgram/luckgram_storage.h), never uploaded.
// Telegram's own contact notes are a separate thing and stay untouched.
namespace Luckgram::Notes {

inline constexpr auto kFeatureKey = "user_notes";

[[nodiscard]] bool Enabled();
[[nodiscard]] rpl::producer<bool> EnabledValue();

[[nodiscard]] QString Get(not_null<PeerData*> peer);
// Saves (blank deletes) the note: trimmed, at most 2000 characters.
void Set(not_null<PeerData*> peer, const QString &note);
// The note, live; empty while there is none or the switch is off.
[[nodiscard]] rpl::producer<QString> Value(not_null<PeerData*> peer);

// The chat bar is collapsed by default and remembered per chat.
[[nodiscard]] bool BarExpanded(not_null<PeerData*> peer);
void SetBarExpanded(not_null<PeerData*> peer, bool expanded);

struct Entry {
	PeerId peer = 0;
	QString note;
	TimeId updated = 0;
};
// Every note of the account, most recently edited first.
[[nodiscard]] std::vector<Entry> All(not_null<Main::Session*> session);
// Fires the peer whose note changed.
[[nodiscard]] rpl::producer<PeerId> Changes(
	not_null<Main::Session*> session);

// Texts, same as mobile UserNotesStrings.
[[nodiscard]] QString RowTitle(); // 私人备注
[[nodiscard]] QString OnlyYou(); // 只有你能看到
[[nodiscard]] QString AddNote(); // 添加备注…
[[nodiscard]] QString BarTitle(); // 私人备注 · 只有你能看到
[[nodiscard]] QString AllNotes(); // 全部备注

// The editor (「私人备注」): title, multi-line field, "only you" info,
// Save / Cancel, and 「删除备注」 when there is a note.
void EditBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

} // namespace Luckgram::Notes
