/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/proxy/luckgram_proxy_exit_logic.h"
#include "luckgram/proxy/luckgram_proxy_import_logic.h"

// D3: the cases of mobile ProxyImportLogicTest (specs#118) and ExitInfoTest
// (specs#117), one test per mobile test method.

namespace {

using namespace Luckgram::ProxyImport;
namespace Exit = Luckgram::ProxyExit;

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] QString B64(const QString &text) {
	return QString::fromLatin1(text.toUtf8().toBase64());
}

[[nodiscard]] Entry Socks(const char *host) {
	return MakeEntry(U(kTypeSocks5), U(host), 1);
}

[[nodiscard]] std::set<QString> Keys(std::initializer_list<Entry> list) {
	auto result = std::set<QString>();
	for (const auto &entry : list) {
		result.emplace(entry.key());
	}
	return result;
}

const auto kClash = QStringList{
	U("mixed-port: 7890"),
	U("proxies:"),
	U("  - {name: 'SG·vless', type: vless, server: agg.example.com, port: 443, uuid: x, network: ws, ws-opts: {path: /a, headers: {Host: agg.example.com}}}"),
	U("  - {name: \"s5, flow\", type: socks5, server: 198.51.100.24, port: 1080, username: u1, password: 'p, 1'}"),
	U("  - name: s5-block"),
	U("    type: socks5"),
	U("    server: '198.51.100.77'"),
	U("    port: 1081"),
	U("    username: \"u2\""),
	U("    password: p2 # comment"),
	U("    udp: true"),
	U("  - name: s5-tls"),
	U("    type: socks5"),
	U("    server: 192.0.2.1"),
	U("    port: 443"),
	U("    tls: true"),
	U("  - name: ss"),
	U("    type: ss"),
	U("    server: 192.0.2.2"),
	U("    port: 8388"),
	U("    plugin-opts:"),
	U("      mode: websocket"),
	U("      type: socks5"),
	U("proxy-groups:"),
	U("  - {name: g, type: select, proxies: [s5-block]}"),
	U("rules:"),
	U("  - MATCH,g"),
}.join(QChar('\n'));

} // namespace

LUCKGRAM_TEST(ProxyImportKinds) {
	LG_CHECK(KindOf(U("tg://socks?server=1.2.3.4&port=1080")) == Kind::Proxy);
	LG_CHECK(KindOf(U(" https://t.me/proxy?server=a.b&port=443&secret=ee00 "))
		== Kind::Proxy);
	LG_CHECK(KindOf(U("socks5://u:p@1.2.3.4:1080")) == Kind::Proxy);
	LG_CHECK(KindOf(U("https://agg.example.com/clash/123"))
		== Kind::Subscription);
	LG_CHECK(KindOf(U("http://10.0.0.1:8080/sub")) == Kind::Subscription);
	LG_CHECK(KindOf(U("https://t.me/durov")) == Kind::Unknown);
	LG_CHECK(KindOf(U("tg://socks?server=1.2.3.4")) == Kind::Unknown);
	LG_CHECK(KindOf(U("hello world")) == Kind::Unknown);
	LG_CHECK(KindOf(QString()) == Kind::Unknown);
}

LUCKGRAM_TEST(ProxyImportTgLinks) {
	const auto e = ParseLink(U(
		"tg://socks?server=38.182.96.3&port=20201&user=lg%20sg&pass=a%2Bb%26c"));
	LG_CHECK(e.has_value());
	LG_EQUAL(e->type, U(kTypeSocks5));
	LG_EQUAL(e->host, U("38.182.96.3"));
	LG_EQUAL(e->port, 20201);
	LG_EQUAL(e->user, U("lg sg"));
	LG_EQUAL(e->password, U("a+b&c"));
	const auto m = ParseLink(U(
		"https://t.me/proxy?server=mtp.example.org&port=443&secret=eeabcdef"));
	LG_CHECK(m.has_value());
	LG_EQUAL(m->type, U(kTypeMtproto));
	LG_EQUAL(m->secret, U("eeabcdef"));
	const auto t = ParseLink(U("telegram.me/socks?server=example.com&port=1"));
	LG_CHECK(t.has_value());
	LG_EQUAL(t->host, U("example.com"));
	LG_CHECK(!ParseLink(U("tg://proxy?server=a&port=1"))); // no secret
	LG_CHECK(!ParseLink(U("tg://socks?server=a&port=99999")));
	LG_CHECK(!ParseLink(U("https://example.com/socks?server=a&port=1")));
}

