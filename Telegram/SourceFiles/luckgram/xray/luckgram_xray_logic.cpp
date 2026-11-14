/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/xray/luckgram_xray_logic.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>

#include <algorithm>
#include <array>
#include <utility>

namespace Luckgram::Xray {
namespace {

using Query = std::vector<std::pair<QString, QString>>;

constexpr auto kBase64Alphabet
	= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

[[nodiscard]] QString L(const char *latin1) {
	return QString::fromLatin1(latin1);
}

// java.net.URLEncoder + " " -> "%20": letters, digits and ".-*_" stay.
[[nodiscard]] QString Encode(const QString &text) {
	auto result = QString();
	const auto utf8 = text.toUtf8();
	for (const auto byte : utf8) {
		const auto c = uchar(byte);
		if ((c >= 'a' && c <= 'z')
			|| (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9')
			|| c == '.' || c == '-' || c == '*' || c == '_') {
			result += QChar(c);
		} else {
			result += QChar('%');
			result += QString::number(c >> 4, 16).toUpper();
			result += QString::number(c & 0x0F, 16).toUpper();
		}
	}
	return result;
}

// Percent-decoding as UTF-8; '+' stays '+' (mobile: "+" -> "%2B" first).
[[nodiscard]] QString Decode(const QString &text) {
	return QUrl::fromPercentEncoding(text.toUtf8());
}

[[nodiscard]] int ParseInt(const QString &text) {
	auto ok = false;
	const auto result = text.trimmed().toInt(&ok);
	return ok ? result : -1;
}

[[nodiscard]] int ParsePort(const QString &text) {
	const auto port = ParseInt(text);
	return (port > 0 && port < 65536) ? port : -1;
}

[[nodiscard]] QString Lower(const QString &text) {
	return text.trimmed().toLower();
}

[[nodiscard]] QString NonEmpty(const QString &a, const QString &b) {
	return a.isEmpty() ? b : a;
}

void PutIf(Query &query, const char *key, const QString &value) {
	if (!value.isEmpty()) {
		query.emplace_back(L(key), value);
	}
}

void Put(Query &query, const char *key, const QString &value) {
	query.emplace_back(L(key), value);
}

[[nodiscard]] QString Get(
		const std::map<QString, QString> &map,
		const char *key) {
	const auto i = map.find(L(key));
	return (i != end(map)) ? i->second : QString();
}

[[nodiscard]] bool Has(
		const std::map<QString, QString> &map,
		const char *key) {
	return map.find(L(key)) != end(map);
}

// "host:port" or "[v6]:port".
[[nodiscard]] std::optional<std::pair<QString, QString>> HostPort(
		const QString &text) {
	if (text.startsWith(QChar('['))) {
		const auto close = text.indexOf(QChar(']'));
		if (close < 0
			|| text.size() < close + 3
			|| text[close + 1] != QChar(':')) {
			return std::nullopt;
		}
		return std::make_pair(text.mid(1, close - 1), text.mid(close + 2));
	}
	const auto colon = text.lastIndexOf(QChar(':'));
	if (colon <= 0) {
		return std::nullopt;
	}
	return std::make_pair(text.left(colon), text.mid(colon + 1));
}

[[nodiscard]] QString HostForUrl(const QString &host) {
	return (host.contains(QChar(':')) && !host.startsWith(QChar('[')))
		? (QChar('[') + host + QChar(']'))
		: host;
}

// Cuts "#name" and "?query" off rest; returns the decoded name.
[[nodiscard]] QString TakeNameAndQuery(QString &rest, QString &query) {
	auto name = QString();
	const auto hash = rest.indexOf(QChar('#'));
	if (hash >= 0) {
		name = Decode(rest.mid(hash + 1));
		rest = rest.left(hash);
	}
	const auto question = rest.indexOf(QChar('?'));
	if (question >= 0) {
		query = rest.mid(question + 1);
		rest = rest.left(question);
	}
	if (rest.endsWith(QChar('/'))) {
		rest.chop(1);
	}
	return name;
}

// scheme://id@host:port?query#name: id, host and port are required.
[[nodiscard]] std::optional<Node> ParseUrl(
		const QString &protocol,
		const QString &text) {
	auto rest = text.mid(text.indexOf(QStringLiteral("://")) + 3);
	auto query = QString();
	const auto name = TakeNameAndQuery(rest, query);
	const auto at = rest.lastIndexOf(QChar('@'));
	if (at <= 0) {
		return std::nullopt;
	}
	const auto id = rest.left(at);
	const auto hostPort = HostPort(rest.mid(at + 1));
	if (!hostPort || Decode(id).isEmpty()) {
		return std::nullopt;
	}
	const auto port = ParsePort(hostPort->second);
	if (port <= 0) {
		return std::nullopt;
	}
	auto link = protocol + QStringLiteral("://") + id + QChar('@')
		+ HostForUrl(hostPort->first) + QChar(':') + QString::number(port);
	if (!query.isEmpty()) {
		link += QChar('?') + query;
	}
	if (!name.isEmpty()) {
		link += QChar('#') + Encode(name);
	}
	return Node{
		.protocol = protocol,
		.host = hostPort->first,
		.port = port,
		.name = name,
		.link = link,
	};
}

[[nodiscard]] QString BuildLink(
		const QString &protocol,
		const QString &id,
		const QString &host,
		int port,
		const Query &query,
		const QString &name) {
	if (id.isEmpty() || host.isEmpty() || port <= 0) {
		return QString();
	}
	auto result = protocol + QStringLiteral("://") + Encode(id) + QChar('@')
		+ HostForUrl(host) + QChar(':') + QString::number(port);
	auto separator = QChar('?');
	for (const auto &[key, value] : query) {
		result += separator + key + QChar('=') + Encode(value);
		separator = QChar('&');
	}
	if (!name.isEmpty()) {
		result += QChar('#') + Encode(name);
	}
	return result;
}

// SIP002 (ss://base64url(method:pass)@host:port or plain method:pass) and
// the legacy ss://base64(method:pass@host:port).
[[nodiscard]] std::optional<Node> ParseShadowsocks(const QString &text) {
	auto rest = text.mid(5);
	auto query = QString();
	const auto name = TakeNameAndQuery(rest, query);
	if (query.toLower().contains(QStringLiteral("plugin="))) {
		return std::nullopt; // obfs / v2ray-plugin: not in Xray.
	}
	auto userInfo = std::optional<QString>();
	auto hostPortText = QString();
	const auto at = rest.lastIndexOf(QChar('@'));
	if (at < 0) {
		const auto decoded = Base64Text(rest);
		if (!decoded) {
			return std::nullopt;
		}
		const auto innerAt = decoded->lastIndexOf(QChar('@'));
		if (innerAt <= 0) {
			return std::nullopt;
		}
		userInfo = decoded->left(innerAt);
		hostPortText = decoded->mid(innerAt + 1);
	} else {
		const auto user = Decode(rest.left(at));
		userInfo = user.contains(QChar(':')) ? user : Base64Text(user);
		hostPortText = rest.mid(at + 1);
	}
	if (!userInfo) {
		return std::nullopt;
	}
	const auto colon = userInfo->indexOf(QChar(':'));
	if (colon <= 0 || colon == userInfo->size() - 1) {
		return std::nullopt;
	}
	const auto hostPort = HostPort(hostPortText);
	if (!hostPort) {
		return std::nullopt;
	}
	const auto port = ParsePort(hostPort->second);
	if (port <= 0) {
		return std::nullopt;
	}
	const auto link = QStringLiteral("ss://")
		+ Base64UrlNoPad(userInfo->toUtf8())
		+ QChar('@') + HostForUrl(hostPort->first)
		+ QChar(':') + QString::number(port)
		+ (name.isEmpty() ? QString() : (QChar('#') + Encode(name)));
	return Node{
		.protocol = L(kShadowsocks),
		.host = hostPort->first,
		.port = port,
		.name = name,
		.link = link,
	};
}

[[nodiscard]] QString JsonString(const QJsonObject &json, const char *key) {
	const auto value = json.value(L(key));
	if (value.isString()) {
		return value.toString();
	} else if (value.isDouble()) {
		return QString::number(qint64(value.toDouble()));
	}
	return QString();
}

// v2rayN: vmess://base64({"add","port","id","net","host","path","tls",...}).
[[nodiscard]] std::optional<Node> ParseVmessJson(QString base64) {
	const auto hash = base64.indexOf(QChar('#'));
	if (hash >= 0) {
		base64 = base64.left(hash);
	}
	const auto text = Base64Text(base64);
	if (!text || !text->trimmed().startsWith(QChar('{'))) {
		return std::nullopt;
	}
	const auto document = QJsonDocument::fromJson(text->toUtf8());
	if (!document.isObject()) {
		return std::nullopt;
	}
	const auto o = document.object();
	// alterId > 0 is legacy VMess (MD5 auth); Xray only has AEAD.
	if (ParseInt(JsonString(o, "aid")) > 0) {
		return std::nullopt;
	}
	auto query = Query();
	const auto net = JsonString(o, "net");
	const auto tls = Lower(JsonString(o, "tls"));
	Put(query, "encryption", NonEmpty(JsonString(o, "scy"), QStringLiteral("auto")));
	Put(query, "security", (tls == QStringLiteral("tls"))
		? QStringLiteral("tls")
		: (tls == QStringLiteral("reality"))
		? QStringLiteral("reality")
		: QStringLiteral("none"));
	PutIf(query, "sni", JsonString(o, "sni"));
	PutIf(query, "fp", JsonString(o, "fp"));
	PutIf(query, "alpn", JsonString(o, "alpn"));
	Put(query, "type", net.isEmpty() ? QStringLiteral("tcp") : net);
	if (net == QStringLiteral("grpc")) {
		PutIf(query, "serviceName", JsonString(o, "path"));
	} else {
		PutIf(query, "host", JsonString(o, "host"));
		PutIf(query, "path", JsonString(o, "path"));
	}
	if ((net == QStringLiteral("tcp") || net.isEmpty())
		&& JsonString(o, "type") == QStringLiteral("http")) {
		Put(query, "headerType", QStringLiteral("http"));
	}
	const auto link = BuildLink(
		L(kVmess),
		JsonString(o, "id"),
		JsonString(o, "add"),
		ParsePort(JsonString(o, "port")),
		query,
		JsonString(o, "ps"));
	return link.isEmpty() ? std::nullopt : ParseUrl(L(kVmess), link);
}

// tls / reality -> security, sni, fp, pbk, sid.
void ClashSecurity(const std::map<QString, QString> &m, Query &query) {
	const auto reality = Has(m, "reality-opts.public-key");
	const auto tls = (Lower(Get(m, "tls")) == QStringLiteral("true"));
	if (reality) {
		Put(query, "security", QStringLiteral("reality"));
		PutIf(query, "pbk", Get(m, "reality-opts.public-key"));
		PutIf(query, "sid", Get(m, "reality-opts.short-id"));
	} else {
		Put(query, "security", tls ? QStringLiteral("tls") : QStringLiteral("none"));
	}
	if (tls || reality) {
		PutIf(query, "sni", NonEmpty(Get(m, "servername"), Get(m, "sni")));
		PutIf(query, "fp", Get(m, "client-fingerprint"));
	}
}

// network -> type, host, path, serviceName. False for a transport Xray
// can't use.
[[nodiscard]] bool ClashTransport(
		const std::map<QString, QString> &m,
		Query &query) {
	const auto network = Lower(NonEmpty(Get(m, "network"), QStringLiteral("tcp")));
	if (network == QStringLiteral("tcp")) {
		Put(query, "type", QStringLiteral("tcp"));
	} else if (network == QStringLiteral("ws")) {
		Put(query, "type", QStringLiteral("ws"));
		PutIf(query, "host", NonEmpty(
			Get(m, "ws-opts.headers.Host"),
			Get(m, "ws-opts.headers.host")));
		PutIf(query, "path", Get(m, "ws-opts.path"));
	} else if (network == QStringLiteral("grpc")) {
		Put(query, "type", QStringLiteral("grpc"));
		PutIf(query, "serviceName", Get(m, "grpc-opts.grpc-service-name"));
	} else if (network == QStringLiteral("h2") || network == QStringLiteral("http")) {
		Put(query, "type", QStringLiteral("http"));
		PutIf(query, "path", Get(m, "h2-opts.path"));
	} else if (network == QStringLiteral("xhttp")) {
		Put(query, "type", QStringLiteral("xhttp"));
		PutIf(query, "host", Get(m, "xhttp-opts.host"));
		PutIf(query, "path", Get(m, "xhttp-opts.path"));
		PutIf(query, "mode", Get(m, "xhttp-opts.mode"));
	} else {
		return false;
	}
	return true;
}

// The parts of a canonical link, for Outbound().
struct Parts {
	QString protocol;
	QString id; // Decoded.
	QString host;
	int port = 0;
	std::map<QString, QString> query; // Decoded values.
};

[[nodiscard]] std::optional<Parts> Split(const QString &link) {
	const auto node = Parse(link);
	if (!node) {
		return std::nullopt;
	}
	auto rest = node->link.mid(node->link.indexOf(QStringLiteral("://")) + 3);
	auto queryText = QString();
	(void)TakeNameAndQuery(rest, queryText);
	auto result = Parts{
		.protocol = node->protocol,
		.id = Decode(rest.left(rest.lastIndexOf(QChar('@')))),
		.host = node->host,
		.port = node->port,
	};
	for (const auto &pair : queryText.split(QChar('&'), Qt::SkipEmptyParts)) {
		const auto equal = pair.indexOf(QChar('='));
		const auto key = (equal >= 0) ? pair.left(equal) : pair;
		const auto value = (equal >= 0) ? Decode(pair.mid(equal + 1)) : QString();
		result.query.emplace(key, value);
	}
	return result;
}

[[nodiscard]] QJsonArray Strings(const QString &commaSeparated) {
	auto result = QJsonArray();
	for (const auto &part : commaSeparated.split(QChar(','), Qt::SkipEmptyParts)) {
		result.push_back(part.trimmed());
	}
	return result;
}

// streamSettings from the query; empty object when unusable.
[[nodiscard]] std::optional<QJsonObject> StreamSettings(const Parts &parts) {
	const auto &q = parts.query;
	const auto value = [&](const char *key) {
		const auto i = q.find(L(key));
		return (i != end(q)) ? i->second : QString();
	};
	auto stream = QJsonObject();
	auto network = Lower(NonEmpty(value("type"), QStringLiteral("tcp")));
	if (network == QStringLiteral("h2")) {
		network = QStringLiteral("http");
	} else if (network == QStringLiteral("splithttp")) {
		network = QStringLiteral("xhttp");
	}
	const auto host = value("host");
	const auto path = value("path");
	if (network == QStringLiteral("tcp") || network == QStringLiteral("raw")) {
		network = QStringLiteral("tcp");
		if (value("headerType") == QStringLiteral("http")) {
			auto request = QJsonObject();
			request.insert(QStringLiteral("path"), QJsonArray{ NonEmpty(path, QStringLiteral("/")) });
			if (!host.isEmpty()) {
				request.insert(QStringLiteral("headers"), QJsonObject{
					{ QStringLiteral("Host"), Strings(host) },
				});
			}
			stream.insert(QStringLiteral("tcpSettings"), QJsonObject{
				{ QStringLiteral("header"), QJsonObject{
					{ QStringLiteral("type"), QStringLiteral("http") },
					{ QStringLiteral("request"), request },
				} },
			});
		}
	} else if (network == QStringLiteral("ws")) {
		auto ws = QJsonObject{ { QStringLiteral("path"), NonEmpty(path, QStringLiteral("/")) } };
		if (!host.isEmpty()) {
			ws.insert(QStringLiteral("host"), host);
		}
		stream.insert(QStringLiteral("wsSettings"), ws);
	} else if (network == QStringLiteral("grpc")) {
		stream.insert(QStringLiteral("grpcSettings"), QJsonObject{
			{ QStringLiteral("serviceName"), value("serviceName") },
			{ QStringLiteral("multiMode"), value("mode") == QStringLiteral("multi") },
		});
	} else if (network == QStringLiteral("http")) {
		auto http = QJsonObject{ { QStringLiteral("path"), NonEmpty(path, QStringLiteral("/")) } };
		if (!host.isEmpty()) {
			http.insert(QStringLiteral("host"), Strings(host));
		}
		stream.insert(QStringLiteral("httpSettings"), http);
	} else if (network == QStringLiteral("xhttp") || network == QStringLiteral("httpupgrade")) {
		auto settings = QJsonObject{ { QStringLiteral("path"), NonEmpty(path, QStringLiteral("/")) } };
		if (!host.isEmpty()) {
			settings.insert(QStringLiteral("host"), host);
		}
		if (network == QStringLiteral("xhttp") && !value("mode").isEmpty()) {
			settings.insert(QStringLiteral("mode"), value("mode"));
		}
		stream.insert(network + QStringLiteral("Settings"), settings);
	} else {
		return std::nullopt; // kcp, quic...: not offered.
	}
	stream.insert(QStringLiteral("network"), network);

	const auto security = Lower(NonEmpty(value("security"), QStringLiteral("none")));
	const auto sni = NonEmpty(value("sni"), host);
	if (security == QStringLiteral("tls")) {
		auto tls = QJsonObject();
		if (!sni.isEmpty()) {
			tls.insert(QStringLiteral("serverName"), sni);
		}
		if (!value("fp").isEmpty()) {
			tls.insert(QStringLiteral("fingerprint"), value("fp"));
		}
		if (!value("alpn").isEmpty()) {
			tls.insert(QStringLiteral("alpn"), Strings(value("alpn")));
		}
		if (value("allowInsecure") == QStringLiteral("1")
			|| value("insecure") == QStringLiteral("1")) {
			tls.insert(QStringLiteral("allowInsecure"), true);
		}
		stream.insert(QStringLiteral("security"), QStringLiteral("tls"));
		stream.insert(QStringLiteral("tlsSettings"), tls);
	} else if (security == QStringLiteral("reality")) {
		if (value("pbk").isEmpty()) {
			return std::nullopt;
		}
		auto reality = QJsonObject{
			{ QStringLiteral("publicKey"), value("pbk") },
			{ QStringLiteral("fingerprint"), NonEmpty(value("fp"), QStringLiteral("chrome")) },
		};
		if (!sni.isEmpty()) {
			reality.insert(QStringLiteral("serverName"), sni);
		}
		if (!value("sid").isEmpty()) {
			reality.insert(QStringLiteral("shortId"), value("sid"));
		}
		if (!value("spx").isEmpty()) {
			reality.insert(QStringLiteral("spiderX"), value("spx"));
		}
		stream.insert(QStringLiteral("security"), QStringLiteral("reality"));
		stream.insert(QStringLiteral("realitySettings"), reality);
	} else if (security != QStringLiteral("none")) {
		return std::nullopt;
	}
	return stream;
}

} // namespace

QString Node::key() const {
	const auto hash = link.indexOf(QChar('#'));
	return QStringLiteral("xray|") + ((hash >= 0) ? link.left(hash) : link);
}

QString Label(const QString &protocol) {
	if (protocol == L(kVless)) {
		return QStringLiteral("VLESS");
	} else if (protocol == L(kVmess)) {
		return QStringLiteral("VMess");
	} else if (protocol == L(kTrojan)) {
		return QStringLiteral("Trojan");
	} else if (protocol == L(kShadowsocks)) {
		return QStringLiteral("SS");
	}
	return protocol.toUpper();
}

bool LooksLikeLink(const QString &raw) {
	const auto lower = raw.trimmed().toLower();
	return lower.startsWith(QStringLiteral("vless://"))
		|| lower.startsWith(QStringLiteral("vmess://"))
		|| lower.startsWith(QStringLiteral("trojan://"))
		|| lower.startsWith(QStringLiteral("ss://"));
}

std::optional<Node> Parse(const QString &raw) {
	const auto text = raw.trimmed();
	const auto lower = text.toLower();
	if (lower.startsWith(QStringLiteral("vmess://"))) {
		if (auto node = ParseVmessJson(text.mid(8))) {
			return node;
		}
		return ParseUrl(L(kVmess), text);
	} else if (lower.startsWith(QStringLiteral("ss://"))) {
		return ParseShadowsocks(text);
	} else if (lower.startsWith(QStringLiteral("vless://"))) {
		return ParseUrl(L(kVless), text);
	} else if (lower.startsWith(QStringLiteral("trojan://"))) {
		return ParseUrl(L(kTrojan), text);
	}
	return std::nullopt;
}

QString FromClash(const std::map<QString, QString> &m) {
	const auto type = Lower(Get(m, "type"));
	const auto server = Get(m, "server");
	const auto port = ParsePort(Get(m, "port"));
	const auto name = Get(m, "name");
	if (server.isEmpty() || port <= 0) {
		return QString();
	}
	auto query = Query();
	if (type == L(kVless)) {
		Put(query, "encryption", NonEmpty(Get(m, "encryption"), QStringLiteral("none")));
		PutIf(query, "flow", Get(m, "flow"));
		ClashSecurity(m, query);
		if (!ClashTransport(m, query)) {
			return QString();
		}
		return BuildLink(L(kVless), Get(m, "uuid"), server, port, query, name);
	} else if (type == L(kVmess)) {
		if (ParseInt(Get(m, "alterId")) > 0) {
			return QString(); // Legacy MD5 auth.
		}
		Put(query, "encryption", NonEmpty(Get(m, "cipher"), QStringLiteral("auto")));
		ClashSecurity(m, query);
		if (!ClashTransport(m, query)) {
			return QString();
		}
		return BuildLink(L(kVmess), Get(m, "uuid"), server, port, query, name);
	} else if (type == L(kTrojan)) {
		Put(query, "security", QStringLiteral("tls"));
		PutIf(query, "sni", NonEmpty(Get(m, "sni"), Get(m, "servername")));
		PutIf(query, "fp", Get(m, "client-fingerprint"));
		if (Has(m, "reality-opts.public-key")) {
			query[0].second = QStringLiteral("reality");
			PutIf(query, "pbk", Get(m, "reality-opts.public-key"));
			PutIf(query, "sid", Get(m, "reality-opts.short-id"));
		}
		if (!ClashTransport(m, query)) {
			return QString();
		}
		return BuildLink(
			L(kTrojan),
			Get(m, "password"),
			server,
			port,
			query,
			name);
	} else if (type == L(kShadowsocks)) {
		if (Has(m, "plugin")) {
			return QString(); // obfs / v2ray-plugin / shadow-tls.
		}
		const auto cipher = Get(m, "cipher");
		const auto password = Get(m, "password");
		if (cipher.isEmpty() || password.isEmpty()) {
			return QString();
		}
		return QStringLiteral("ss://")
			+ Base64UrlNoPad((cipher + QChar(':') + password).toUtf8())
			+ QChar('@') + HostForUrl(server) + QChar(':') + QString::number(port)
			+ (name.isEmpty() ? QString() : (QChar('#') + Encode(name)));
	}
	return QString();
}

bool IsPrivateAddress(const QString &host) {
	auto h = Lower(host);
	if (h.startsWith(QChar('[')) && h.endsWith(QChar(']'))) {
		h = h.mid(1, h.size() - 2);
	}
	if (h == QStringLiteral("localhost")) {
		return true;
	}
	static const auto ipv4 = QRegularExpression(
		QStringLiteral("^(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})$"));
	if (const auto m = ipv4.match(h); m.hasMatch()) {
		auto b = std::array<int, 4>();
		for (auto i = 0; i != 4; ++i) {
			b[i] = m.captured(i + 1).toInt();
			if (b[i] > 255) {
				return false;
			}
		}
		return (b[0] == 10)
			|| (b[0] == 127)
			|| (b[0] == 172 && b[1] >= 16 && b[1] <= 31)
			|| (b[0] == 192 && b[1] == 168)
			|| (b[0] == 169 && b[1] == 254);
	}
	if (!h.contains(QChar(':'))) {
		return false; // A host name.
	}
	if (h == QStringLiteral("::1")) {
		return true;
	}
	// fc00::/7 (ULA) and fe80::/10 (link-local): the first group.
	const auto first = h.section(QChar(':'), 0, 0);
	if (first.size() != 4) {
		return false; // "::..." or a short first group: not one of those.
	}
	const auto value = first.toUInt(nullptr, 16);
	return ((value & 0xFE00) == 0xFC00) || ((value & 0xFFC0) == 0xFE80);
}

bool NeedsEncryption(const QString &link) {
	const auto parts = Split(link);
	if (!parts
		|| (parts->protocol != L(kVless) && parts->protocol != L(kTrojan))) {
		return false;
	}
	const auto value = [&](const char *key) {
		const auto i = parts->query.find(L(key));
		return (i != end(parts->query)) ? Lower(i->second) : QString();
	};
	const auto security = NonEmpty(value("security"), QStringLiteral("none"));
	if (security != QStringLiteral("none")) {
		return false; // tls / reality (anything else isn't built anyway).
	}
	const auto encryption = value("encryption");
	if (parts->protocol == L(kVless)
		&& !encryption.isEmpty()
		&& encryption != QStringLiteral("none")) {
		return false; // VLESS encryption (mlkem768x25519plus...).
	}
	return !IsPrivateAddress(parts->host);
}

QString StartFailureReason(const QByteArray &output) {
	const auto text = QString::fromUtf8(output);
	const auto marker = QStringLiteral("Failed to start:");
	const auto at = text.lastIndexOf(marker);
	if (at < 0) {
		return QString();
	}
	auto line = text.mid(at + marker.size()).section(QChar('\n'), 0, 0);
	const auto separator = line.lastIndexOf(QStringLiteral(" > "));
	auto reason = ((separator >= 0) ? line.mid(separator + 3) : line).trimmed();
	// Host names and addresses: never logged.
	static const auto ipv6 = QRegularExpression(
		QStringLiteral("\\[?[0-9A-Fa-f]{0,4}(:[0-9A-Fa-f]{0,4}){2,7}\\]?"));
	static const auto names = QRegularExpression(
		QStringLiteral("[A-Za-z0-9_-]+(\\.[A-Za-z0-9_-]+)+"));
	reason.replace(ipv6, QStringLiteral("*"));
	reason.replace(names, QStringLiteral("*"));
	return reason.left(300);
}

QJsonObject Outbound(const QString &link) {
	const auto parts = Split(link);
	if (!parts || NeedsEncryption(link)) {
		return QJsonObject();
	}
	const auto stream = StreamSettings(*parts);
	if (!stream) {
		return QJsonObject();
	}
	const auto value = [&](const char *key) {
		const auto i = parts->query.find(L(key));
		return (i != end(parts->query)) ? i->second : QString();
	};
	auto settings = QJsonObject();
	if (parts->protocol == L(kVless) || parts->protocol == L(kVmess)) {
		auto user = QJsonObject{ { QStringLiteral("id"), parts->id } };
		if (parts->protocol == L(kVless)) {
			user.insert(
				QStringLiteral("encryption"),
				NonEmpty(value("encryption"), QStringLiteral("none")));
			if (!value("flow").isEmpty()) {
				user.insert(QStringLiteral("flow"), value("flow"));
			}
		} else {
			user.insert(
				QStringLiteral("security"),
				NonEmpty(value("encryption"), QStringLiteral("auto")));
			user.insert(QStringLiteral("alterId"), 0);
		}
		settings.insert(QStringLiteral("vnext"), QJsonArray{ QJsonObject{
			{ QStringLiteral("address"), parts->host },
			{ QStringLiteral("port"), parts->port },
			{ QStringLiteral("users"), QJsonArray{ user } },
		} });
	} else if (parts->protocol == L(kTrojan)) {
		settings.insert(QStringLiteral("servers"), QJsonArray{ QJsonObject{
			{ QStringLiteral("address"), parts->host },
			{ QStringLiteral("port"), parts->port },
			{ QStringLiteral("password"), parts->id },
		} });
	} else if (parts->protocol == L(kShadowsocks)) {
		const auto userInfo = Base64Text(parts->id);
		const auto colon = userInfo ? userInfo->indexOf(QChar(':')) : -1;
		if (colon <= 0) {
			return QJsonObject();
		}
		settings.insert(QStringLiteral("servers"), QJsonArray{ QJsonObject{
			{ QStringLiteral("address"), parts->host },
			{ QStringLiteral("port"), parts->port },
			{ QStringLiteral("method"), userInfo->left(colon) },
			{ QStringLiteral("password"), userInfo->mid(colon + 1) },
		} });
	} else {
		return QJsonObject();
	}
	return QJsonObject{
		{ QStringLiteral("protocol"), (parts->protocol == L(kShadowsocks))
			? QStringLiteral("shadowsocks")
			: parts->protocol },
		{ QStringLiteral("settings"), settings },
		{ QStringLiteral("streamSettings"), *stream },
	};
}

QByteArray BuildConfig(
		const std::vector<Run> &runs,
		const QString &user,
		const QString &password) {
	auto inbounds = QJsonArray();
	auto outbounds = QJsonArray{ QJsonObject{
		{ QStringLiteral("tag"), QStringLiteral("block") },
		{ QStringLiteral("protocol"), QStringLiteral("blackhole") },
	} };
	auto rules = QJsonArray();
	for (auto i = 0; i != int(runs.size()); ++i) {
		const auto in = QStringLiteral("in-") + QString::number(i);
		const auto out = QStringLiteral("out-") + QString::number(i);
		inbounds.push_back(QJsonObject{
			{ QStringLiteral("tag"), in },
			{ QStringLiteral("listen"), QStringLiteral("127.0.0.1") },
			{ QStringLiteral("port"), runs[i].localPort },
			{ QStringLiteral("protocol"), QStringLiteral("socks") },
			{ QStringLiteral("settings"), QJsonObject{
				{ QStringLiteral("auth"), QStringLiteral("password") },
				{ QStringLiteral("accounts"), QJsonArray{ QJsonObject{
					{ QStringLiteral("user"), user },
					{ QStringLiteral("pass"), password },
				} } },
				{ QStringLiteral("udp"), true },
				{ QStringLiteral("ip"), QStringLiteral("127.0.0.1") },
			} },
		});
		auto outbound = runs[i].outbound;
		outbound.insert(QStringLiteral("tag"), out);
		outbound.remove(QStringLiteral("sendThrough"));
		outbounds.push_back(outbound);
		rules.push_back(QJsonObject{
			{ QStringLiteral("type"), QStringLiteral("field") },
			{ QStringLiteral("inboundTag"), QJsonArray{ in } },
			{ QStringLiteral("outboundTag"), out },
		});
	}
	const auto config = QJsonObject{
		{ QStringLiteral("log"), QJsonObject{
			{ QStringLiteral("loglevel"), QStringLiteral("warning") },
			{ QStringLiteral("access"), QStringLiteral("none") },
		} },
		{ QStringLiteral("inbounds"), inbounds },
		{ QStringLiteral("outbounds"), outbounds },
		{ QStringLiteral("routing"), QJsonObject{
			{ QStringLiteral("domainStrategy"), QStringLiteral("AsIs") },
			{ QStringLiteral("rules"), rules },
		} },
	};
	return QJsonDocument(config).toJson(QJsonDocument::Compact);
}

std::vector<int> PickPorts(int base, const std::set<int> &used, int n) {
	auto result = std::vector<int>();
	auto port = (base < 20000 || base > 59999) ? 20000 : base;
	for (auto tries = 0; int(result.size()) < n && tries < 40000; ++tries) {
		if (!used.contains(port)
			&& std::find(begin(result), end(result), port) == end(result)) {
			result.push_back(port);
		}
		port = (port >= 59999) ? 20000 : (port + 1);
	}
	return result;
}

std::optional<QString> Base64Text(const QString &text) {
	auto clean = text.trimmed();
	clean.remove(QRegularExpression(QStringLiteral("\\s")));
	clean.replace(QChar('-'), QChar('+')).replace(QChar('_'), QChar('/'));
	const auto equal = clean.indexOf(QChar('='));
	if (equal >= 0) {
		clean = clean.left(equal);
	}
	static const auto valid = QRegularExpression(QStringLiteral("^[A-Za-z0-9+/]+$"));
	if (clean.isEmpty()
		|| !valid.match(clean).hasMatch()
		|| clean.size() % 4 == 1) {
		return std::nullopt;
	}
	const auto alphabet = QByteArray(kBase64Alphabet);
	auto out = QByteArray();
	auto buffer = 0;
	auto bits = 0;
	for (const auto c : clean) {
		buffer = ((buffer << 6) | alphabet.indexOf(char(c.unicode())))
			& 0xFFFFFF;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(char((buffer >> bits) & 0xFF));
		}
	}
	return QString::fromUtf8(out);
}

QString Base64UrlNoPad(const QByteArray &data) {
	auto result = QString::fromLatin1(data.toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
	return result;
}

} // namespace Luckgram::Xray
