/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/xray/luckgram_xray_logic.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

// D12: ported from Android XrayLogicTest; Outbound() is desktop only (the
// phones use libXray's converter).
namespace {

using namespace Luckgram::Xray;

QString S(const char *utf8) {
	return QString::fromUtf8(utf8);
}

const auto kUuid = S("1b0c5e6a-0000-4000-8000-000000000001");

QJsonObject Json(const QByteArray &bytes) {
	return QJsonDocument::fromJson(bytes).object();
}

} // namespace

LUCKGRAM_TEST(XrayVlessLink) {
	const auto n = Parse(S("vless://") + kUuid
		+ S("@edge.example.com:443?encryption=none&security=tls&sni=edge.example.com&type=ws&path=%2Fws#SG%201"));
	LG_CHECK(n.has_value());
	LG_EQUAL(n->protocol, S("vless"));
	LG_EQUAL(n->host, S("edge.example.com"));
	LG_EQUAL(n->port, 443);
	LG_EQUAL(n->name, S("SG 1"));
	LG_CHECK(n->link.startsWith(S("vless://") + kUuid + S("@edge.example.com:443?")));
	LG_CHECK(n->link.endsWith(S("#SG%201")));
	auto renamedLink = n->link;
	renamedLink.replace(S("#SG%201"), S("#other"));
	const auto renamed = Parse(renamedLink);
	LG_CHECK(renamed.has_value());
	LG_EQUAL(renamed->key(), n->key());
}

LUCKGRAM_TEST(XrayIncompleteLinks) {
	LG_CHECK(!Parse(S("vless://edge.example.com:443")));
	LG_CHECK(!Parse(S("vless://") + kUuid + S("@edge.example.com")));
	LG_CHECK(!Parse(S("trojan://@h:443")));
	LG_CHECK(!Parse(S("vless://") + kUuid + S("@h:70000")));
	LG_CHECK(!Parse(S("http://example.com")));
}

LUCKGRAM_TEST(XrayIpv6Host) {
	const auto n = Parse(S("trojan://pw@[2001:db8::1]:443?security=tls#t"));
	LG_CHECK(n.has_value());
	LG_EQUAL(n->host, S("2001:db8::1"));
	LG_CHECK(n->link.contains(S("@[2001:db8::1]:443")));
}

LUCKGRAM_TEST(XrayVmessV2rayNJson) {
	auto o = QJsonObject{
		{ S("v"), S("2") },
		{ S("ps"), S("US 1") },
		{ S("add"), S("v.example.com") },
		{ S("port"), S("8443") },
		{ S("id"), kUuid },
		{ S("aid"), S("0") },
		{ S("net"), S("ws") },
		{ S("host"), S("cdn.example.com") },
		{ S("path"), S("/v") },
		{ S("tls"), S("tls") },
		{ S("sni"), S("cdn.example.com") },
	};
	const auto link = [](const QJsonObject &json) {
		return S("vmess://") + QString::fromLatin1(
			QJsonDocument(json).toJson(QJsonDocument::Compact).toBase64());
	};
	const auto n = Parse(link(o));
	LG_CHECK(n.has_value());
	LG_EQUAL(n->protocol, S("vmess"));
	LG_EQUAL(n->host, S("v.example.com"));
	LG_EQUAL(n->port, 8443);
	LG_EQUAL(n->name, S("US 1"));
	LG_CHECK(n->link.startsWith(S("vmess://") + kUuid
		+ S("@v.example.com:8443?encryption=auto&security=tls&sni=cdn.example.com&type=ws&host=cdn.example.com&path=%2Fv")));
	o.insert(S("aid"), S("64")); // Legacy alterId: not in Xray.
	LG_CHECK(!Parse(link(o)));
}

