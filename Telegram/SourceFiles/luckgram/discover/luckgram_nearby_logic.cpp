/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/discover/luckgram_nearby_logic.h"

#include "luckgram/discover/luckgram_square_logic.h"

#include <QtCore/QJsonArray>

#include <algorithm>
#include <cmath>

namespace Luckgram::Nearby {
namespace {

[[nodiscard]] QString L(const char *text) {
	return QString::fromLatin1(text);
}

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] QString Str(const QJsonObject &o, const char *key) {
	const auto value = o.value(L(key));
	return value.isString() ? value.toString() : QString();
}

[[nodiscard]] QString CutCodePoints(const QString &s, int max) {
	const auto ucs4 = s.toUcs4();
	if (ucs4.size() <= max) {
		return s;
	}
	return QString::fromUcs4(ucs4.constData(), max);
}

[[nodiscard]] QString OneLine(const QString &s) {
	auto result = s;
	result.replace(QChar('\n'), QChar(' '));
	return result.trimmed();
}

} // namespace

double Coarsen(double value) {
	if (std::isnan(value) || std::isinf(value)) {
		return 0.;
	}
	return std::round(value * 100.) / 100.;
}

QString Coord(double value) {
	const auto result = QString::number(Coarsen(value), 'f', 2);
	return (result == L("-0.00")) ? L("0.00") : result;
}

bool IsValidLocation(double lat, double lon) {
	return !std::isnan(lat)
		&& !std::isnan(lon)
		&& lat >= -90.
		&& lat <= 90.
		&& lon >= -180.
		&& lon <= 180.;
}

QString QueryPath(double lat, double lon) {
	return L("/v1/nearby?lat=") + Coord(lat) + L("&lon=") + Coord(lon);
}

QString CleanBio(const QString &bio) {
	return CutCodePoints(OneLine(bio), kBioMax);
}

QString CardName(
		const QString &firstName,
		const QString &lastName,
		const QString &username) {
	auto s = OneLine(firstName);
	if (s.isEmpty()) {
		s = OneLine(lastName);
	}
	if (s.isEmpty()) {
		s = username;
	}
	return CutCodePoints(s, kNameMax);
}

QJsonObject CardBody(
		const QString &displayName,
		const QString &bio,
		const QString &username,
		double lat,
		double lon,
		const std::optional<QString> &avatarBase64) {
	auto result = QJsonObject();
	result.insert(L("display_name"), CardName(displayName, QString(), username));
	result.insert(L("bio"), CleanBio(bio));
	result.insert(L("username"), username);
	// Coarsened again here, so the raw value can't be sent by mistake.
	result.insert(L("lat"), Coord(lat).toDouble());
	result.insert(L("lon"), Coord(lon).toDouble());
	if (avatarBase64) {
		result.insert(L("avatar"), *avatarBase64);
	}
	return result;
}

Precondition Check(const QString &username) {
	return username.isEmpty() ? Precondition::NeedUsername : Precondition::Ready;
}

MyState ParseMe(const QJsonObject &j, qint64 now) {
	auto result = MyState();
	result.visible = j.value(L("visible")).toBool(false);
	result.expiresAtMs = Square::ParseTime(Str(j, "expires_at"));
	if (result.visible && result.expiresAtMs <= 0) {
		result.expiresAtMs = now + kDefaultVisibleMs;
	}
	return result;
}

std::vector<Person> ParseList(const QJsonObject &j, const QString &selfUsername) {
	auto result = std::vector<Person>();
	for (const auto &value : j.value(L("items")).toArray()) {
		if (int(result.size()) >= kMaxResults) {
			break;
		}
		const auto o = value.toObject();
		auto p = Person();
		p.id = Str(o, "id");
		p.username = Str(o, "username");
		if (p.id.isEmpty() || p.username.isEmpty()) {
			continue;
		} else if (!selfUsername.isEmpty()
			&& p.username.compare(selfUsername, Qt::CaseInsensitive) == 0) {
			continue;
		}
		p.displayName = Str(o, "display_name");
		if (p.displayName.isEmpty()) {
			p.displayName = p.username;
		}
		p.bio = Str(o, "bio");
		p.avatarUrl = Str(o, "avatar_url");
		if (!p.avatarUrl.startsWith(L("https://"))) {
			p.avatarUrl = QString();
		}
		p.bucket = NormalizeBucket(Str(o, "distance_bucket"));
		result.push_back(std::move(p));
	}
	return result;
}

QStringList Buckets() {
	return { L("500m"), L("1km"), L("3km"), L("10km"), L("far") };
}

QString NormalizeBucket(const QString &bucket) {
	return Buckets().contains(bucket) ? bucket : L("far");
}

