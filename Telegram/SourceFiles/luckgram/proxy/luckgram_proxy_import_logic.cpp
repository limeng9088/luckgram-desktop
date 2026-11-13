/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/proxy/luckgram_proxy_import_logic.h"

#include <QtCore/QStringList>
#include <QtCore/QUrl>

#include <map>

namespace Luckgram::ProxyImport {
namespace {

using Map = std::map<QString, QString>;

[[nodiscard]] QString L(const char *text) {
	return QString::fromLatin1(text);
}

[[nodiscard]] int LeadingSpaces(const QString &text) {
	auto result = 0;
	while (result < text.size() && text[result] == QChar(' ')) {
		++result;
	}
	return result;
}

[[nodiscard]] int FirstOf(const QString &text, const QString &chars) {
	auto result = int(text.size());
	for (const auto c : chars) {
		const auto i = int(text.indexOf(c));
		if (i >= 0 && i < result) {
			result = i;
		}
	}
	return result;
}

[[nodiscard]] QString Decode(const QString &text) {
	// URLDecoder with '+' kept as is.
	return QUrl::fromPercentEncoding(text.toUtf8());
}

[[nodiscard]] int ParsePort(const QString &text) {
	auto ok = false;
	const auto port = text.trimmed().toInt(&ok);
	return (ok && port > 0 && port < 65536) ? port : -1;
}

[[nodiscard]] int ParsePort(const Map &map, const char *key) {
	const auto i = map.find(L(key));
	return (i == map.end()) ? -1 : ParsePort(i->second);
}

[[nodiscard]] QString Get(const Map &map, const char *key) {
	const auto i = map.find(L(key));
	return (i == map.end()) ? QString() : i->second;
}

[[nodiscard]] bool Has(const Map &map, const char *key) {
	return map.find(L(key)) != map.end();
}

[[nodiscard]] bool IsTelegramLink(const QString &lower) {
	const auto host = HostOf(lower);
	return !host.isEmpty()
		&& (host == L("t.me")
			|| host == L("telegram.me")
			|| host == L("telegram.dog")
			|| host.endsWith(L(".t.me")));
}

[[nodiscard]] Map QueryParams(QString query) {
	auto result = Map();
	const auto hash = query.indexOf(QChar('#'));
	if (hash >= 0) {
		query = query.left(hash);
	}
	for (const auto &part : query.split(QChar('&'))) {
		if (part.isEmpty()) {
			continue;
		}
		const auto eq = part.indexOf(QChar('='));
		const auto key = Decode(eq >= 0 ? part.left(eq) : part).toLower();
		const auto value = (eq >= 0) ? Decode(part.mid(eq + 1)) : QString();
		result.emplace(key, value); // The first one wins.
	}
	return result;
}

[[nodiscard]] std::optional<Entry> ParseSocksUrl(const QString &rest) {
	auto auth = rest.left(FirstOf(rest, L("/?#")));
	auto user = QString();
	auto pass = QString();
	const auto at = auth.lastIndexOf(QChar('@'));
	if (at >= 0) {
		const auto credentials = auth.left(at);
		auth = auth.mid(at + 1);
		const auto colon = credentials.indexOf(QChar(':'));
		user = Decode(colon >= 0 ? credentials.left(colon) : credentials);
		pass = (colon >= 0) ? Decode(credentials.mid(colon + 1)) : QString();
	}
	auto host = QString();
	auto port = QString();
	if (auth.startsWith(QChar('['))) {
		const auto close = auth.indexOf(QChar(']'));
		if (close < 0) {
			return std::nullopt;
		}
		host = auth.mid(1, close - 1);
		if (auth.size() > close + 2 && auth[close + 1] == QChar(':')) {
			port = auth.mid(close + 2);
		}
	} else {
		const auto colon = auth.lastIndexOf(QChar(':'));
		if (colon < 0) {
			return std::nullopt;
		}
		host = auth.left(colon);
		port = auth.mid(colon + 1);
	}
	const auto number = ParsePort(port);
	if (host.isEmpty() || number <= 0) {
		return std::nullopt;
	}
	return MakeEntry(L(kTypeSocks5), host, number, user, pass);
}

[[nodiscard]] std::optional<QString> DecodeBase64Text(const QString &text) {
	if (text.isEmpty() || text.contains(L("://"))) {
		return std::nullopt;
	}
	auto clean = QString();
	for (const auto c : text) {
		if (!c.isSpace()) {
			clean.append(c);
		}
	}
	if (clean.isEmpty()) {
		return std::nullopt;
	}
	for (const auto c : clean) {
		const auto ok = (c >= QChar('A') && c <= QChar('Z'))
			|| (c >= QChar('a') && c <= QChar('z'))
			|| (c >= QChar('0') && c <= QChar('9'))
			|| c == QChar('+') || c == QChar('/') || c == QChar('=')
			|| c == QChar('_') || c == QChar('-');
		if (!ok) {
			return std::nullopt;
		}
	}
	clean.replace(QChar('-'), QChar('+')).replace(QChar('_'), QChar('/'));
	while (clean.size() % 4) {
		clean.append(QChar('='));
	}
	const auto bytes = Base64(clean);
	if (!bytes) {
		return std::nullopt;
	}
	return QString::fromUtf8(*bytes);
}

void AppendUnique(
		std::vector<Entry> &list,
		std::set<QString> &keys,
		Entry entry) {
	if (keys.emplace(entry.key()).second) {
		list.push_back(std::move(entry));
	}
}

[[nodiscard]] Result ParseLinkList(const QString &text) {
	auto result = Result();
	auto keys = std::set<QString>();
	for (const auto &line : text.split(QChar('\n'))) {
		const auto l = line.trimmed();
		if (l.isEmpty()
			|| l.startsWith(QChar('#'))
			|| (!l.contains(L("://")) && !l.startsWith(L("tg:")))) {
			continue;
		}
		if (auto entry = ParseLink(l)) {
			AppendUnique(result.proxies, keys, std::move(*entry));
		} else {
			++result.skipped;
		}
	}
	return result;
}

// ---- Clash / mihomo YAML (only what proxies use).

[[nodiscard]] QString Unquote(const QString &value) {
	const auto size = value.size();
	if (size >= 2 && value[0] == QChar('"') && value[size - 1] == QChar('"')) {
		return value.mid(1, size - 2)
			.replace(L("\\\""), L("\""))
			.replace(L("\\\\"), L("\\"));
	}
	if (size >= 2
		&& value[0] == QChar('\'')
		&& value[size - 1] == QChar('\'')) {
		return value.mid(1, size - 2).replace(L("''"), L("'"));
	}
	return value;
}

[[nodiscard]] QString StripComment(const QString &value) {
	if (value.startsWith(QChar('"')) || value.startsWith(QChar('\''))) {
		return value;
	}
	const auto hash = value.indexOf(L(" #"));
	return (hash >= 0) ? value.left(hash).trimmed() : value;
}

// Index of the ':' that ends a key (outside quotes, then a space or end).
[[nodiscard]] int KeyColon(const QString &text) {
	auto quote = QChar();
	for (auto i = 0; i != text.size(); ++i) {
		const auto c = text[i];
		if (!quote.isNull()) {
			if (c == quote) {
				quote = QChar();
			}
		} else if (c == QChar('"') || c == QChar('\'')) {
			quote = c;
		} else if (c == QChar(':')
			&& (i + 1 == text.size() || text[i + 1] == QChar(' '))) {
			return i;
		}
	}
	return -1;
}

void ParseFlowMap(const QString &text, Map &into, const QString &prefix);

// key: value into `into` as prefix + key; a flow map value is flattened.
// Returns the prefix for the keys below when the value is empty (a block
// map opens), else an empty string.
QString PutPair(const QString &text, Map &into, const QString &prefix) {
	const auto colon = KeyColon(text);
	if (colon <= 0) {
		return QString();
	}
	const auto key = prefix + Unquote(text.left(colon).trimmed());
	const auto value = text.mid(colon + 1).trimmed();
	if (value.isEmpty() || value.startsWith(QChar('#'))) {
		return key + QChar('.');
	} else if (value.startsWith(QChar('{'))) {
		ParseFlowMap(value, into, key + QChar('.'));
		return QString();
	} else if (value.startsWith(QChar('['))) {
		return QString(); // Lists are not used.
	}
	into[key] = Unquote(StripComment(value));
	return QString();
}

// {a: 1, b: "x, y", c: {d: e}} -> a, b, c.d (with prefix in front).
void ParseFlowMap(const QString &text, Map &into, const QString &prefix) {
	const auto start = text.indexOf(QChar('{'));
	auto depth = 0;
	auto quote = QChar();
	auto part = QString();
	for (auto i = start + 1; i < text.size(); ++i) {
		const auto c = text[i];
		if (!quote.isNull()) {
			part.append(c);
			if (c == quote) {
				quote = QChar();
			}
			continue;
		}
		if (c == QChar('"') || c == QChar('\'')) {
			quote = c;
			part.append(c);
		} else if (c == QChar('{') || c == QChar('[')) {
			++depth;
			part.append(c);
		} else if (c == QChar('}') || c == QChar(']')) {
			if (!depth) {
				PutPair(part.trimmed(), into, prefix);
				return;
			}
			--depth;
			part.append(c);
		} else if (c == QChar(',') && !depth) {
			PutPair(part.trimmed(), into, prefix);
			part.clear();
		} else {
			part.append(c);
		}
	}
	PutPair(part.trimmed(), into, prefix);
}

[[nodiscard]] std::optional<Entry> FromClash(const Map &map) {
	if (!Has(map, "type")) {
		return std::nullopt;
	}
	const auto type = Get(map, "type");
	if (type.compare(L("socks5"), Qt::CaseInsensitive) != 0) {
		const auto link = Xray::FromClash(map);
		const auto node = link.isEmpty() ? std::nullopt : Xray::Parse(link);
		return node ? std::make_optional(MakeXrayEntry(*node)) : std::nullopt;
	}
	if (Get(map, "tls").compare(L("true"), Qt::CaseInsensitive) == 0) {
		return std::nullopt; // Telegram has no SOCKS5 over TLS.
	}
	const auto server = Get(map, "server");
	const auto port = ParsePort(map, "port");
	if (server.isEmpty() || port <= 0) {
		return std::nullopt;
	}
	return MakeEntry(
		L(kTypeSocks5),
		server,
		port,
		Get(map, "username"),
		Get(map, "password"));
}

[[nodiscard]] Result ParseClash(const QString &text) {
	struct Open {
		int indent = 0;
		QString prefix;
	};
	auto items = std::vector<Map>();
	auto inProxies = false;
	auto current = -1; // Index in items of the block item being read.
	auto itemIndent = -1;
	auto keyIndent = -1;
	auto open = std::vector<Open>(); // Nested maps of the current item.
	for (const auto &line : text.split(QChar('\n'))) {
		const auto t = line.trimmed();
		if (t.isEmpty() || t.startsWith(QChar('#'))) {
			continue;
		}
		const auto indent = LeadingSpaces(line);
		if (!indent && !t.startsWith(QChar('-'))) {
			inProxies = t.startsWith(L("proxies:"));
			current = -1;
			itemIndent = -1;
			continue;
		} else if (!inProxies) {
			continue;
		}
		if (t.startsWith(L("- ")) || t == L("-")) {
			if (itemIndent < 0) {
				itemIndent = indent;
			}
			if (indent == itemIndent) {
				const auto after = t.mid(1);
				const auto rest = after.trimmed();
				keyIndent = indent + 1 + LeadingSpaces(after);
				items.emplace_back();
				current = int(items.size()) - 1;
				open.clear();
				if (rest.startsWith(QChar('{'))) {
					ParseFlowMap(rest, items.back(), QString());
					current = -1;
				} else if (!rest.isEmpty()) {
					const auto opened = PutPair(rest, items.back(), QString());
					if (!opened.isEmpty()) {
						open.push_back({ keyIndent, opened });
					}
				}
				continue;
			}
		}
		// A key of the current block item; nested maps become dotted keys.
		if (current < 0 || indent < keyIndent || t.startsWith(QChar('-'))) {
			continue;
		}
		while (!open.empty() && open.back().indent >= indent) {
			open.pop_back();
		}
		if (indent > keyIndent && open.empty()) {
			continue; // Under a list or a scalar.
		}
		const auto prefix = open.empty() ? QString() : open.back().prefix;
		const auto opened = PutPair(t, items[current], prefix);
		if (!opened.isEmpty()) {
			open.push_back({ indent, opened });
		}
	}
	auto result = Result();
	auto keys = std::set<QString>();
	for (const auto &item : items) {
		if (auto entry = FromClash(item)) {
			AppendUnique(result.proxies, keys, std::move(*entry));
		} else {
			++result.skipped;
		}
	}
	return result;
}

[[nodiscard]] bool HasProxiesSection(const QString &text) {
	for (const auto &line : text.split(QChar('\n'))) {
		if (line.startsWith(L("proxies:"))) {
			return true;
		}
	}
	return false;
}

} // namespace

QString Entry::key() const {
	if (type == L(kTypeXray) && node) {
		return node->key();
	}
	return type
		+ QChar('|') + host.toLower()
		+ QChar('|') + QString::number(port)
		+ QChar('|') + user
		+ QChar('|') + password
		+ QChar('|') + secret;
}

Entry MakeEntry(
		const QString &type,
		const QString &host,
		int port,
		const QString &user,
		const QString &password,
		const QString &secret) {
	return Entry{
		.type = type,
		.host = host.trimmed(),
		.port = port,
		.user = user,
		.password = password,
		.secret = secret,
	};
}

Entry MakeXrayEntry(const Xray::Node &node) {
	auto result = MakeEntry(L(kTypeXray), node.host, node.port);
	result.node = node;
	return result;
}

Kind KindOf(const QString &raw) {
	const auto text = raw.trimmed();
	if (text.isEmpty()
		|| text.contains(QChar('\n'))
		|| text.contains(QChar(' '))) {
		return Kind::Unknown;
	} else if (ParseLink(text)) {
		return Kind::Proxy;
	}
	const auto lower = text.toLower();
	if ((lower.startsWith(L("https://")) || lower.startsWith(L("http://")))
		&& !IsTelegramLink(lower)
		&& !HostOf(text).isEmpty()) {
		return Kind::Subscription;
	}
	return Kind::Unknown;
}

QString HostOf(const QString &url) {
	const auto i = url.indexOf(L("://"));
	if (i < 0) {
		return QString();
	}
	const auto rest = url.mid(i + 3);
	auto auth = rest.left(FirstOf(rest, L("/?#")));
	const auto at = auth.lastIndexOf(QChar('@'));
	if (at >= 0) {
		auth = auth.mid(at + 1);
	}
	if (auth.startsWith(QChar('['))) {
		const auto close = auth.indexOf(QChar(']'));
		return (close > 1) ? auth.mid(1, close - 1) : QString();
	}
	const auto colon = auth.lastIndexOf(QChar(':'));
	if (colon >= 0) {
		auth = auth.left(colon);
	}
	return auth.toLower();
}

std::optional<Entry> ParseLink(const QString &raw) {
	const auto text = raw.trimmed();
	const auto lower = text.toLower();
	if (Xray::LooksLikeLink(text)) {
		const auto node = Xray::Parse(text);
		return node ? std::make_optional(MakeXrayEntry(*node)) : std::nullopt;
	}
	for (const auto prefix : { "socks5h://", "socks5://", "socks://" }) {
		if (lower.startsWith(L(prefix))) {
			return ParseSocksUrl(text.mid(int(qstrlen(prefix))));
		}
	}
	auto path = QString();
	auto query = QString();
	auto found = false;
	if (lower.startsWith(L("tg:"))) {
		auto rest = text.mid(3);
		if (rest.startsWith(L("//"))) {
			rest = rest.mid(2);
		}
		const auto q = rest.indexOf(QChar('?'));
		path = ((q >= 0) ? rest.left(q) : rest).toLower();
		query = (q >= 0) ? rest.mid(q + 1) : QString();
		found = true;
	} else if (lower.startsWith(L("https://"))
		|| lower.startsWith(L("http://"))
		|| lower.startsWith(L("t.me/"))
		|| lower.startsWith(L("telegram.me/"))) {
		const auto url = lower.startsWith(L("http"))
			? text
			: (L("https://") + text);
		if (!IsTelegramLink(url.toLower())) {
			return std::nullopt;
		}
		auto rest = url.mid(url.indexOf(L("://")) + 3);
		const auto slash = rest.indexOf(QChar('/'));
		if (slash < 0) {
			return std::nullopt;
		}
		rest = rest.mid(slash + 1);
		const auto q = rest.indexOf(QChar('?'));
		path = ((q >= 0) ? rest.left(q) : rest).toLower();
		query = (q >= 0) ? rest.mid(q + 1) : QString();
		found = true;
	}
	if (!found) {
		return std::nullopt;
	}
	if (path.endsWith(QChar('/'))) {
		path.chop(1);
	}
	if (path != L("socks") && path != L("proxy")) {
		return std::nullopt;
	}
	const auto params = QueryParams(query);
	const auto server = Get(params, "server");
	const auto port = ParsePort(params, "port");
	if (server.isEmpty() || port <= 0) {
		return std::nullopt;
	}
	if (path == L("socks")) {
		return MakeEntry(
			L(kTypeSocks5),
			server,
			port,
			Get(params, "user"),
			Get(params, "pass"));
	}
	const auto secret = Get(params, "secret");
	if (secret.isEmpty()) {
		return std::nullopt;
	}
	return MakeEntry(L(kTypeMtproto), server, port, {}, {}, secret);
}

Result ParseSubscription(const QString &body) {
	auto text = body;
	text.replace(L("\r\n"), L("\n")).replace(QChar('\r'), QChar('\n'));
	if (text.startsWith(QChar(0xFEFF))) {
		text = text.mid(1);
	}
	if (HasProxiesSection(text)) {
		return ParseClash(text);
	}
	const auto decoded = DecodeBase64Text(text.trimmed());
	return ParseLinkList(decoded ? *decoded : text);
}

std::optional<QByteArray> Base64(const QString &text) {
	if (text.size() % 4) {
		return std::nullopt;
	}
	const auto alphabet = L(
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
	auto result = QByteArray();
	result.reserve(int(text.size()) * 3 / 4);
	for (auto i = 0; i < text.size(); i += 4) {
		int v[4] = { 0, 0, 0, 0 };
		auto pad = 0;
		for (auto j = 0; j != 4; ++j) {
			const auto c = text[i + j];
			if (c == QChar('=')) {
				if (i + 4 != text.size() || j < 2) {
					return std::nullopt;
				}
				++pad;
			} else {
				if (pad > 0) {
					return std::nullopt;
				}
				v[j] = int(alphabet.indexOf(c));
				if (v[j] < 0) {
					return std::nullopt;
				}
			}
		}
		const auto n = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
		result.append(char((n >> 16) & 0xFF));
		if (pad < 2) {
			result.append(char((n >> 8) & 0xFF));
		}
		if (pad < 1) {
			result.append(char(n & 0xFF));
		}
	}
	return result;
}

Diff MakeDiff(
		const std::set<QString> &previouslyImported,
		const std::vector<Entry> &now,
		const std::set<QString> &existing) {
	auto result = Diff();
	auto nowKeys = std::set<QString>();
	for (const auto &entry : now) {
		const auto key = entry.key();
		nowKeys.emplace(key);
		if (!existing.contains(key)) {
			result.add.push_back(entry);
		}
	}
	for (const auto &key : previouslyImported) {
		if (!nowKeys.contains(key)) {
			result.remove.push_back(key);
		}
	}
	return result;
}

std::set<QString> Owned(
		const std::set<QString> &previouslyOwned,
		const std::vector<Entry> &now,
		const Diff &diff) {
	auto added = std::set<QString>();
	for (const auto &entry : diff.add) {
		added.emplace(entry.key());
	}
	auto result = std::set<QString>();
	for (const auto &entry : now) {
		const auto key = entry.key();
		if (previouslyOwned.contains(key) || added.contains(key)) {
			result.emplace(key);
		}
	}
	return result;
}

} // namespace Luckgram::ProxyImport
