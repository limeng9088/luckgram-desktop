/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/discover/luckgram_discover_logic.h"

#include "luckgram/discover/luckgram_nearby_logic.h"
#include "luckgram/discover/luckgram_square_logic.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QUrl>

namespace Luckgram::Discover {

std::vector<Entry> VisibleEntries(bool squareOn, bool nearbyOn) {
	auto result = std::vector<Entry>();
	if (squareOn) {
		result.push_back(Entry::Square);
	}
	if (nearbyOn) {
		result.push_back(Entry::Nearby);
	}
	return result;
}

QString Texts::t(const char *zh, const char *en) const {
	return QString::fromUtf8(_zh ? zh : en);
}

QString Texts::pageTitle() const {
	return t("发现", "Discover");
}

QString Texts::emptyText() const {
	return t("暂时没有可用的功能", "No features available right now");
}

QString Texts::title(Entry entry) const {
	return (entry == Entry::Nearby)
		? Nearby::Texts(_zh).title()
		: Square::Texts(_zh).title();
}

QString Texts::subtitle(Entry entry) const {
	return (entry == Entry::Nearby)
		? t("发现你周围的人和群组", "Find people and groups around you")
		: t("看看大家在分享什么", "Posts from people and channels");
}

} // namespace Luckgram::Discover

namespace Luckgram::Blocks {
namespace {

[[nodiscard]] QString L(const char *text) {
	return QString::fromLatin1(text);
}

[[nodiscard]] QJsonObject ParseCache(const QString &cache) {
	const auto document = QJsonDocument::fromJson(cache.toUtf8());
	return document.isObject() ? document.object() : QJsonObject();
}

[[nodiscard]] QString Serialize(const QJsonObject &o) {
	return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

} // namespace

QString ListPath(const QString &cursor) {
	return cursor.isEmpty()
		? L(kPath)
		: (L(kPath) + L("?cursor=") + Square::UrlEncode(cursor));
}

QString DeletePath(const QString &userId) {
	return L(kPath) + QChar('/') + Square::PathId(userId);
}

QJsonObject BlockBody(const QString &userId) {
	auto result = QJsonObject();
	result.insert(L("user"), userId);
	return result;
}

QStringList ParseIds(const QJsonObject &json) {
	auto result = QStringList();
	for (const auto &value : json.value(L("items")).toArray()) {
		const auto item = value.toObject();
		const auto user = item.value(L("user"));
		const auto id = user.isObject()
			? user.toObject().value(L("id")).toString()
			: user.toString();
		if (!id.isEmpty() && !result.contains(id)) {
			result.push_back(id);
		}
	}
	return result;
}

QString NextCursor(const QJsonObject &json) {
	const auto value = json.value(L("next_cursor"));
	return value.isString() ? value.toString() : QString();
}

QString PutName(const QString &cache, const QString &id, const QString &name) {
	auto o = ParseCache(cache);
	const auto trimmed = name.trimmed();
	if (trimmed.isEmpty()) {
		o.remove(id);
	} else {
		o.insert(id, trimmed);
	}
	return Serialize(o);
}

QString RemoveName(const QString &cache, const QString &id) {
	auto o = ParseCache(cache);
	o.remove(id);
	return Serialize(o);
}

QString Name(const QString &cache, const QString &id) {
	return ParseCache(cache).value(id).toString();
}

QString Texts::t(const char *zh, const char *en) const {
	return QString::fromUtf8(_zh ? zh : en);
}

QString Texts::title() const { return t("已拉黑的人", "Blocked People"); }
QString Texts::info() const { return t("拉黑后，你和对方在「附近的人」和「广场」里互相看不到。这里的拉黑和 Telegram 的拉黑互不影响。", "You and the people you block can't see each other in People Nearby and Square. This is separate from blocking on Telegram."); }
QString Texts::empty() const { return t("你还没有拉黑任何人", "You haven't blocked anyone"); }
QString Texts::unknownUser() const { return t("Luckgram 用户", "Luckgram user"); }
QString Texts::unblock() const { return t("取消拉黑", "Unblock"); }

QString Texts::unblockConfirm(const QString &name) const {
	return _zh
		? QString::fromUtf8("取消拉黑 %1？之后你们可能会在「附近的人」和「广场」里再次看到对方。").arg(name)
		: L("Unblock %1? You may see each other again in People Nearby and Square.").arg(name);
}

QString Texts::unblocked() const { return t("已取消拉黑", "Unblocked"); }
QString Texts::loading() const { return t("加载中…", "Loading…"); }
QString Texts::loadMore() const { return t("加载更多", "Load more"); }
QString Texts::retry() const { return t("重试", "Retry"); }
QString Texts::cancel() const { return t("取消", "Cancel"); }
QString Texts::connectFailed() const { return t("暂时无法连接服务，请稍后再试", "The service isn't reachable right now. Please try again later."); }

} // namespace Luckgram::Blocks