std::vector<std::pair<QString, std::vector<Person>>> Group(
		const std::vector<Person> &people) {
	auto result = std::vector<std::pair<QString, std::vector<Person>>>();
	for (const auto &bucket : Buckets()) {
		auto list = std::vector<Person>();
		for (const auto &p : people) {
			if (p.bucket == bucket) {
				list.push_back(p);
			}
		}
		if (!list.empty()) {
			result.emplace_back(bucket, std::move(list));
		}
	}
	return result;
}

bool IsFreshFix(qint64 fixTimeMs, qint64 nowMs) {
	return (fixTimeMs > 0)
		&& (nowMs - fixTimeMs <= kFallbackMaxAgeMs)
		&& (fixTimeMs <= nowMs + 60 * 1000);
}

qint64 ReminderAt(qint64 expiresAtMs, qint64 nowMs) {
	const auto at = expiresAtMs - kRemindBeforeMs;
	return (at > nowMs) ? at : 0;
}

bool MissedReminder(qint64 expiresAtMs, qint64 nowMs, qint64 remindedForMs) {
	return (expiresAtMs > 0)
		&& (ReminderAt(expiresAtMs, nowMs) == 0)
		&& !IsExpired(expiresAtMs, nowMs)
		&& (remindedForMs != expiresAtMs);
}

bool IsExpired(qint64 expiresAtMs, qint64 nowMs) {
	return (expiresAtMs > 0) && (nowMs >= expiresAtMs);
}

QStringList ReportReasons() {
	return { L("spam"), L("scam"), L("porn"), L("impersonation"), L("other") };
}

QJsonObject ReportBody(const QString &userId, const QString &reason) {
	auto result = QJsonObject();
	result.insert(L("user"), userId);
	result.insert(L("reason"), reason);
	return result;
}

QString Texts::t(const char *zh, const char *en) const {
	return U(_zh ? zh : en);
}

QString Texts::title() const { return t("附近的人", "People Nearby"); }
QString Texts::featureDesc() const { return t("用 Luckgram 服务端找到附近同样开启的人，直接发起私聊", "Find Luckgram users near you who have turned it on, and message them"); }
QString Texts::needUsernameTitle() const { return t("需要先设置用户名", "Set a username first"); }
QString Texts::needUsernameText() const { return t("别人通过你的公开用户名打开和你的私聊。设置用户名后再回来。", "Others open a chat with you through your public username. Set one, then come back."); }
QString Texts::goUsername() const { return t("去设置用户名", "Set username"); }
QString Texts::introTitle() const { return t("开始前请了解", "Before you start"); }

QString Texts::introText() const {
	return t(
		"• 打开「在附近可见」后，会上传你的名片（显示名、头像、简介、用户名）和模糊位置（约 1 公里网格，原始坐标不离开这台电脑）。\n"
		"• 只有同样打开的 Luckgram 用户能看到你，看到的是距离档位，不是位置。\n"
		"• 24 小时后自动关闭，到期前 1 小时提醒你续期。\n"
		"• 随时可以关闭；关闭或退出登录后，名片会立即从服务端删除。\n"
		"• 只在打开页面、刷新、续期时定位一次，不会后台定位。\n"
		"• 浏览列表时（包括关闭可见时）也会用同样的模糊位置向服务端查询；为防止定位追踪，服务端会短期保留你上次查询所在的约 1 公里网格，但不保存名片。",
		"• When \"Visible nearby\" is on, your card (name, photo, bio, username) and a blurred location (about a 1 km grid; the exact position never leaves this computer) are uploaded.\n"
		"• Only Luckgram users who also turned it on can see you, and they see a distance range, not a location.\n"
		"• It turns off after 24 hours; you get a reminder 1 hour before so you can renew.\n"
		"• Turn it off at any time. Turning it off or logging out deletes your card from the server right away.\n"
		"• Your location is read once when you open this page, refresh or renew, never in the background.\n"
		"• Browsing the list (also while not visible) sends the same blurred location to the server to find people; to prevent location tracking the server briefly keeps the ~1 km grid cell of your last search, but no card.");
}

QString Texts::introContinue() const { return t("我知道了，继续", "Got it, continue"); }
QString Texts::noPermission() const { return t("没有定位权限，无法显示附近的人。可以在系统设置里允许 Luckgram 使用位置。", "No location permission, so nobody can be shown. Allow Luckgram to use your location in system settings."); }
QString Texts::openSettings() const { return t("打开系统设置", "Open settings"); }
QString Texts::locationFailed() const { return t("暂时无法获取位置，请点击「刷新」重试", "Couldn't get your location. Click Refresh to try again."); }
QString Texts::locationOff() const { return t("系统定位已关闭，请先打开定位服务", "Location services are off. Turn them on first."); }
QString Texts::visibleSwitch() const { return t("在附近可见", "Visible nearby"); }
QString Texts::visibleOffInfo() const { return t("关闭时只浏览：会用模糊位置查询附近的人，但不上传名片，别人看不到你。", "While off you only browse: a blurred location is used to search, but your card isn't uploaded and nobody can see you."); }

