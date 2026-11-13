/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <optional>
#include <utility>
#include <vector>

// D10 (mobile 25, server S10): pure logic of People Nearby, ported from
// Android NearbyLogic: coordinate coarsening, parsing, distance groups,
// expiry and reminder times, preconditions and texts. QtCore only, unit
// tested in luckgram_tests (cases from NearbyLogicTest). The Android
// location provider chain is not here: the desktop reads the position
// from the system location service or from the user (D10 desktop rules).
namespace Luckgram::Nearby {

inline constexpr auto kFeatureKey = "nearby";

inline constexpr auto kBioMax = 70;
inline constexpr auto kNameMax = 64;
inline constexpr auto kMaxResults = 100;
inline constexpr auto kRemindBeforeMs = qint64(60) * 60 * 1000;
inline constexpr auto kDefaultVisibleMs = qint64(24) * 60 * 60 * 1000;
inline constexpr auto kAvatarMaxBytes = 256 * 1024;
inline constexpr auto kFallbackMaxAgeMs = qint64(30) * 60 * 1000;

// ~1 km grid (2 decimals); the raw value never leaves the device.
[[nodiscard]] double Coarsen(double value);
// As sent: 2 decimals, '.' separator, no "-0.00".
[[nodiscard]] QString Coord(double value);
[[nodiscard]] bool IsValidLocation(double lat, double lon);
[[nodiscard]] QString QueryPath(double lat, double lon);

// Bio: one line, trimmed, at most 70 code points.
[[nodiscard]] QString CleanBio(const QString &bio);
// First name, else last name, else the username; at most 64 code points.
[[nodiscard]] QString CardName(
	const QString &firstName,
	const QString &lastName,
	const QString &username);
// avatarBase64: nullopt = leave the photo as it is, "" = delete it.
[[nodiscard]] QJsonObject CardBody(
	const QString &displayName,
	const QString &bio,
	const QString &username,
	double lat,
	double lon,
	const std::optional<QString> &avatarBase64);

enum class Precondition {
	Ready,
	NeedUsername,
};

// Only the public username is up to the user; the S6 link is silent.
[[nodiscard]] Precondition Check(const QString &username);

struct Person {
	QString id;
	QString displayName;
	QString bio;
	QString username;
	QString avatarUrl;
	QString bucket;
};

struct MyState {
	bool visible = false;
	qint64 expiresAtMs = 0;
};

[[nodiscard]] MyState ParseMe(const QJsonObject &j, qint64 now);
// GET /v1/nearby; drops entries without id / username and the own card.
[[nodiscard]] std::vector<Person> ParseList(
	const QJsonObject &j,
	const QString &selfUsername);

[[nodiscard]] QStringList Buckets();
[[nodiscard]] QString NormalizeBucket(const QString &bucket);
// Buckets() order, empty groups left out, server order inside a group.
[[nodiscard]] std::vector<std::pair<QString, std::vector<Person>>> Group(
	const std::vector<Person> &people);

[[nodiscard]] bool IsFreshFix(qint64 fixTimeMs, qint64 nowMs);
// When to show "turns off in 1 hour", 0 if already too late.
[[nodiscard]] qint64 ReminderAt(qint64 expiresAtMs, qint64 nowMs);
// The reminder time passed while the app was not running.
[[nodiscard]] bool MissedReminder(
	qint64 expiresAtMs,
	qint64 nowMs,
	qint64 remindedForMs);
[[nodiscard]] bool IsExpired(qint64 expiresAtMs, qint64 nowMs);

[[nodiscard]] QStringList ReportReasons();
[[nodiscard]] QJsonObject ReportBody(const QString &userId, const QString &reason);

class Texts final {
public:
	explicit Texts(bool zh) : _zh(zh) {
	}

	[[nodiscard]] QString title() const;
	[[nodiscard]] QString featureDesc() const;
	[[nodiscard]] QString needUsernameTitle() const;
	[[nodiscard]] QString needUsernameText() const;
	[[nodiscard]] QString goUsername() const;
	[[nodiscard]] QString introTitle() const;
	[[nodiscard]] QString introText() const;
	[[nodiscard]] QString introContinue() const;
	[[nodiscard]] QString noPermission() const;
	[[nodiscard]] QString openSettings() const;
	[[nodiscard]] QString locationFailed() const;
	[[nodiscard]] QString locationOff() const;
	[[nodiscard]] QString visibleSwitch() const;
	[[nodiscard]] QString visibleOffInfo() const;
	[[nodiscard]] QString visibleUntil(const QString &time) const;
	[[nodiscard]] QString renew() const;
	[[nodiscard]] QString renewed() const;
	[[nodiscard]] QString myCard() const;
	[[nodiscard]] QString bio() const;
	[[nodiscard]] QString bioHint() const;
	[[nodiscard]] QString bioEmpty() const;
	[[nodiscard]] QString showPhoto() const;
	[[nodiscard]] QString save() const;
	[[nodiscard]] QString cardSaved() const;
	[[nodiscard]] QString listHeader() const;
	[[nodiscard]] QString empty() const;
	[[nodiscard]] QString loading() const;
	[[nodiscard]] QString refresh() const;
	[[nodiscard]] QString bucket(const QString &bucket) const;
	[[nodiscard]] QString sendMessage() const;
	[[nodiscard]] QString report() const;
	[[nodiscard]] QString hide() const;
	[[nodiscard]] QString hideConfirm(const QString &name) const;
	[[nodiscard]] QString hidden() const;
	[[nodiscard]] QString reportTitle() const;
	[[nodiscard]] QString reportReason(const QString &reason) const;
	[[nodiscard]] QString reported() const;
	[[nodiscard]] QString cancel() const;
	[[nodiscard]] QString reminderTitle() const;
	[[nodiscard]] QString reminderText() const;
	[[nodiscard]] QString rateLimited(qint64 seconds) const;
	[[nodiscard]] QString disabled() const;
	[[nodiscard]] QString underReview() const;
	[[nodiscard]] QString connecting() const;
	[[nodiscard]] QString connectFailed() const;
	[[nodiscard]] QString banned() const;

	// Desktop only (D10 "桌面端差异": where the position comes from).
	[[nodiscard]] QString sourceSystem() const;
	[[nodiscard]] QString sourceManual() const;
	[[nodiscard]] QString systemLocationFailed() const;
	[[nodiscard]] QString pickOnMap() const;
	[[nodiscard]] QString useThisLocation() const;
	[[nodiscard]] QString useSystemLocation() const;
	[[nodiscard]] QString mapUnavailable() const;

private:
	[[nodiscard]] QString t(const char *zh, const char *en) const;

	bool _zh = false;

};

} // namespace Luckgram::Nearby
