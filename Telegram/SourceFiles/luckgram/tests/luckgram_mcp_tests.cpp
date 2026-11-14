/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/mcp/luckgram_mcp_logic.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

// Local MCP server: the request parser, the access checks and the methods.
namespace {

using namespace Luckgram::Mcp;

const auto kToken = QStringLiteral(
	"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ-_");

QString S(const char *utf8) {
	return QString::fromUtf8(utf8);
}

QJsonObject J(const QByteArray &json) {
	return QJsonDocument::fromJson(json).object();
}

struct Fake {
	std::vector<AccountInfo> list;
	bool locked = false;
	bool refuse = false; // The app asks the user first, nothing changes.
	int activated = 0;

	Fake() {
		auto a = AccountInfo();
		a.id = 111;
		a.index = 1;
		a.name = S("Alice");
		a.username = S("alice");
		a.phone = S("8613800000001");
		a.unread = 5;
		a.active = true;
		auto b = AccountInfo();
		b.id = 7000000000001ULL;
		b.index = 2;
		b.name = S("奶油小可爱");
		b.phone = S("923001234567");
		b.premium = true;
		list = { a, b };
	}

	Backend backend() {
		auto result = Backend();
		result.locked = [this] { return locked; };
		result.accounts = [this] { return list; };
		result.activate = [this](quint64 id) {
			++activated;
			if (!refuse) {
				for (auto &account : list) {
					account.active = (account.id == id);
				}
			}
			return QString();
		};
		return result;
	}
};

Config MakeConfig() {
	auto result = Config();
	result.token = kToken;
	result.port = kDefaultPort;
	return result;
}

Request Post(const QByteArray &body) {
	auto result = Request();
	result.method = "POST";
	result.path = "/mcp";
	result.headers = {
		{ "host", "127.0.0.1:28765" },
		{ "authorization", "Bearer " + kToken.toLatin1() },
		{ "content-type", "application/json" },
	};
	result.body = body;
	return result;
}

void SetHeader(Request &request, const QByteArray &name, const QByteArray &value) {
	for (auto &[key, was] : request.headers) {
		if (key == name) {
			was = value;
			return;
		}
	}
	request.headers.emplace_back(name, value);
}

QByteArray Call(const char *tool, const char *arguments = "{}") {
	return QByteArray(R"({"jsonrpc":"2.0","id":7,"method":"tools/call",)")
		+ R"("params":{"name":")" + tool + R"(","arguments":)" + arguments
		+ "}}";
}

QJsonObject ToolReply(Fake &fake, const char *tool, const char *arguments = "{}") {
	const auto response = Handle(
		Post(Call(tool, arguments)),
		MakeConfig(),
		fake.backend());
	LG_EQUAL(response.status, 200);
	const auto reply = J(response.body);
	LG_EQUAL(reply.value(S("id")).toInt(), 7);
	return reply.value(S("result")).toObject();
}

QString ErrorText(const QJsonObject &result) {
	LG_CHECK(result.value(S("isError")).toBool());
	return result.value(S("content")).toArray().at(0).toObject().value(
		S("text")).toString();
}

} // namespace

LUCKGRAM_TEST(McpParseWaitsForTheWholeRequest) {
	const auto full = QByteArray(
		"POST /mcp?x=1 HTTP/1.1\r\n"
		"Host: 127.0.0.1:28765\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: 7\r\n"
		"\r\n"
		"{\"a\":1}");
	auto request = Request();
	auto status = 0;
	for (auto size = 0; size != int(full.size()); ++size) {
		LG_CHECK(ParseRequest(full.left(size), &request, &status)
			== ParseResult::Incomplete);
	}
	LG_CHECK(ParseRequest(full, &request, &status) == ParseResult::Ready);
	LG_EQUAL(request.method, QByteArray("POST"));
	LG_EQUAL(request.path, QByteArray("/mcp"));
	LG_EQUAL(request.header("host"), QByteArray("127.0.0.1:28765"));
	LG_EQUAL(request.header("content-type"), QByteArray("application/json"));
	LG_EQUAL(request.body, QByteArray("{\"a\":1}"));
}

LUCKGRAM_TEST(McpParseRejectsBadRequests) {
	auto request = Request();
	auto status = 0;
	LG_CHECK(ParseRequest("garbage\r\n\r\n", &request, &status)
		== ParseResult::Bad);
	LG_EQUAL(status, 400);
	LG_CHECK(ParseRequest(
		"POST /mcp HTTP/1.1\r\nContent-Length: x\r\n\r\n",
		&request,
		&status) == ParseResult::Bad);
	LG_EQUAL(status, 400);
	LG_CHECK(ParseRequest(
		"POST /mcp HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n",
		&request,
		&status) == ParseResult::Bad);
	LG_EQUAL(status, 400);
	LG_CHECK(ParseRequest(
		"POST /mcp HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",
		&request,
		&status) == ParseResult::Bad);
	LG_EQUAL(status, 411);
	LG_CHECK(ParseRequest(
		"POST /mcp HTTP/1.1\r\nContent-Length: 99999999\r\n\r\n",
		&request,
		&status) == ParseResult::Bad);
	LG_EQUAL(status, 413);
	// Headers that never end.
	LG_CHECK(ParseRequest(
		"POST /mcp HTTP/1.1\r\nX: " + QByteArray(kMaxHeaderBytes, 'a'),
		&request,
		&status) == ParseResult::Bad);
	LG_EQUAL(status, 431);
}