LUCKGRAM_TEST(XrayShadowsocksForms) {
	const auto userInfo = Base64UrlNoPad(QByteArray("aes-128-gcm:pass"));
	const auto sip002 = Parse(S("ss://") + userInfo + S("@198.51.100.5:8388#SS%201"));
	LG_CHECK(sip002.has_value());
	LG_EQUAL(sip002->protocol, S("ss"));
	LG_EQUAL(sip002->port, 8388);
	LG_EQUAL(sip002->name, S("SS 1"));
	const auto plain = Parse(S("ss://aes-128-gcm:pass@198.51.100.5:8388"));
	LG_CHECK(plain.has_value());
	LG_EQUAL(plain->key(), sip002->key());
	const auto legacy = QString::fromLatin1(
		QByteArray("aes-128-gcm:pass@198.51.100.5:8388").toBase64());
	const auto old = Parse(S("ss://") + legacy + S("#x"));
	LG_CHECK(old.has_value());
	LG_EQUAL(old->key(), sip002->key());
	LG_CHECK(!Parse(S("ss://") + userInfo + S("@198.51.100.5:8388/?plugin=obfs-local%3Bobfs%3Dhttp")));
}

LUCKGRAM_TEST(XrayClashVlessWsTls) {
	const auto link = FromClash({
		{ S("name"), S("SG·hk 1001") },
		{ S("type"), S("vless") },
		{ S("server"), S("agg.example.com") },
		{ S("port"), S("443") },
		{ S("uuid"), kUuid },
		{ S("tls"), S("true") },
		{ S("servername"), S("sni.example.com") },
		{ S("client-fingerprint"), S("chrome") },
		{ S("network"), S("ws") },
		{ S("ws-opts.path"), S("/ray?ed=2048") },
		{ S("ws-opts.headers.Host"), S("sni.example.com") },
		{ S("udp"), S("true") },
	});
	LG_EQUAL(link, S("vless://") + kUuid
		+ S("@agg.example.com:443?encryption=none&security=tls&sni=sni.example.com&fp=chrome&type=ws&host=sni.example.com&path=%2Fray%3Fed%3D2048#SG%C2%B7hk%201001"));
	const auto n = Parse(link);
	LG_CHECK(n.has_value());
	LG_EQUAL(n->name, S("SG·hk 1001"));
	LG_EQUAL(n->link, link);
}

LUCKGRAM_TEST(XrayClashReality) {
	const auto link = FromClash({
		{ S("name"), S("r") },
		{ S("type"), S("vless") },
		{ S("server"), S("r.example.com") },
		{ S("port"), S("443") },
		{ S("uuid"), kUuid },
		{ S("flow"), S("xtls-rprx-vision") },
		{ S("tls"), S("true") },
		{ S("servername"), S("www.example.com") },
		{ S("reality-opts.public-key"), S("PUBKEY") },
		{ S("reality-opts.short-id"), S("ab12") },
		{ S("client-fingerprint"), S("chrome") },
	});
	for (const auto part : { "flow=xtls-rprx-vision", "security=reality", "pbk=PUBKEY", "sid=ab12", "sni=www.example.com", "type=tcp" }) {
		LG_CHECK(link.contains(S(part)));
	}
	// Desktop: the outbound for it.
	const auto outbound = Outbound(link);
	LG_EQUAL(outbound.value(S("protocol")).toString(), S("vless"));
	const auto server = outbound.value(S("settings")).toObject()
		.value(S("vnext")).toArray().at(0).toObject();
	LG_EQUAL(server.value(S("address")).toString(), S("r.example.com"));
	LG_EQUAL(server.value(S("port")).toInt(), 443);
	const auto user = server.value(S("users")).toArray().at(0).toObject();
	LG_EQUAL(user.value(S("id")).toString(), kUuid);
	LG_EQUAL(user.value(S("flow")).toString(), S("xtls-rprx-vision"));
	LG_EQUAL(user.value(S("encryption")).toString(), S("none"));
	const auto stream = outbound.value(S("streamSettings")).toObject();
	LG_EQUAL(stream.value(S("network")).toString(), S("tcp"));
	LG_EQUAL(stream.value(S("security")).toString(), S("reality"));
	const auto reality = stream.value(S("realitySettings")).toObject();
	LG_EQUAL(reality.value(S("publicKey")).toString(), S("PUBKEY"));
	LG_EQUAL(reality.value(S("shortId")).toString(), S("ab12"));
	LG_EQUAL(reality.value(S("serverName")).toString(), S("www.example.com"));
	LG_EQUAL(reality.value(S("fingerprint")).toString(), S("chrome"));
}

