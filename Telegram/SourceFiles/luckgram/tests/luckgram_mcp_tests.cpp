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
	// Skills: "<skill>/<account>" switched on, the last call, and answers
	// held back until flush() (a tool waiting for the network).
	QStringList enabled;
	QString calledTool;
	quint64 calledAccount = 0;
	QJsonObject calledArguments;
	int calls = 0;
	bool hold = false;
	QString callError;
	std::vector<ToolDone> held;
	std::vector<std::pair<QString, std::vector<qint64>>> times;
	qint64 now = 1'800'000'000'000LL;

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
		result.skillEnabled = [this](quint64 account, const QString &skill) {
			return enabled.contains(
				skill + QChar('/') + QString::number(account));
		};
		result.nowMs = [this] { return now; };
		result.loadTimes = [this](const QString &key) {
			for (const auto &[was, list] : times) {
				if (was == key) {
					return list;
				}
			}
			return std::vector<qint64>();
		};
		result.saveTimes = [this](
				const QString &key,
				const std::vector<qint64> &list) {
			for (auto &[was, old] : times) {
				if (was == key) {
					old = list;
					return;
				}
			}
			times.emplace_back(key, list);
		};
		result.call = [this](
				const QString &tool,
				quint64 account,
				const QJsonObject &arguments,
				ToolDone done) {
			++calls;
			calledTool = tool;
			calledAccount = account;
			calledArguments = arguments;
			if (hold) {
				held.push_back(std::move(done));
				return;
			}
			auto outcome = ToolOutcome();
			outcome.error = callError;
			outcome.value.insert(QStringLiteral("total"), 3);
			done(std::move(outcome));
		};
		return result;
	}

	void flush() {
		for (const auto &done : std::exchange(held, {})) {
			auto outcome = ToolOutcome();
			outcome.value.insert(QStringLiteral("total"), 3);
			done(std::move(outcome));
		}
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
		S("switch_account"),
		S("list_friends"),
		S("find_user"),
		S("add_friend"),
		S("remove_friend"),
		S("list_chats"),
		S("get_messages"),
		S("send_message"),
		S("mark_chat_read"),
		S("join_chat"),
		S("leave_chat"),
		S("update_profile") }));

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

LUCKGRAM_TEST(McpSkillOffByDefault) {
	auto fake = Fake();
	for (const auto tool : { "list_friends", "list_chats" }) {
		const auto text = ErrorText(ToolReply(fake, tool));
		LG_CHECK(text.contains(S("skill is off")));
		LG_CHECK(text.contains(S("111"))); // Which account, so it can be told.
	}
	LG_EQUAL(fake.calls, 0);

	// One skill of one account says nothing about the others.
	fake.enabled = { S("friends/111") };
	LG_CHECK(!ToolReply(fake, "list_friends").value(S("isError")).toBool());
	LG_CHECK(ToolReply(fake, "list_chats").value(S("isError")).toBool());
	LG_CHECK(ToolReply(
		fake,
		"list_friends",
		R"({"account":"7000000000001"})").value(S("isError")).toBool());
	LG_EQUAL(fake.calls, 1);
}