LUCKGRAM_TEST(McpAccessChecks) {
	auto fake = Fake();
	const auto config = MakeConfig();
	const auto body = QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"ping"})");
	LG_EQUAL(Handle(Post(body), config, fake.backend()).status, 200);

	auto request = Post(body);
	SetHeader(request, "authorization", "");
	LG_EQUAL(Handle(request, config, fake.backend()).status, 401);
	SetHeader(request, "authorization", "Bearer wrong");
	LG_EQUAL(Handle(request, config, fake.backend()).status, 401);
	SetHeader(request, "authorization", "Bearer " + kToken.toLatin1() + "x");
	LG_EQUAL(Handle(request, config, fake.backend()).status, 401);
	SetHeader(request, "authorization", "bearer " + kToken.toLatin1());
	LG_EQUAL(Handle(request, config, fake.backend()).status, 200);

	// No usable token configured: nothing gets in, not even an empty one.
	auto empty = config;
	empty.token = QString();
	request = Post(body);
	SetHeader(request, "authorization", "Bearer ");
	LG_EQUAL(Handle(request, empty, fake.backend()).status, 401);

	// DNS rebinding: the page's own name comes in Host.
	request = Post(body);
	SetHeader(request, "host", "evil.example:28765");
	LG_EQUAL(Handle(request, config, fake.backend()).status, 403);
	SetHeader(request, "host", "127.0.0.1.evil.example");
	LG_EQUAL(Handle(request, config, fake.backend()).status, 403);
	SetHeader(request, "host", "");
	LG_EQUAL(Handle(request, config, fake.backend()).status, 403);
	// A forwarded port is fine.
	SetHeader(request, "host", "localhost:9000");
	LG_EQUAL(Handle(request, config, fake.backend()).status, 200);
	SetHeader(request, "host", "[::1]:9000");
	LG_EQUAL(Handle(request, config, fake.backend()).status, 200);

	// A web page, even with the token.
	request = Post(body);
	SetHeader(request, "origin", "https://evil.example");
	LG_EQUAL(Handle(request, config, fake.backend()).status, 403);
	SetHeader(request, "origin", "null");
	LG_EQUAL(Handle(request, config, fake.backend()).status, 403);
	SetHeader(request, "origin", "http://localhost:5173");
	LG_EQUAL(Handle(request, config, fake.backend()).status, 200);

	request = Post(body);
	request.path = "/other";
	LG_EQUAL(Handle(request, config, fake.backend()).status, 404);
	request = Post(body);
	request.path = "/mcp/";
	LG_EQUAL(Handle(request, config, fake.backend()).status, 200);
	request = Post(body);
	request.method = "GET";
	LG_EQUAL(Handle(request, config, fake.backend()).status, 405);
	request = Post(body);
	SetHeader(request, "content-type", "text/plain");
	LG_EQUAL(Handle(request, config, fake.backend()).status, 415);
	LG_EQUAL(Handle(Post("{nope"), config, fake.backend()).status, 400);
	LG_EQUAL(Handle(Post("[]"), config, fake.backend()).status, 400);
}

LUCKGRAM_TEST(McpTokens) {
	LG_CHECK(IsValidToken(kToken));
	LG_CHECK(!IsValidToken(QString()));
	LG_CHECK(!IsValidToken(S("short")));
	LG_CHECK(!IsValidToken(kToken + S(" ")));
	LG_CHECK(SameToken("abc", "abc"));
	LG_CHECK(!SameToken("abc", "abd"));
	LG_CHECK(!SameToken("abc", "abcd"));
	LG_CHECK(!SameToken("", "a"));
}