LUCKGRAM_TEST(XrayClashOthers) {
	const auto vmess = FromClash({
		{ S("name"), S("m") }, { S("type"), S("vmess") },
		{ S("server"), S("m.example.com") }, { S("port"), S("443") },
		{ S("uuid"), kUuid }, { S("alterId"), S("0") }, { S("cipher"), S("auto") },
		{ S("tls"), S("true") }, { S("network"), S("grpc") },
		{ S("grpc-opts.grpc-service-name"), S("svc") },
	});
	LG_CHECK(vmess.startsWith(S("vmess://") + kUuid
		+ S("@m.example.com:443?encryption=auto&security=tls&type=grpc&serviceName=svc")));
	LG_CHECK(FromClash({
		{ S("type"), S("vmess") }, { S("server"), S("m") }, { S("port"), S("443") },
		{ S("uuid"), kUuid }, { S("alterId"), S("64") },
	}).isEmpty());
	const auto trojan = FromClash({
		{ S("name"), S("t") }, { S("type"), S("trojan") },
		{ S("server"), S("t.example.com") }, { S("port"), S("443") },
		{ S("password"), S("pw") }, { S("sni"), S("t.example.com") },
	});
	LG_EQUAL(trojan, S("trojan://pw@t.example.com:443?security=tls&sni=t.example.com&type=tcp#t"));
	const auto ss = FromClash({
		{ S("name"), S("s") }, { S("type"), S("ss") },
		{ S("server"), S("198.51.100.9") }, { S("port"), S("8388") },
		{ S("cipher"), S("chacha20-ietf-poly1305") }, { S("password"), S("pw") },
	});
	const auto ssNode = Parse(ss);
	LG_CHECK(ssNode.has_value());
	LG_EQUAL(ssNode->protocol, S("ss"));
	LG_CHECK(FromClash({
		{ S("type"), S("ss") }, { S("server"), S("h") }, { S("port"), S("1") },
		{ S("cipher"), S("aes-128-gcm") }, { S("password"), S("p") }, { S("plugin"), S("obfs") },
	}).isEmpty());
	LG_CHECK(FromClash({
		{ S("type"), S("hysteria2") }, { S("server"), S("h") }, { S("port"), S("443") },
		{ S("password"), S("p") },
	}).isEmpty());
	LG_CHECK(FromClash({
		{ S("type"), S("vless") }, { S("server"), S("h") }, { S("port"), S("443") },
		{ S("uuid"), kUuid }, { S("network"), S("kcp") },
	}).isEmpty());
	LG_CHECK(FromClash({
		{ S("type"), S("vless") }, { S("server"), S("h") }, { S("port"), S("443") },
	}).isEmpty());

	// Desktop outbounds.
	const auto vmessStream = Outbound(vmess).value(S("streamSettings")).toObject();
	LG_EQUAL(vmessStream.value(S("network")).toString(), S("grpc"));
	LG_EQUAL(vmessStream.value(S("grpcSettings")).toObject()
		.value(S("serviceName")).toString(), S("svc"));
	const auto trojanOut = Outbound(trojan);
	LG_EQUAL(trojanOut.value(S("protocol")).toString(), S("trojan"));
	LG_EQUAL(trojanOut.value(S("settings")).toObject().value(S("servers"))
		.toArray().at(0).toObject().value(S("password")).toString(), S("pw"));
	const auto ssServer = Outbound(ss).value(S("settings")).toObject()
		.value(S("servers")).toArray().at(0).toObject();
	LG_EQUAL(ssServer.value(S("method")).toString(), S("chacha20-ietf-poly1305"));
	LG_EQUAL(ssServer.value(S("password")).toString(), S("pw"));
	LG_EQUAL(Outbound(ss).value(S("protocol")).toString(), S("shadowsocks"));
	LG_CHECK(Outbound(S("http://example.com")).isEmpty());
}