QString Texts::visibleUntil(const QString &time) const {
	return _zh
		? U("可见到 %1，到期前 1 小时提醒你续期。").arg(time)
		: L("Visible until %1. You'll be reminded 1 hour before.").arg(time);
}

QString Texts::renew() const { return t("续期 24 小时", "Renew for 24 hours"); }
QString Texts::renewed() const { return t("已续期 24 小时", "Renewed for 24 hours"); }
QString Texts::myCard() const { return t("我的名片", "My card"); }
QString Texts::bio() const { return t("一句话简介", "Bio"); }
QString Texts::bioHint() const { return t("介绍一下自己（最多 70 字）", "Say something about yourself (up to 70 characters)"); }
QString Texts::bioEmpty() const { return t("未填写", "Not set"); }
QString Texts::showPhoto() const { return t("显示头像", "Show photo"); }
QString Texts::save() const { return t("保存", "Save"); }
QString Texts::cardSaved() const { return t("名片已更新", "Card updated"); }
QString Texts::listHeader() const { return t("附近的人", "People nearby"); }
QString Texts::empty() const { return t("附近暂时没有打开可见的人，稍后刷新试试。", "Nobody nearby has turned it on yet. Try refreshing later."); }
QString Texts::loading() const { return t("正在查找附近的人…", "Looking for people nearby…"); }
QString Texts::refresh() const { return t("刷新", "Refresh"); }

QString Texts::bucket(const QString &bucket) const {
	if (bucket == L("500m")) {
		return t("500 米内", "Within 500 m");
	} else if (bucket == L("1km")) {
		return t("1 公里内", "Within 1 km");
	} else if (bucket == L("3km")) {
		return t("3 公里内", "Within 3 km");
	} else if (bucket == L("10km")) {
		return t("10 公里内", "Within 10 km");
	}
	return t("更远", "Farther");
}

QString Texts::sendMessage() const { return t("发消息", "Send message"); }
QString Texts::report() const { return t("举报", "Report"); }
QString Texts::hide() const { return t("不再显示此人", "Don't show this person"); }

QString Texts::hideConfirm(const QString &name) const {
	return _zh
		? U("不再显示后，你和 %1 在「附近的人」和「广场」里互相看不到对方。").arg(name)
		: L("You and %1 will no longer see each other in People Nearby and Square.").arg(name);
}

QString Texts::hidden() const { return t("已不再显示此人", "You won't see this person again"); }
QString Texts::reportTitle() const { return t("举报原因", "Reason"); }

QString Texts::reportReason(const QString &reason) const {
	if (reason == L("spam")) {
		return t("垃圾信息", "Spam");
	} else if (reason == L("scam")) {
		return t("诈骗", "Scam");
	} else if (reason == L("porn")) {
		return t("色情", "Pornography");
	} else if (reason == L("impersonation")) {
		return t("冒充他人", "Impersonation");
	}
	return t("其他", "Other");
}

QString Texts::reported() const { return t("已举报，感谢反馈", "Reported. Thank you."); }
QString Texts::cancel() const { return t("取消", "Cancel"); }
QString Texts::reminderTitle() const { return t("「在附近可见」1 小时后关闭", "\"Visible nearby\" turns off in 1 hour"); }
QString Texts::reminderText() const { return t("打开「附近的人」续期，继续让附近的人看到你。", "Open People Nearby to renew and stay visible."); }

QString Texts::rateLimited(qint64 seconds) const {
	const auto minutes = std::max(qint64(1), (seconds + 59) / 60);
	return _zh
		? U("操作太频繁，请 %1 分钟后再试").arg(minutes)
		: L("Too many requests. Try again in %1 min.").arg(minutes);
}

QString Texts::disabled() const { return t("附近的人暂时不可用，请稍后再试", "People Nearby is temporarily unavailable. Please try again later."); }
QString Texts::underReview() const { return t("你的名片被举报，正在审核，暂时不会出现在附近的人里", "Your card was reported and is under review. It won't appear nearby for now."); }
QString Texts::connecting() const { return t("正在连接附近的人服务…", "Connecting to People Nearby…"); }
QString Texts::connectFailed() const { return t("暂时无法连接附近的人服务，请稍后再试", "People Nearby isn't reachable right now. Please try again later."); }
QString Texts::banned() const { return t("你的账号已被限制使用附近的人", "Your account can't use People Nearby"); }

} // namespace Luckgram::Nearby