LUCKGRAM_TEST(McpSkillAccountAndArguments) {
	auto fake = Fake();
	fake.enabled = {
		S("friends/111"),
		S("friends/7000000000001"),
		S("chats/111"),
	};
	// The active account and the defaults.
	auto result = ToolReply(fake, "list_friends");
	LG_EQUAL(fake.calledTool, S("list_friends"));
	LG_EQUAL(fake.calledAccount, quint64(111));
	LG_EQUAL(fake.calledArguments.value(S("limit")).toInt(), 200);
	LG_EQUAL(fake.calledArguments.value(S("offset")).toInt(), 0);
	LG_EQUAL(fake.calledArguments.value(S("query")).toString(), QString());
	LG_CHECK(!fake.calledArguments.contains(S("account")));
	auto value = result.value(S("structuredContent")).toObject();
	LG_EQUAL(value.value(S("total")).toInt(), 3);
	LG_EQUAL(value.value(S("account")).toString(), S("111"));

	// Another account, without switching to it.
	result = ToolReply(
		fake,
		"list_friends",
		R"({"account":"7000000000001","query":"  li  ","limit":5,"offset":10})");
	LG_EQUAL(fake.calledAccount, quint64(7000000000001ULL));
	LG_EQUAL(fake.calledArguments.value(S("query")).toString(), S("li"));
	LG_EQUAL(fake.calledArguments.value(S("limit")).toInt(), 5);
	LG_EQUAL(fake.calledArguments.value(S("offset")).toInt(), 10);
	LG_EQUAL(fake.activated, 0);
	LG_CHECK(fake.list[0].active);
	LG_EQUAL(
		result.value(S("structuredContent")).toObject().value(
			S("account")).toString(),
		S("7000000000001"));

	result = ToolReply(fake, "list_chats", R"({"unread_only":true})");
	LG_EQUAL(fake.calledArguments.value(S("limit")).toInt(), 50);
	LG_CHECK(fake.calledArguments.value(S("unread_only")).toBool());

	const auto before = fake.calls;
	for (const auto arguments : {
		R"({"account":"999"})",
		R"({"account":"abc"})",
		R"({"limit":0})",
		R"({"limit":1001})",
		R"({"limit":"5"})",
		R"({"limit":1.5})",
		R"({"offset":-1})",
		R"({"query":5})",
	}) {
		LG_CHECK(!ErrorText(
			ToolReply(fake, "list_friends", arguments)).isEmpty());
	}
	LG_CHECK(!ErrorText(
		ToolReply(fake, "list_chats", R"({"limit":201})")).isEmpty());
	LG_CHECK(!ErrorText(
		ToolReply(fake, "list_chats", R"({"unread_only":"yes"})")).isEmpty());
	LG_EQUAL(fake.calls, before);

	// What the app could not do comes back as the tool's error.
	fake.callError = S("Contacts did not load in time.");
	LG_EQUAL(
		ErrorText(ToolReply(fake, "list_friends")),
		S("Contacts did not load in time."));

	// No active account and none named.
	fake.list[0].active = false;
	LG_CHECK(!ErrorText(ToolReply(fake, "list_friends")).isEmpty());
}

LUCKGRAM_TEST(McpSkillLockedAndLate) {
	auto fake = Fake();
	fake.enabled = { S("friends/111") };
	fake.locked = true;
	LG_CHECK(!ErrorText(ToolReply(fake, "list_friends")).isEmpty());
	LG_EQUAL(fake.calls, 0);
	fake.locked = false;

	// A tool that answers later: nothing is sent until it does, and a
	// batch keeps the order of its requests.
	fake.hold = true;
	auto answered = 0;
	auto response = Response();
	HandleAsync(
		Post(QByteArray("[") + Call("list_friends")
			+ R"(,{"jsonrpc":"2.0","id":8,"method":"ping"}])"),
		MakeConfig(),
		fake.backend(),
		[&](Response got) {
			++answered;
			response = std::move(got);
		});
	LG_EQUAL(answered, 0);
	LG_EQUAL(fake.calls, 1);
	fake.flush();
	LG_EQUAL(answered, 1);
	LG_EQUAL(response.status, 200);
	const auto replies = QJsonDocument::fromJson(response.body).array();
	LG_EQUAL(int(replies.size()), 2);
	LG_EQUAL(replies.at(0).toObject().value(S("id")).toInt(), 7);
	LG_EQUAL(replies.at(1).toObject().value(S("id")).toInt(), 8);

	// The sync wrapper never pretends: 500 while the answer is not there.
	LG_EQUAL(
		Handle(Post(Call("list_friends")), MakeConfig(), fake.backend()).status,
		500);
	fake.flush();
}

LUCKGRAM_TEST(McpSkillRegistry) {
	LG_EQUAL(int(Skills().size()), 8);
	LG_CHECK(!Skills()[0].write);
	LG_CHECK(Skills()[1].write); // friends_manage asks before it goes on.
	LG_EQUAL(
		SkillSettingKey(S("friends"), 7000000000001ULL),
		S("mcp_skill_friends_7000000000001"));
}