LUCKGRAM_TEST(XrayOutboundWsTls) {
	const auto outbound = Outbound(S("vless://") + kUuid
		+ S("@edge.example.com:443?encryption=none&security=tls&sni=sni.example.com&fp=chrome&alpn=h2%2Chttp%2F1.1&type=ws&host=cdn.example.com&path=%2Fray%3Fed%3D2048#x"));
	const auto stream = outbound.value(S("streamSettings")).toObject();
	LG_EQUAL(stream.value(S("network")).toString(), S("ws"));
	const auto ws = stream.value(S("wsSettings")).toObject();
	LG_EQUAL(ws.value(S("path")).toString(), S("/ray?ed=2048"));
	LG_EQUAL(ws.value(S("host")).toString(), S("cdn.example.com"));
	const auto tls = stream.value(S("tlsSettings")).toObject();
	LG_EQUAL(tls.value(S("serverName")).toString(), S("sni.example.com"));
	LG_EQUAL(tls.value(S("fingerprint")).toString(), S("chrome"));
	LG_EQUAL(tls.value(S("alpn")).toArray().size(), 2);
	// Reality without a public key can't work.
	LG_CHECK(Outbound(S("vless://") + kUuid + S("@h:443?security=reality&type=tcp")).isEmpty());
}

LUCKGRAM_TEST(XrayConfig) {
	const auto outbound = QJsonObject{
		{ S("protocol"), S("vless") },
		{ S("tag"), S("name from link") },
		{ S("sendThrough"), S("1.2.3.4") },
	};
	const auto config = Json(BuildConfig(
		{ Run{ 31001, outbound }, Run{ 31002, outbound } },
		S("lg"),
		S("secret")));
	const auto in = config.value(S("inbounds")).toArray();
	LG_EQUAL(in.size(), 2);
	const auto first = in.at(0).toObject();
	LG_EQUAL(first.value(S("listen")).toString(), S("127.0.0.1"));
	LG_EQUAL(first.value(S("port")).toInt(), 31001);
	LG_EQUAL(first.value(S("protocol")).toString(), S("socks"));
	const auto settings = first.value(S("settings")).toObject();
	LG_EQUAL(settings.value(S("auth")).toString(), S("password"));
	LG_EQUAL(settings.value(S("accounts")).toArray().at(0).toObject()
		.value(S("pass")).toString(), S("secret"));
	const auto out = config.value(S("outbounds")).toArray();
	LG_EQUAL(out.at(0).toObject().value(S("tag")).toString(), S("block"));
	LG_EQUAL(out.at(2).toObject().value(S("tag")).toString(), S("out-1"));
	LG_CHECK(!out.at(2).toObject().contains(S("sendThrough")));
	const auto rule = config.value(S("routing")).toObject()
		.value(S("rules")).toArray().at(1).toObject();
	LG_EQUAL(rule.value(S("inboundTag")).toArray().at(0).toString(), S("in-1"));
	LG_EQUAL(rule.value(S("outboundTag")).toString(), S("out-1"));
	LG_EQUAL(config.value(S("log")).toObject().value(S("loglevel")).toString(), S("warning"));
}

LUCKGRAM_TEST(XrayPorts) {
	LG_CHECK(PickPorts(30000, { 30001 }, 3) == (std::vector<int>{ 30000, 30002, 30003 }));
	LG_CHECK(PickPorts(59999, {}, 2) == (std::vector<int>{ 59999, 20000 }));
	LG_CHECK(PickPorts(5, {}, 1) == (std::vector<int>{ 20000 }));
}

