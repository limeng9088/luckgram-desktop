/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/discover/luckgram_discover_offline.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimeZone>

namespace Luckgram::Discover::Offline {
namespace {

[[nodiscard]] QString L(const char *text) {
	return QString::fromLatin1(text);
}

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] QString Time(qint64 ms) {
	return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::utc()).toString(
		Qt::ISODateWithMs);
}

[[nodiscard]] QByteArray Json(const QJsonObject &o) {
	return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// Path without the query: "/v1/square/feed?tab=new" -> "/v1/square/feed".
[[nodiscard]] QString Route(const QString &path) {
	const auto query = path.indexOf(QChar('?'));
	return (query >= 0) ? path.left(query) : path;
}

struct Person {
	const char *id;
	const char *name;
	const char *username;
	const char *bio;
	const char *bucket;
};

// Obviously made up people (no avatar).
constexpr Person kPeople[] = {
	{ "ci-demo-1", "示例用户 A", "ci_demo_user_a", "这是 CI 截图用的示例资料。", "500m" },
	{ "ci-demo-2", "示例用户 B", "ci_demo_user_b", "示例简介。", "1km" },
	{ "ci-demo-3", "示例用户 C", "ci_demo_user_c", "", "3km" },
};

[[nodiscard]] QJsonObject Author(const Person &p) {
	return {
		{ L("id"), L(p.id) },
		{ L("display_name"), U(p.name) },
		{ L("username"), L(p.username) },
		{ L("bio"), U(p.bio) },
	};
}

[[nodiscard]] QJsonObject Feed(qint64 now) {
	struct PostData {
		int author;
		const char *text;
		int likes;
		int comments;
		qint64 agoMinutes;
	};
	constexpr PostData kPosts[] = {
		{ 0, "这是一条示例动态，用于 CI 截图，不是真实内容。", 12, 3, 5 },
		{ 1, "示例动态：今天天气不错。", 4, 0, 42 },
		{ 2, "示例动态：广场的列表、点赞和评论入口。", 27, 8, 180 },
	};
	auto items = QJsonArray();
	auto index = 0;
	for (const auto &post : kPosts) {
		items.push_back(QJsonObject{
			{ L("id"), L("ci-demo-post-") + QString::number(++index) },
			{ L("author"), Author(kPeople[post.author]) },
			{ L("text"), U(post.text) },
			{ L("images"), QJsonArray() },
			{ L("like_count"), post.likes },
			{ L("comment_count"), post.comments },
			{ L("liked"), false },
			{ L("created_at"), Time(now - post.agoMinutes * 60 * 1000) },
			{ L("status"), L("published") },
		});
	}
	return { { L("items"), items }, { L("next_cursor"), QString() } };
}

[[nodiscard]] QJsonObject Nearby() {
	auto items = QJsonArray();
	for (const auto &p : kPeople) {
		items.push_back(QJsonObject{
			{ L("id"), L(p.id) },
			{ L("display_name"), U(p.name) },
			{ L("username"), L(p.username) },
			{ L("bio"), U(p.bio) },
			{ L("distance_bucket"), L(p.bucket) },
		});
	}
	return { { L("items"), items } };
}

[[nodiscard]] QJsonObject Empty() {
	return { { L("items"), QJsonArray() }, { L("next_cursor"), QString() } };
}

[[nodiscard]] Reply Ok(const QJsonObject &o) {
	return { .status = 200, .body = Json(o) };
}

} // namespace

bool Handles(const QString &path) {
	const auto route = Route(path);
	for (const auto prefix : { "/v1/square", "/v1/nearby", "/v1/blocks", "/v1/link" }) {
		const auto p = L(prefix);
		if (route == p || route.startsWith(p + QChar('/'))) {
			return true;
		}
	}
	return false;
}

std::optional<Reply> Answer(
		const QByteArray &method,
		const QString &path,
		qint64 nowMs) {
	if (!Handles(path)) {
		return std::nullopt;
	}
	const auto route = Route(path);
	if (method == "GET") {
		if (route == L("/v1/square/feed")) {
			return Ok(Feed(nowMs));
		} else if (route == L("/v1/square/profile")) {
			return Ok({
				{ L("display_name"), U("示例用户（CI）") },
				{ L("bio"), U("CI 截图用的示例资料。") },
			});
		} else if (route == L("/v1/nearby")) {
			return Ok(Nearby());
		} else if (route == L("/v1/nearby/me")) {
			return Ok({ { L("visible"), false } });
		} else if (route == L("/v1/link")) {
			return Ok({ { L("linked"), true } });
		}
		// Comments, someone's posts, the block list: empty.
		return Ok(Empty());
	}
	return Reply{
		.status = 503,
		.body = Json({ { L("error"), QJsonObject{
			{ L("code"), L("upstream_unavailable") },
		} } }),
	};
}

} // namespace Luckgram::Discover::Offline