LUCKGRAM_TEST(McpFriendTargets) {
	auto fake = Fake();
	fake.enabled = { S("friends/111"), S("friends_manage/111") };

	ToolReply(fake, "find_user", R"({"username":"@Some_User"})");
	LG_EQUAL(fake.calledTool, S("find_user"));
	LG_EQUAL(fake.calledArguments.value(S("username")).toString(), S("Some_User"));
	ToolReply(fake, "find_user", R"({"phone":"+57 310 000 0000"})");
	LG_EQUAL(fake.calledArguments.value(S("phone")).toString(), S("573100000000"));

	ToolReply(fake, "add_friend", R"({"user_id":"7000000000009","first_name":" Li ","last_name":"Wei"})");
	LG_EQUAL(fake.calledTool, S("add_friend"));
	LG_EQUAL(fake.calledArguments.value(S("user_id")).toString(), S("7000000000009"));
	LG_EQUAL(fake.calledArguments.value(S("first_name")).toString(), S("Li"));
	LG_EQUAL(fake.calledArguments.value(S("last_name")).toString(), S("Wei"));
	ToolReply(fake, "remove_friend", R"({"user_id":5})");
	LG_EQUAL(fake.calledArguments.value(S("user_id")).toString(), S("5"));

	const auto before = fake.calls;
	const auto bad = [&](const char *tool, const char *arguments) {
		LG_CHECK(!ErrorText(ToolReply(fake, tool, arguments)).isEmpty());
	};
	bad("find_user", "{}");
	bad("find_user", R"({"username":"abcd","phone":"12345678"})");
	bad("find_user", R"({"user_id":"5"})");
	bad("find_user", R"({"username":"ab"})");
	bad("find_user", R"({"username":"has space"})");
	bad("find_user", R"({"username":"t.me/x"})");
	bad("find_user", R"({"phone":"123"})");
	bad("find_user", R"({"username":5})");
	bad("add_friend", "{}");
	bad("add_friend", R"({"user_id":"0"})");
	bad("add_friend", R"({"user_id":"abc"})");
	bad("add_friend", R"({"user_id":"5","first_name":7})");
	bad("remove_friend", R"({"phone":"12345678"})");
	bad("remove_friend", R"({"user_id":"5","first_name":"x"})");
	LG_EQUAL(fake.calls, before);
}

LUCKGRAM_TEST(McpWriteSkillIsItsOwnSwitch) {
	auto fake = Fake();
	// Seeing friends does not allow changing them.
	fake.enabled = { S("friends/111") };
	LG_CHECK(ErrorText(
		ToolReply(fake, "add_friend", R"({"user_id":"5"})")).contains(
			S("friends_manage")));
	LG_CHECK(!ErrorText(
		ToolReply(fake, "remove_friend", R"({"user_id":"5"})")).isEmpty());
	LG_EQUAL(fake.calls, 0);
	// Refused calls don't use up the limit.
	LG_CHECK(fake.times.empty());
}

LUCKGRAM_TEST(McpRateLimitMath) {
	constexpr auto kMinute = qint64(60) * 1000;
	constexpr auto kHour = 60 * kMinute;
	const auto now = qint64(1'800'000'000'000LL);
	const auto limit = RateLimit{ 2, 3 };

	auto times = std::vector<qint64>();
	LG_EQUAL(RateWaitSeconds(times, now, limit), qint64(0));
	times = { now - 10 * kMinute };
	LG_EQUAL(RateWaitSeconds(times, now, limit), qint64(0));
	// Two in the hour: free when the older one is an hour old.
	times = { now - 10 * kMinute, now - 50 * kMinute };
	LG_EQUAL(RateWaitSeconds(times, now, limit), qint64(10 * 60));
	// The day limit, with the hour free.
	times = { now - 2 * kHour, now - 3 * kHour, now - 23 * kHour };
	LG_EQUAL(RateWaitSeconds(times, now, limit), qint64(60 * 60));
	// Older than a day and from the future (clock set back): dropped.
	times = { now - 25 * kHour, now + kHour, now - 30 * kHour };
	LG_EQUAL(RateWaitSeconds(times, now, limit), qint64(0));
	LG_CHECK(times.empty());
	// No limit.
	times = { now - 1, now - 2, now - 3 };
	LG_EQUAL(RateWaitSeconds(times, now, RateLimit()), qint64(0));

	LG_EQUAL(SerializeTimes({ 5, 70 }), S("5,70"));
	LG_CHECK(ParseTimes(S("5,x,,70,-3")) == (std::vector<qint64>{ 5, 70 }));
	LG_EQUAL(LimitOf(S("add_friend")).perHour, 5);
	LG_EQUAL(LimitOf(S("add_friend")).perDay, 20);
	LG_EQUAL(LimitOf(S("list_friends")).perHour, 0);
	LG_EQUAL(
		RateSettingKey(S("add_friend"), 111),
		S("mcp_rate_add_friend_111"));
}

