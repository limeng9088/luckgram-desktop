/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>
#include <vector>

// D8 (mobile 11, 12, 13): the decisions of double space, hidden chats and
// store-restricted channels, without any UI or session. Only QtCore here so
// that test_luckgram_privacy builds it alone; the same cases as the mobile
// unit tests (StoreRestrictionsTest and friends) live in
// tests/test_luckgram_privacy.cpp.
namespace Luckgram::Privacy {

// ---- 13 受限频道 / store_restrictions_off

// An entry of restriction_reason only for the App Store / Google Play
// builds: its dash-separated platform list names ios and / or android and
// nothing else. "all", "android-all", unknown tokens and an empty platform
// are not store-only. Same rule as mobile StoreRestrictions.isStoreOnly.
[[nodiscard]] bool IsStoreOnly(const QString &platform);

// Whether the switch skips the entry (mobile StoreRestrictions.ignore).
[[nodiscard]] bool Ignore(bool switchOn, const QString &platform);

// Whether an entry restricts Luckgram Desktop: it names "all", "ios" or
// "android" (the platforms the mobile apps obey, desktop D8 behaves like
// them) and the switch doesn't skip it.
[[nodiscard]] bool Applies(const QString &platform, bool switchOn);

struct Restriction {
	QString platform;
	QString text;
};

// Text of the first entry that applies, or an empty string.
[[nodiscard]] QString FirstApplicable(
	const std::vector<Restriction> &list,
	bool switchOn);

// ---- 12 隐藏聊天 / hidden_chats

// Peer ids of one account, stored as "1,2,3" (sorted, no duplicates, no
// zeroes). Parsing skips garbage instead of failing.
[[nodiscard]] QString SerializeIds(std::vector<quint64> ids);
[[nodiscard]] std::vector<quint64> ParseIds(const QString &value);

// The new list after hiding / unhiding, or nullopt if nothing changes.
[[nodiscard]] std::optional<std::vector<quint64>> WithHidden(
	std::vector<quint64> ids,
	quint64 id,
	bool hidden);

// New messages of a hidden chat don't notify and don't count unless the
// user turned "notify for hidden chats" on.
[[nodiscard]] bool SuppressHiddenNotification(
	bool featureOn,
	bool hidden,
	bool notifyHidden);

// 「在隐藏的聊天中搜索」: every query word starts one of the name words
// (both already lowercased / normalized by the caller, as dialogs search
// does). An empty query matches nothing.
[[nodiscard]] bool NameMatches(
	const std::vector<QString> &queryWords,
	const std::vector<QString> &nameWords);

// ---- 11 双重空间 / double_bottom

enum class SecondPasscodeError {
	None,
	Empty,
	Mismatch, // 两次输入的密码不一致
	SameAsMain, // 第二密码不能与主密码相同
};

[[nodiscard]] SecondPasscodeError CheckNewSecondPasscode(
	const QString &passcode,
	const QString &repeat,
	const QString &main);

// An account is hidden only in the second space, only when it is logged
// in (userId != 0) and only when it isn't one of the visible ones.
[[nodiscard]] bool AccountHidden(
	bool secondSpace,
	quint64 userId,
	const std::vector<quint64> &visible);

// Ticking an account in 「第二密码下可见的账号」: the new list, or nullopt
// when that would leave no account (「至少选择一个账号」).
[[nodiscard]] std::optional<std::vector<quint64>> ToggleVisible(
	std::vector<quint64> visible,
	quint64 userId);

// The stored second-space record (luckgram.json, "double_bottom.record"):
// salt, nonce and AES-256-GCM ciphertext + tag of the local key under a
// key derived from the second passcode, then the visible account list
// sealed under a key derived from the local key. Only the encoding lives
// here, the crypto is in luckgram_double_bottom.cpp.
struct SecondRecord {
	QByteArray salt;
	QByteArray nonce;
	QByteArray sealed; // ciphertext followed by the 16-byte GCM tag
	// Version 2: the visible account list, AES-256-GCM under a key derived
	// from the local key (so it can change in the main space without the
	// second passcode). Both empty when there is none yet.
	QByteArray visibleNonce;
	QByteArray visibleSealed;
	int iterations = 0;
};

inline constexpr auto kRecordVersion = 2; // Version 1 still decodes.
inline constexpr auto kSaltSize = 16;
inline constexpr auto kNonceSize = 12;
inline constexpr auto kTagSize = 16;

[[nodiscard]] QString EncodeRecord(const SecondRecord &record);
// nullopt for anything malformed (wrong version, sizes, base64).
[[nodiscard]] std::optional<SecondRecord> DecodeRecord(const QString &value);

} // namespace Luckgram::Privacy