LUCKGRAM_TEST(ProxyImportSocksUrls) {
	const auto e = ParseLink(U("socks5://us%40er:p%3Ass@proxy.example.com:1080/"));
	LG_CHECK(e.has_value());
	LG_EQUAL(e->user, U("us@er"));
	LG_EQUAL(e->password, U("p:ss"));
	LG_EQUAL(e->host, U("proxy.example.com"));
	LG_EQUAL(e->port, 1080);
	const auto v6 = ParseLink(U("socks5h://[2001:db8::1]:1080"));
	LG_CHECK(v6.has_value());
	LG_EQUAL(v6->host, U("2001:db8::1"));
	LG_EQUAL(v6->user, QString());
	const auto plain = ParseLink(U("socks://h:1081"));
	LG_CHECK(plain.has_value());
	LG_EQUAL(plain->port, 1081);
	LG_CHECK(!ParseLink(U("socks5://host-without-port")));
}

LUCKGRAM_TEST(ProxyImportClashSocks5AndXray) {
	const auto r = ParseSubscription(kClash);
	LG_EQUAL(int(r.proxies.size()), 3);
	if (r.proxies.size() != 3) {
		return;
	}
	const auto &v = r.proxies[0];
	LG_EQUAL(v.type, U(kTypeXray));
	LG_CHECK(v.node.has_value());
	LG_EQUAL(v.node->protocol, U("vless"));
	LG_EQUAL(v.node->name, U("SG·vless"));
	LG_EQUAL(v.host, U("agg.example.com"));
	LG_CHECK(v.node->link.contains(U("type=ws")));
	LG_CHECK(v.node->link.contains(U("path=%2Fa")));
	LG_CHECK(v.node->link.contains(U("host=agg.example.com")));
	LG_EQUAL(r.proxies[1].host, U("198.51.100.24"));
	LG_EQUAL(r.proxies[1].password, U("p, 1"));
	LG_EQUAL(r.proxies[2].host, U("198.51.100.77"));
	LG_EQUAL(r.proxies[2].port, 1081);
	LG_EQUAL(r.proxies[2].user, U("u2"));
	LG_EQUAL(r.proxies[2].password, U("p2"));
	// socks5 over TLS, ss without cipher (its nested "type: socks5" ignored).
	LG_EQUAL(r.skipped, 2);
}

LUCKGRAM_TEST(ProxyImportVlessWithoutUuidSkipped) {
	const auto r = ParseSubscription(U(
		"proxies:\n"
		"- {name: a, type: vless, server: x, port: 443}\n"
		"- {name: b, type: vless, server: y, port: 443}\n"));
	LG_EQUAL(int(r.proxies.size()), 0);
	LG_EQUAL(r.skipped, 2);
}

LUCKGRAM_TEST(ProxyImportBase64LinkList) {
	const auto list = U(
		"tg://socks?server=198.51.100.1&port=1080\n"
		"vless://abc@x:443\n"
		"socks5://u:p@198.51.100.2:1080\n"
		"\n"
		"# c\n"
		"socks5://u:p@198.51.100.2:1080\n");
	const auto b64 = B64(list);
	const auto r = ParseSubscription(b64);
	LG_EQUAL(int(r.proxies.size()), 3); // tg, vless, socks5 (duplicate dropped)
	LG_EQUAL(r.skipped, 0);
	LG_EQUAL(int(ParseSubscription(list).proxies.size()), 3);
	auto noPad = b64;
	noPad.remove(QChar('=')).replace(QChar('+'), QChar('-'))
		.replace(QChar('/'), QChar('_'));
	LG_EQUAL(int(ParseSubscription(noPad).proxies.size()), 3);
}

LUCKGRAM_TEST(ProxyImportBase64Decoder) {
	for (const auto s : { "", "f", "fo", "foo", "foob", "fooba", "foobar" }) {
		const auto decoded = Base64(B64(U(s)));
		LG_CHECK(decoded.has_value());
		LG_EQUAL(QString::fromUtf8(decoded.value_or(QByteArray())), U(s));
	}
	LG_CHECK(!Base64(U("abc")));
	LG_CHECK(!Base64(U("a=bc")));
}