LUCKGRAM_TEST(McpRateLimitPerAccount) {
	auto fake = Fake();
	fake.enabled = {
		S("friends_manage/111"),
		S("friends_manage/7000000000001"),
	};
	for (auto i = 0; i != 5; ++i) {
		LG_CHECK(!ToolReply(
			fake,
			"add_friend",
			R"({"user_id":"5"})").value(S("isError")).toBool());
		fake.now += 1000;
	}
	LG_EQUAL(fake.calls, 5);
	const auto text = ErrorText(
		ToolReply(fake, "add_friend", R"({"user_id":"5"})"));
	LG_CHECK(text.contains(S("Rate limit")));
	LG_CHECK(text.contains(S("seconds")));
	LG_EQUAL(fake.calls, 5);
	// Another account has its own count.
	LG_CHECK(!ToolReply(
		fake,
		"add_friend",
		R"({"account":"7000000000001","user_id":"5"})").value(
			S("isError")).toBool());
	LG_EQUAL(fake.calls, 6);
	// An hour later the first account may again, until the day limit.
	fake.now += qint64(61) * 60 * 1000;
	LG_CHECK(!ToolReply(
		fake,
		"add_friend",
		R"({"user_id":"5"})").value(S("isError")).toBool());
	// A failed call counts too.
	fake.callError = S("FLOOD_WAIT_30");
	LG_EQUAL(
		ErrorText(ToolReply(fake, "add_friend", R"({"user_id":"5"})")),
		S("FLOOD_WAIT_30"));
	LG_EQUAL(int(fake.backend().loadTimes(
		RateSettingKey(S("add_friend"), 111)).size()), 7);
}