LUCKGRAM_TEST(XrayBase64AndLooksLikeLink) {
	for (const auto text : { "f", "fo", "foo", "foob", "fooba", "foobar", "中文" }) {
		const auto bytes = QByteArray(text);
		const auto url = Base64UrlNoPad(bytes);
		LG_EQUAL(url, QString::fromLatin1(bytes.toBase64(
			QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
		LG_EQUAL(Base64Text(url).value_or(QString()), S(text));
		LG_EQUAL(
			Base64Text(QString::fromLatin1(bytes.toBase64())).value_or(QString()),
			S(text));
	}
	LG_CHECK(!Base64Text(S("@@@")).has_value());
	LG_CHECK(!LooksLikeLink(S("socks5://h:1")));
	LG_CHECK(LooksLikeLink(S(" VLESS://x")));
	LG_EQUAL(Label(S("vmess")), S("VMess"));
	LG_EQUAL(Label(S("ss")), S("SS"));
}

// #125: xray 26 exits with code 23 for VLESS / Trojan without TLS to a
// public address (checked with xray 26.10.31 `run -test`); such a node
// isn't built, so it can't stop the core for the other nodes.
LUCKGRAM_TEST(XrayRefusesPlainVlessTrojan) {
	const auto vless = [](const char *rest) {
		return S("vless://") + kUuid + S(rest);
	};
	const auto plain = vless("@5.6.7.8:443?encryption=none&security=none&type=tcp#QA");
	LG_CHECK(NeedsEncryption(plain));
	LG_CHECK(Outbound(plain).isEmpty());
	LG_CHECK(Parse(plain).has_value()); // Still a node (shown, deletable).
	LG_CHECK(NeedsEncryption(vless("@example.com:443?type=ws")));
	LG_CHECK(NeedsEncryption(S("trojan://pw@5.6.7.8:443?security=none&type=tcp")));
	// Encrypted, or a private address: fine.
	LG_CHECK(!NeedsEncryption(vless("@5.6.7.8:443?security=tls&type=tcp")));
	LG_CHECK(!NeedsEncryption(vless("@5.6.7.8:443?security=reality&pbk=k&type=tcp")));
	LG_CHECK(!NeedsEncryption(vless("@5.6.7.8:443?encryption=mlkem768x25519plus.native.0rtt.x&type=tcp")));
	const auto lan = vless("@192.168.1.5:443?security=none&type=tcp");
	LG_CHECK(!NeedsEncryption(lan));
	LG_CHECK(!Outbound(lan).isEmpty());
	LG_CHECK(!NeedsEncryption(S("vmess://") + kUuid + S("@5.6.7.8:443?encryption=auto&type=tcp")));
	LG_CHECK(!NeedsEncryption(S("http://example.com")));
}

LUCKGRAM_TEST(XrayPrivateAddress) {
	for (const auto host : { "10.1.1.1", "172.16.0.1", "172.31.255.1",
			"192.168.0.1", "127.0.0.1", "169.254.1.1", "fd00::1", "fe80::1",
			"[fe80::1]", "::1", "localhost", "LocalHost" }) {
		LG_CHECK(IsPrivateAddress(S(host)));
	}
	for (const auto host : { "5.6.7.8", "172.32.0.1", "100.64.0.1",
			"300.1.1.1", "2001:db8::1", "::", "example.com", "a.lan" }) {
		LG_CHECK(!IsPrivateAddress(S(host)));
	}
}

LUCKGRAM_TEST(XrayStartFailureReason) {
	const auto output = QByteArray(
		"Xray 26.10.31 (Xray, Penetrates Everything.)\r\n"
		"Failed to start: main: failed to load config files: [stdin:] > "
		"infra/conf: failed to build outbound config with tag out-0 > "
		"infra/conf: vless without TLS or other encryption is prohibited "
		"unless the server address is a private IP or domain\r\n");
	LG_EQUAL(StartFailureReason(output), S("infra/conf: vless without TLS or "
		"other encryption is prohibited unless the server address is a "
		"private IP or domain"));
	// Hosts and addresses never reach the log.
	const auto hosts = StartFailureReason(QByteArray(
		"Failed to start: x > dial edge.example.com 5.6.7.8:443 [2001:db8::1]:443"));
	LG_CHECK(!hosts.contains(S("example")));
	LG_CHECK(!hosts.contains(S("5.6.7.8")));
	LG_CHECK(!hosts.contains(S("2001")));
	LG_CHECK(StartFailureReason(QByteArray("started")).isEmpty());
}