LUCKGRAM_TEST(ProxyImportRefreshDiff) {
	const auto a = Socks("198.51.100.1");
	const auto b = Socks("198.51.100.2");
	const auto c = Socks("198.51.100.3");
	const auto now = std::vector<Entry>{ b, c };
	const auto d = MakeDiff(Keys({ a, b }), now, Keys({ a, b }));
	LG_EQUAL(int(d.add.size()), 1);
	LG_CHECK(!d.add.empty() && d.add[0].key() == c.key());
	LG_EQUAL(int(d.remove.size()), 1);
	LG_CHECK(!d.remove.empty() && d.remove[0] == a.key());
	// c already added by hand: not added again.
	const auto d2 = MakeDiff({}, now, Keys({ c }));
	LG_EQUAL(int(d2.add.size()), 1);
	LG_CHECK(!d2.add.empty() && d2.add[0].key() == b.key());
	LG_CHECK(d2.remove.empty());
}

LUCKGRAM_TEST(ProxyImportOwnership) {
	const auto a = Socks("198.51.100.1");
	const auto b = Socks("198.51.100.2");
	const auto c = Socks("198.51.100.3");
	// a was added by hand before the first import: listed, but not owned.
	const auto now = std::vector<Entry>{ a, b };
	const auto first = MakeDiff({}, now, Keys({ a }));
	LG_CHECK(Owned({}, now, first) == Keys({ b }));
	// Refresh: b stays owned, c is new, a still not owned.
	const auto now2 = std::vector<Entry>{ a, b, c };
	const auto owned1 = Keys({ b });
	const auto second = MakeDiff(owned1, now2, Keys({ a, b }));
	LG_CHECK(Owned(owned1, now2, second) == Keys({ b, c }));
}

LUCKGRAM_TEST(ProxyImportHosts) {
	LG_EQUAL(HostOf(U("https://agg.example.com/clash/1?x=y")), U("agg.example.com"));
	LG_EQUAL(HostOf(U("https://u:p@A.B:8443/")), U("a.b"));
	LG_EQUAL(HostOf(U("nope")), QString());
}

LUCKGRAM_TEST(ProxyExitParsesWhoami) {
	const auto e = Exit::Parse(
		"{\"ip\":\"45.39.75.124\",\"country\":\"sg\",\"city\":\"Singapore\"}");
	LG_CHECK(e.has_value());
	LG_EQUAL(e->ip, U("45.39.75.124"));
	LG_EQUAL(e->country, U("SG"));
	LG_EQUAL(e->city, U("Singapore"));
}

LUCKGRAM_TEST(ProxyExitRejectsJunk) {
	LG_CHECK(!Exit::Parse(QByteArray()));
	LG_CHECK(!Exit::Parse("not json"));
	LG_CHECK(!Exit::Parse("{\"country\":\"SG\"}"));
	const auto xx = Exit::Parse("{\"ip\":\"1.2.3.4\",\"country\":\"XX\"}");
	LG_CHECK(xx && xx->country.isEmpty());
	const auto t1 = Exit::Parse("{\"ip\":\"1.2.3.4\",\"country\":\"T1\"}");
	LG_CHECK(t1 && t1->country.isEmpty());
	const auto usa = Exit::Parse("{\"ip\":\"1.2.3.4\",\"country\":\"USA\"}");
	LG_CHECK(usa && usa->country.isEmpty());
}

LUCKGRAM_TEST(ProxyExitFlagFromCode) {
	LG_EQUAL(Exit::Flag(U("SG")), U("🇸🇬"));
	LG_EQUAL(Exit::Flag(U("US")), U("🇺🇸"));
	LG_EQUAL(Exit::Flag(QString()), QString());
	LG_EQUAL(Exit::Flag(U("sg")), QString());
}

LUCKGRAM_TEST(ProxyExitLabelInEnglishAndChinese) {
	const auto e = Exit::MakeInfo(U("45.39.75.124"), U("SG"), QString());
	LG_EQUAL(Exit::Label(e, false), U("🇸🇬 Singapore · 45.39.75.124"));
	LG_EQUAL(Exit::Label(e, true), U("🇸🇬 新加坡 · 45.39.75.124"));
	LG_EQUAL(
		Exit::Label(Exit::MakeInfo(U("1.2.3.4"), QString(), QString()), false),
		U("1.2.3.4"));
}

LUCKGRAM_TEST(ProxyExitFreshForTenMinutes) {
	LG_CHECK(Exit::IsFresh(1000, 1000 + Exit::kTtlMs - 1));
	LG_CHECK(!Exit::IsFresh(1000, 1000 + Exit::kTtlMs));
	LG_CHECK(!Exit::IsFresh(0, 5));
	LG_CHECK(!Exit::IsFresh(1000, 500));
}