LUCKGRAM_TEST(McpMessagesArguments) {
	auto fake = Fake();
	fake.enabled = { S("messages/111"), S("messages_send/111") };

	ToolReply(fake, "get_messages", R"({"chat":"7000000000009"})");
	LG_EQUAL(fake.calledTool, S("get_messages"));
	LG_EQUAL(fake.calledArguments.value(S("chat_id")).toString(), S("7000000000009"));
	LG_CHECK(!fake.calledArguments.contains(S("chat_username")));
	LG_EQUAL(fake.calledArguments.value(S("limit")).toInt(), 20);
	LG_EQUAL(fake.calledArguments.value(S("before_id")).toInt(), 0);
	ToolReply(fake, "get_messages", R"({"chat":"@durov","limit":100,"before_id":55})");
	LG_EQUAL(fake.calledArguments.value(S("chat_username")).toString(), S("durov"));
	LG_EQUAL(fake.calledArguments.value(S("limit")).toInt(), 100);
	LG_EQUAL(fake.calledArguments.value(S("before_id")).toInt(), 55);

	ToolReply(fake, "send_message", R"({"chat":"some_group","text":"  hello \n","reply_to":9})");
	LG_EQUAL(fake.calledTool, S("send_message"));
	LG_EQUAL(fake.calledArguments.value(S("text")).toString(), S("hello"));
	LG_EQUAL(fake.calledArguments.value(S("reply_to")).toInt(), 9);

	const auto before = fake.calls;
	const auto bad = [&](const char *tool, const char *arguments) {
		LG_CHECK(!ErrorText(ToolReply(fake, tool, arguments)).isEmpty());
	};
	bad("get_messages", "{}");
	bad("get_messages", R"({"chat":""})");
	bad("get_messages", R"({"chat":"0"})");
	bad("get_messages", R"({"chat":"a b"})");
	bad("get_messages", R"({"chat":"https://t.me/x"})");
	bad("get_messages", R"({"chat":"durov","limit":101})");
	bad("get_messages", R"({"chat":"durov","before_id":0})");
	bad("send_message", R"({"chat":"durov"})");
	bad("send_message", R"({"chat":"durov","text":"   "})");
	bad("send_message", R"({"chat":"durov","text":5})");
	bad("send_message", R"({"text":"hi"})");
	bad("send_message", R"({"chat":"durov","text":"hi","reply_to":-1})");
	const auto tooLong = QByteArray(R"({"chat":"durov","text":")")
		+ QByteArray(4097, 'a') + "\"}";
	bad("send_message", tooLong.constData());
	LG_EQUAL(fake.calls, before);
}

LUCKGRAM_TEST(McpSendingIsItsOwnSwitchAndLimited) {
	auto fake = Fake();
	fake.enabled = { S("messages/111") };
	// Reading does not allow sending.
	LG_CHECK(ErrorText(ToolReply(
		fake,
		"send_message",
		R"({"chat":"durov","text":"hi"})")).contains(S("messages_send")));
	LG_EQUAL(fake.calls, 0);

	fake.enabled.push_back(S("messages_send/111"));
	for (auto i = 0; i != 30; ++i) {
		LG_CHECK(!ToolReply(
			fake,
			"send_message",
			R"({"chat":"durov","text":"hi"})").value(S("isError")).toBool());
		fake.now += 1000;
	}
	LG_CHECK(ErrorText(ToolReply(
		fake,
		"send_message",
		R"({"chat":"durov","text":"hi"})")).contains(S("Rate limit")));
	LG_EQUAL(fake.calls, 30);
	// Reading is not limited.
	LG_EQUAL(LimitOf(S("get_messages")).perHour, 0);
}

LUCKGRAM_TEST(McpCloudKeysAndIds) {
	const auto key = CloudKeyFromBytes(QByteArray(32, char(0xAB)));
	LG_EQUAL(int(key.size()), 47);
	LG_CHECK(key.startsWith(S("lgc_")));
	LG_CHECK(IsValidCloudKey(key));
	LG_CHECK(!IsValidCloudKey(key.mid(4))); // The local token is not one.
	LG_CHECK(!IsValidCloudKey(key + S("a")));
	LG_CHECK(!IsValidCloudKey(key.left(46) + S("=")));
	LG_CHECK(!IsValidCloudKey(QString()));
	LG_CHECK(CloudKeyFromBytes(QByteArray(31, 'a')).isEmpty());

	LG_CHECK(IsValidDeviceId(S("abcDEF_0-9")));
	LG_CHECK(!IsValidDeviceId(S("short")));
	LG_CHECK(!IsValidDeviceId(S("has space in it")));
	LG_CHECK(!IsValidDeviceId(QString(65, QChar('a'))));
}

LUCKGRAM_TEST(McpCloudPollRoundTrip) {
	auto reply = QJsonObject();
	reply.insert(S("jsonrpc"), S("2.0"));
	reply.insert(S("id"), 1);
	const auto body = J(CloudPollBody(
		S("device-01"),
		S("办公室电脑"),
		S("7.4.2"),
		{ { S("r5"), reply } }));
	LG_EQUAL(body.value(S("device_id")).toString(), S("device-01"));
	LG_EQUAL(body.value(S("device_name")).toString(), S("办公室电脑"));
	const auto results = body.value(S("results")).toArray();
	LG_EQUAL(int(results.size()), 1);
	LG_EQUAL(results.at(0).toObject().value(S("id")).toString(), S("r5"));
	LG_CHECK(results.at(0).toObject().value(S("response")).toObject() == reply);
	LG_CHECK(J(CloudPollBody(S("d"), S("n"), S("v"), {})).value(
		S("results")).toArray().isEmpty());

	auto ok = false;
	const auto requests = ParseCloudRequests(R"({"requests":[
		{"id":"r1","message":{"jsonrpc":"2.0","id":1,"method":"tools/list"}},
		{"id":"","message":{}},
		{"id":"r2","message":"text"},
		5,
		{"id":"r3","message":{"jsonrpc":"2.0","id":1,"method":"ping"}}]})", &ok);
	LG_CHECK(ok);
	LG_EQUAL(int(requests.size()), 2);
	LG_EQUAL(requests[0].id, S("r1"));
	LG_EQUAL(requests[0].message.value(S("method")).toString(), S("tools/list"));
	LG_EQUAL(requests[1].id, S("r3"));
	LG_CHECK(ParseCloudRequests(R"({"requests":[]})", &ok).empty());
	LG_CHECK(ok);
	// Not the relay: an error page, a login portal.
	LG_CHECK(ParseCloudRequests("<html>", &ok).empty());
	LG_CHECK(!ok);
	LG_CHECK(ParseCloudRequests(R"({"error":"x"})", &ok).empty());
	LG_CHECK(!ok);

	// What the relay hands over is answered like a local request.
	auto fake = Fake();
	const auto answer = HandleMessage(requests[0].message, fake.backend());
	LG_CHECK(answer.value(S("result")).toObject().contains(S("tools")));
}

LUCKGRAM_TEST(McpCloudRetryAndConfig) {
	LG_EQUAL(CloudRetryDelayMs(0), qint64(2000));
	LG_EQUAL(CloudRetryDelayMs(1), qint64(2000));
	LG_EQUAL(CloudRetryDelayMs(2), qint64(4000));
	LG_EQUAL(CloudRetryDelayMs(5), qint64(32000));
	LG_EQUAL(CloudRetryDelayMs(6), qint64(60000));
	LG_EQUAL(CloudRetryDelayMs(1000), qint64(60000));

	const auto key = CloudKeyFromBytes(QByteArray(32, 'k'));
	const auto server = J(CloudClientConfigJson(key).toUtf8()).value(
		S("mcpServers")).toObject().value(S("luckgram-cloud")).toObject();
	LG_EQUAL(server.value(S("url")).toString(), S("https://mcp.luckgram.cc/mcp"));
	LG_EQUAL(
		server.value(S("headers")).toObject().value(
			S("Authorization")).toString(),
		S("Bearer ") + key);
}