LUCKGRAM_TEST(McpInitializeAndToolsList) {
	auto fake = Fake();
	auto reply = HandleMessage(J(R"({"jsonrpc":"2.0","id":"a","method":"initialize",
		"params":{"protocolVersion":"2025-03-26","capabilities":{},
		"clientInfo":{"name":"t","version":"1"}}})"), fake.backend());
	LG_EQUAL(reply.value(S("id")).toString(), S("a"));
	auto result = reply.value(S("result")).toObject();
	LG_EQUAL(result.value(S("protocolVersion")).toString(), S("2025-03-26"));
	LG_CHECK(result.value(S("capabilities")).toObject().contains(S("tools")));
	LG_EQUAL(
		result.value(S("serverInfo")).toObject().value(S("name")).toString(),
		S("luckgram-desktop"));

	// An unknown version gets the newest one this server speaks.
	reply = HandleMessage(J(R"({"jsonrpc":"2.0","id":1,"method":"initialize",
		"params":{"protocolVersion":"1999-01-01"}})"), fake.backend());
	LG_EQUAL(
		reply.value(S("result")).toObject().value(
			S("protocolVersion")).toString(),
		S("2025-11-25"));

	// Notifications and stray responses get nothing back.
	LG_CHECK(HandleMessage(
		J(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"),
		fake.backend()).isEmpty());
	LG_CHECK(HandleMessage(
		J(R"({"jsonrpc":"2.0","id":3,"result":{}})"),
		fake.backend()).isEmpty());
	const auto accepted = Handle(
		Post(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"),
		MakeConfig(),
		fake.backend());
	LG_EQUAL(accepted.status, 202);
	LG_CHECK(accepted.body.isEmpty());

	reply = HandleMessage(
		J(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"),
		fake.backend());
	const auto tools = reply.value(S("result")).toObject().value(
		S("tools")).toArray();
	auto names = QStringList();
	for (const auto &tool : tools) {
		const auto object = tool.toObject();
		names.push_back(object.value(S("name")).toString());
		LG_EQUAL(
			object.value(S("inputSchema")).toObject().value(
				S("type")).toString(),
			S("object"));
	}
	LG_EQUAL(names, (QStringList{
		S("list_accounts"),
		S("get_active_account"),
		S("switch_account") }));

	reply = HandleMessage(
		J(R"({"jsonrpc":"2.0","id":4,"method":"resources/list"})"),
		fake.backend());
	LG_EQUAL(
		reply.value(S("error")).toObject().value(S("code")).toInt(),
		-32601);
	reply = HandleMessage(J(R"({"id":4})"), fake.backend());
	LG_EQUAL(
		reply.value(S("error")).toObject().value(S("code")).toInt(),
		-32600);
}

LUCKGRAM_TEST(McpBatch) {
	auto fake = Fake();
	const auto response = Handle(
		Post(R"([{"jsonrpc":"2.0","id":1,"method":"ping"},
			{"jsonrpc":"2.0","method":"notifications/initialized"},
			5,
			{"jsonrpc":"2.0","id":2,"method":"ping"}])"),
		MakeConfig(),
		fake.backend());
	LG_EQUAL(response.status, 200);
	const auto replies = QJsonDocument::fromJson(response.body).array();
	LG_EQUAL(int(replies.size()), 3);
	LG_EQUAL(replies.at(0).toObject().value(S("id")).toInt(), 1);
	LG_CHECK(replies.at(1).toObject().contains(S("error")));
	LG_EQUAL(replies.at(2).toObject().value(S("id")).toInt(), 2);
}

LUCKGRAM_TEST(McpListAndActive) {
	auto fake = Fake();
	auto result = ToolReply(fake, "list_accounts");
	LG_CHECK(!result.value(S("isError")).toBool());
	const auto accounts = result.value(S("structuredContent")).toObject(
		).value(S("accounts")).toArray();
	LG_EQUAL(int(accounts.size()), 2);
	const auto first = accounts.at(0).toObject();
	LG_EQUAL(first.value(S("id")).toString(), S("111"));
	LG_EQUAL(first.value(S("index")).toInt(), 1);
	LG_EQUAL(first.value(S("username")).toString(), S("alice"));
	LG_EQUAL(first.value(S("unread")).toInt(), 5);
	LG_CHECK(first.value(S("active")).toBool());
	const auto second = accounts.at(1).toObject();
	// A string, so a 64-bit id survives any JSON reader.
	LG_EQUAL(second.value(S("id")).toString(), S("7000000000001"));
	LG_EQUAL(second.value(S("name")).toString(), S("奶油小可爱"));
	LG_CHECK(!second.contains(S("username")));
	LG_CHECK(second.value(S("premium")).toBool());
	// The text block is the same JSON.
	const auto text = result.value(S("content")).toArray().at(0).toObject(
		).value(S("text")).toString();
	LG_CHECK(J(text.toUtf8()) == result.value(S("structuredContent")).toObject());

	result = ToolReply(fake, "get_active_account");
	LG_EQUAL(
		result.value(S("structuredContent")).toObject().value(
			S("account")).toObject().value(S("id")).toString(),
		S("111"));

	fake.list[0].active = false;
	LG_CHECK(!ErrorText(ToolReply(fake, "get_active_account")).isEmpty());
}

LUCKGRAM_TEST(McpSwitch) {
	auto fake = Fake();
	auto result = ToolReply(fake, "switch_account", R"({"id":"7000000000001"})");
	auto value = result.value(S("structuredContent")).toObject();
	LG_CHECK(value.value(S("switched")).toBool());
	LG_EQUAL(
		value.value(S("account")).toObject().value(S("id")).toString(),
		S("7000000000001"));
	LG_CHECK(value.value(S("account")).toObject().value(S("active")).toBool());
	LG_EQUAL(fake.activated, 1);

	// Already there: nothing is touched.
	result = ToolReply(fake, "switch_account", R"({"id":7000000000001})");
	value = result.value(S("structuredContent")).toObject();
	LG_CHECK(!value.value(S("switched")).toBool());
	LG_CHECK(value.value(S("already_active")).toBool());
	LG_EQUAL(fake.activated, 1);

	result = ToolReply(fake, "switch_account", R"({"username":"@ALICE"})");
	LG_CHECK(result.value(S("structuredContent")).toObject().value(
		S("switched")).toBool());
	result = ToolReply(fake, "switch_account", R"({"phone":"+92 300 1234567"})");
	LG_CHECK(result.value(S("structuredContent")).toObject().value(
		S("switched")).toBool());
	result = ToolReply(fake, "switch_account", R"({"index":1})");
	LG_CHECK(result.value(S("structuredContent")).toObject().value(
		S("switched")).toBool());
	LG_EQUAL(fake.activated, 4);
	LG_CHECK(fake.list[0].active);

	// The app asked the user instead of switching: not reported as done.
	fake.refuse = true;
	result = ToolReply(fake, "switch_account", R"({"index":2})");
	LG_CHECK(!ErrorText(result).isEmpty());
	LG_CHECK(fake.list[0].active);
}

LUCKGRAM_TEST(McpSwitchWrongArguments) {
	auto fake = Fake();
	for (const auto arguments : {
		"{}",
		R"({"id":"111","index":1})",
		R"({"id":"999"})",
		R"({"id":"abc"})",
		R"({"id":-1})",
		R"({"id":1.5})",
		R"({"id":true})",
		R"({"username":"nobody"})",
		R"({"username":"@"})",
		R"({"phone":"+1 555"})",
		R"({"phone":"none"})",
		R"({"index":0})",
		R"({"index":3})",
		R"({"index":1.5})",
		R"({"index":"1"})",
	}) {
		LG_CHECK(!ErrorText(
			ToolReply(fake, "switch_account", arguments)).isEmpty());
	}
	LG_EQUAL(fake.activated, 0);

	// An account without a username is not matched by an empty one.
	auto error = QString();
	LG_EQUAL(
		ResolveAccount(fake.list, J(R"({"username":""})"), &error),
		quint64(0));

	const auto response = Handle(
		Post(Call("delete_everything")),
		MakeConfig(),
		fake.backend());
	LG_EQUAL(
		J(response.body).value(S("error")).toObject().value(
			S("code")).toInt(),
		-32602);
}

LUCKGRAM_TEST(McpLockedShowsNothing) {
	auto fake = Fake();
	fake.locked = true;
	for (const auto tool : { "list_accounts", "get_active_account" }) {
		const auto result = ToolReply(fake, tool);
		LG_CHECK(!ErrorText(result).isEmpty());
		LG_CHECK(!result.contains(S("structuredContent")));
	}
	LG_CHECK(!ErrorText(
		ToolReply(fake, "switch_account", R"({"index":2})")).isEmpty());
	LG_EQUAL(fake.activated, 0);
}

LUCKGRAM_TEST(McpSerializeAndClientConfig) {
	auto response = Response();
	response.status = 401;
	response.contentType = "text/plain; charset=utf-8";
	response.body = "no\n";
	response.headers.emplace_back("WWW-Authenticate", "Bearer");
	const auto bytes = Serialize(response);
	LG_CHECK(bytes.startsWith("HTTP/1.1 401 Unauthorized\r\n"));
	LG_CHECK(bytes.contains("\r\nContent-Length: 3\r\n"));
	LG_CHECK(bytes.contains("\r\nWWW-Authenticate: Bearer\r\n"));
	LG_CHECK(bytes.contains("\r\nConnection: close\r\n"));
	LG_CHECK(!bytes.contains("Access-Control"));
	LG_CHECK(bytes.endsWith("\r\n\r\nno\n"));

	const auto config = J(ClientConfigJson(MakeConfig()).toUtf8());
	const auto server = config.value(S("mcpServers")).toObject().value(
		S("luckgram")).toObject();
	LG_EQUAL(server.value(S("type")).toString(), S("http"));
	LG_EQUAL(server.value(S("url")).toString(), S("http://127.0.0.1:28765/mcp"));
	LG_EQUAL(
		server.value(S("headers")).toObject().value(
			S("Authorization")).toString(),
		S("Bearer ") + kToken);
}