LUCKGRAM_TEST(McpGroupsProfileAndReadArguments) {
	auto fake = Fake();
	fake.enabled = {
		S("chats_manage/111"),
		S("groups/111"),
		S("profile/111"),
	};
	ToolReply(fake, "mark_chat_read", R"({"chat":"7000000000009"})");
	LG_EQUAL(fake.calledTool, S("mark_chat_read"));
	LG_EQUAL(fake.calledArguments.value(S("chat_id")).toString(), S("7000000000009"));
	LG_EQUAL(int(fake.calledArguments.size()), 1);
	ToolReply(fake, "join_chat", R"({"chat":"@some_channel"})");
	LG_EQUAL(fake.calledTool, S("join_chat"));
	LG_EQUAL(fake.calledArguments.value(S("chat_username")).toString(), S("some_channel"));
	ToolReply(fake, "leave_chat", R"({"chat":"some_channel"})");
	LG_EQUAL(fake.calledTool, S("leave_chat"));

	ToolReply(fake, "update_profile", R"({"first_name":" Li ","bio":""})");
	LG_EQUAL(fake.calledTool, S("update_profile"));
	LG_EQUAL(fake.calledArguments.value(S("first_name")).toString(), S("Li"));
	LG_CHECK(fake.calledArguments.contains(S("bio")));
	LG_EQUAL(fake.calledArguments.value(S("bio")).toString(), QString());
	LG_CHECK(!fake.calledArguments.contains(S("last_name")));

	const auto before = fake.calls;
	const auto bad = [&](const char *tool, const char *arguments) {
		LG_CHECK(!ErrorText(ToolReply(fake, tool, arguments)).isEmpty());
	};
	bad("mark_chat_read", "{}");
	bad("join_chat", R"({"chat":"https://t.me/+abcdef"})");
	bad("leave_chat", R"({"chat":""})");
	bad("update_profile", "{}");
	bad("update_profile", R"({"first_name":""})");
	bad("update_profile", R"({"first_name":5})");
	const auto longBio = QByteArray(R"({"bio":")") + QByteArray(71, 'a') + "\"}";
	bad("update_profile", longBio.constData());
	LG_EQUAL(fake.calls, before);

	// Each is its own switch, and every one of them is limited.
	auto other = Fake();
	other.enabled = { S("chats/111"), S("messages/111") };
	LG_CHECK(ErrorText(ToolReply(
		other,
		"mark_chat_read",
		R"({"chat":"durov"})")).contains(S("chats_manage")));
	LG_CHECK(ErrorText(ToolReply(
		other,
		"join_chat",
		R"({"chat":"durov"})")).contains(S("groups")));
	for (const auto tool : {
		"mark_chat_read",
		"join_chat",
		"leave_chat",
		"update_profile",
	}) {
		LG_CHECK(LimitOf(S(tool)).perHour > 0);
		LG_CHECK(LimitOf(S(tool)).perDay >= LimitOf(S(tool)).perHour);
	}
	// Every skill that changes something is marked so (asks when it goes
	// on), every read skill is not.
	for (const auto &skill : Skills()) {
		const auto key = S(skill.key);
		const auto read = (key == S("friends"))
			|| (key == S("chats"))
			|| (key == S("messages"));
		LG_EQUAL(skill.write, !read);
	}
}

LUCKGRAM_TEST(McpCloudBackupHost) {
	LG_EQUAL(CloudHostUrl(0), S("https://mcp.luckgram.cc"));
	LG_EQUAL(CloudHostUrl(1), S("https://lg-mcp.cat158.com"));
	LG_EQUAL(CloudHostUrl(7), S("https://mcp.luckgram.cc"));
	// One failed poll is not a blocked name.
	LG_EQUAL(NextCloudHost(0, 0), 0);
	LG_EQUAL(NextCloudHost(0, 1), 0);
	LG_EQUAL(NextCloudHost(0, 2), 1);
	// And back, when the backup is the one that does not answer.
	LG_EQUAL(NextCloudHost(1, 1), 1);
	LG_EQUAL(NextCloudHost(1, 2), 0);
}
