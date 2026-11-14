/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/mcp/luckgram_mcp_logic.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QList>
#include <QtCore/QStringList>

#include <cmath>

namespace Luckgram::Mcp {
namespace {

constexpr auto kParseError = -32700;
constexpr auto kInvalidRequest = -32600;
constexpr auto kMethodNotFound = -32601;
constexpr auto kInvalidParams = -32602;

// Newest first. The client's version is echoed when it is one of these.
constexpr const char *kProtocolVersions[] = {
	"2025-11-25",
	"2025-06-18",
	"2025-03-26",
	"2024-11-05",
};

[[nodiscard]] QString Q(const char *latin1) {
	return QString::fromLatin1(latin1);
}

[[nodiscard]] const char *StatusText(int status) {
	switch (status) {
	case 200: return "OK";
	case 202: return "Accepted";
	case 400: return "Bad Request";
	case 401: return "Unauthorized";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 411: return "Length Required";
	case 413: return "Content Too Large";
	case 415: return "Unsupported Media Type";
	case 431: return "Request Header Fields Too Large";
	}
	return "Error";
}

[[nodiscard]] Response Plain(int status, const char *text) {
	auto result = Response();
	result.status = status;
	result.contentType = "text/plain; charset=utf-8";
	result.body = QByteArray(text) + '\n';
	return result;
}

[[nodiscard]] Response Json(int status, const QJsonDocument &document) {
	auto result = Response();
	result.status = status;
	result.contentType = "application/json";
	result.body = document.toJson(QJsonDocument::Compact);
	return result;
}

[[nodiscard]] QJsonObject RpcError(
		const QJsonValue &id,
		int code,
		const QString &message) {
	auto error = QJsonObject();
	error.insert(Q("code"), code);
	error.insert(Q("message"), message);
	auto result = QJsonObject();
	result.insert(Q("jsonrpc"), Q("2.0"));
	result.insert(Q("id"), id);
	result.insert(Q("error"), error);
	return result;
}

[[nodiscard]] QJsonObject RpcResult(
		const QJsonValue &id,
		const QJsonObject &value) {
	auto result = QJsonObject();
	result.insert(Q("jsonrpc"), Q("2.0"));
	result.insert(Q("id"), id);
	result.insert(Q("result"), value);
	return result;
}

// "localhost", "127.0.0.1" or "[::1]", with any port: a page of another
// site that resolves its own name to 127.0.0.1 (DNS rebinding) sends its own
// name here. The port is not compared, so a forwarded port still works.
[[nodiscard]] bool IsLoopbackHost(QByteArray host) {
	host = host.trimmed().toLower();
	if (host.startsWith('[')) {
		const auto end = host.indexOf(']');
		if (end < 0) {
			return false;
		}
		const auto rest = host.mid(end + 1);
		if (!rest.isEmpty() && !rest.startsWith(':')) {
			return false;
		}
		return (host.left(end + 1) == "[::1]");
	}
	const auto colon = host.indexOf(':');
	const auto name = (colon < 0) ? host : host.left(colon);
	return (name == "localhost") || (name == "127.0.0.1");
}

[[nodiscard]] bool IsLoopbackOrigin(QByteArray origin) {
	origin = origin.trimmed().toLower();
	for (const auto scheme : { "http://", "https://" }) {
		if (origin.startsWith(scheme)) {
			return IsLoopbackHost(origin.mid(int(qstrlen(scheme))));
		}
	}
	return false;
}

[[nodiscard]] QJsonObject AccountJson(const AccountInfo &account) {
	auto result = QJsonObject();
	// A string: user ids don't fit the integers every JSON reader keeps.
	result.insert(Q("id"), QString::number(account.id));
	result.insert(Q("index"), account.index);
	result.insert(Q("name"), account.name);
	if (!account.username.isEmpty()) {
		result.insert(Q("username"), account.username);
	}
	if (!account.phone.isEmpty()) {
		result.insert(Q("phone"), account.phone);
	}
	result.insert(Q("unread"), account.unread);
	result.insert(Q("unread_muted"), account.unreadMuted);
	result.insert(Q("premium"), account.premium);
	result.insert(Q("active"), account.active);
	return result;
}

[[nodiscard]] QJsonObject ToolResult(const QJsonObject &value) {
	auto text = QJsonObject();
	text.insert(Q("type"), Q("text"));
	text.insert(Q("text"), QString::fromUtf8(
		QJsonDocument(value).toJson(QJsonDocument::Compact)));
	auto content = QJsonArray();
	content.push_back(text);
	auto result = QJsonObject();
	result.insert(Q("content"), content);
	result.insert(Q("structuredContent"), value);
	result.insert(Q("isError"), false);
	return result;
}

[[nodiscard]] QJsonObject ToolError(const QString &message) {
	auto text = QJsonObject();
	text.insert(Q("type"), Q("text"));
	text.insert(Q("text"), message);
	auto content = QJsonArray();
	content.push_back(text);
	auto result = QJsonObject();
	result.insert(Q("content"), content);
	result.insert(Q("isError"), true);
	return result;
}

[[nodiscard]] QJsonObject Property(
		const char *type,
		const char *description) {
	auto result = QJsonObject();
	result.insert(Q("type"), Q(type));
	result.insert(Q("description"), Q(description));
	return result;
}

[[nodiscard]] QJsonObject Tool(
		const char *name,
		const char *title,
		const char *description,
		const QJsonObject &properties,
		bool readOnly) {
	auto schema = QJsonObject();
	schema.insert(Q("type"), Q("object"));
	schema.insert(Q("properties"), properties);
	schema.insert(Q("additionalProperties"), false);
	auto annotations = QJsonObject();
	annotations.insert(Q("title"), Q(title));
	annotations.insert(Q("readOnlyHint"), readOnly);
	annotations.insert(Q("destructiveHint"), false);
	annotations.insert(Q("idempotentHint"), true);
	annotations.insert(Q("openWorldHint"), false);
	auto result = QJsonObject();
	result.insert(Q("name"), Q(name));
	result.insert(Q("title"), Q(title));
	result.insert(Q("description"), Q(description));
	result.insert(Q("inputSchema"), schema);
	result.insert(Q("annotations"), annotations);
	return result;
}

[[nodiscard]] QJsonArray Tools() {
	auto result = QJsonArray();
	result.push_back(Tool(
		"list_accounts",
		"List accounts",
		"List the Telegram accounts logged in to this Luckgram Desktop, in "
		"the order of the account switcher: id (Telegram user id, as a "
		"string), index (1-based), name, username, phone, unread count and "
		"which one is active.",
		QJsonObject(),
		true));
	result.push_back(Tool(
		"get_active_account",
		"Get active account",
		"The account the main Luckgram Desktop window shows right now.",
		QJsonObject(),
		true));
	auto properties = QJsonObject();
	properties.insert(Q("id"), Property(
		"string",
		"Telegram user id from list_accounts. Preferred: it never changes."));
	properties.insert(Q("username"), Property(
		"string",
		"Username of the account, with or without @."));
	properties.insert(Q("phone"), Property(
		"string",
		"Phone number of the account; only the digits are compared."));
	properties.insert(Q("index"), Property(
		"integer",
		"1-based position from list_accounts. It changes when accounts are "
		"added, removed or reordered."));
	result.push_back(Tool(
		"switch_account",
		"Switch account",
		"Make another logged-in account the active one in Luckgram Desktop, "
		"like clicking it in the account switcher. Give exactly one of id, "
		"username, phone or index.",
		properties,
		false));
	return result;
}

[[nodiscard]] QString Digits(const QString &text) {
	auto result = QString();
	for (const auto ch : text) {
		if (ch >= QChar('0') && ch <= QChar('9')) {
			result.push_back(ch);
		}
	}
	return result;
}

[[nodiscard]] QJsonObject Initialize(const QJsonObject &params) {
	const auto requested = params.value(Q("protocolVersion")).toString();
	auto version = Q(kProtocolVersions[0]);
	for (const auto known : kProtocolVersions) {
		if (requested == Q(known)) {
			version = requested;
			break;
		}
	}
	auto tools = QJsonObject();
	tools.insert(Q("listChanged"), false);
	auto capabilities = QJsonObject();
	capabilities.insert(Q("tools"), tools);
	auto info = QJsonObject();
	info.insert(Q("name"), Q("luckgram-desktop"));
	info.insert(Q("title"), Q("Luckgram Desktop"));
	info.insert(Q("version"), Q("1.0.0"));
	auto result = QJsonObject();
	result.insert(Q("protocolVersion"), version);
	result.insert(Q("capabilities"), capabilities);
	result.insert(Q("serverInfo"), info);
	result.insert(Q("instructions"), Q(
		"Controls which Telegram account is active in the Luckgram Desktop "
		"app running on this computer. Call list_accounts first, then "
		"switch_account with the id of the account you need."));
	return result;
}

[[nodiscard]] QJsonObject CallTool(
		const QJsonValue &id,
		const QJsonObject &params,
		const Backend &backend) {
	const auto name = params.value(Q("name")).toString();
	const auto arguments = params.value(Q("arguments")).toObject();
	const auto list = (name == Q("list_accounts"));
	const auto active = (name == Q("get_active_account"));
	const auto change = (name == Q("switch_account"));
	if (!list && !active && !change) {
		return RpcError(id, kInvalidParams, Q("Unknown tool: ") + name);
	}
	if (backend.locked()) {
		return RpcResult(id, ToolError(Q(
			"Luckgram Desktop is locked with a passcode. Unlock it on the "
			"computer first.")));
	}
	const auto accounts = backend.accounts();
	if (list) {
		auto array = QJsonArray();
		for (const auto &account : accounts) {
			array.push_back(AccountJson(account));
		}
		auto value = QJsonObject();
		value.insert(Q("accounts"), array);
		return RpcResult(id, ToolResult(value));
	} else if (active) {
		for (const auto &account : accounts) {
			if (account.active) {
				auto value = QJsonObject();
				value.insert(Q("account"), AccountJson(account));
				return RpcResult(id, ToolResult(value));
			}
		}
		return RpcResult(id, ToolError(Q(
			"No account is active: Luckgram Desktop shows a login screen.")));
	}
	auto error = QString();
	const auto userId = ResolveAccount(accounts, arguments, &error);
	if (!userId) {
		return RpcResult(id, ToolError(error));
	}
	for (const auto &account : accounts) {
		if (account.id == userId && account.active) {
			auto value = QJsonObject();
			value.insert(Q("switched"), false);
			value.insert(Q("already_active"), true);
			value.insert(Q("account"), AccountJson(account));
			return RpcResult(id, ToolResult(value));
		}
	}
	error = backend.activate(userId);
	if (!error.isEmpty()) {
		return RpcResult(id, ToolError(error));
	}
	// Read again: what is active now is the answer, not what was asked.
	for (const auto &account : backend.accounts()) {
		if (account.id == userId) {
			if (!account.active) {
				break;
			}
			auto value = QJsonObject();
			value.insert(Q("switched"), true);
			value.insert(Q("already_active"), false);
			value.insert(Q("account"), AccountJson(account));
			return RpcResult(id, ToolResult(value));
		}
	}
	return RpcResult(id, ToolError(Q(
		"The account was not switched: Luckgram Desktop is asking the user "
		"to confirm something first (for example leaving an ongoing call). "
		"Call get_active_account to check again.")));
}

} // namespace

QByteArray Request::header(const QByteArray &lowerName) const {
	for (const auto &[name, value] : headers) {
		if (name == lowerName) {
			return value;
		}
	}
	return QByteArray();
}

ParseResult ParseRequest(
		const QByteArray &buffer,
		Request *request,
		int *errorStatus) {
	const auto bad = [&](int status) {
		*errorStatus = status;
		return ParseResult::Bad;
	};
	const auto headerEnd = int(buffer.indexOf("\r\n\r\n"));
	if (headerEnd < 0) {
		return (buffer.size() > kMaxHeaderBytes)
			? bad(431)
			: ParseResult::Incomplete;
	} else if (headerEnd > kMaxHeaderBytes) {
		return bad(431);
	}
	const auto lines = buffer.left(headerEnd).split('\n');
	auto result = Request();
	auto contentLength = -1;
	for (auto i = 0; i != int(lines.size()); ++i) {
		auto line = lines[i];
		if (line.endsWith('\r')) {
			line.chop(1);
		}
		if (!i) {
			const auto parts = line.split(' ');
			if (parts.size() != 3
				|| parts[0].isEmpty()
				|| !parts[1].startsWith('/')
				|| !parts[2].startsWith("HTTP/1.")) {
				return bad(400);
			}
			result.method = parts[0].toUpper();
			const auto query = int(parts[1].indexOf('?'));
			result.path = (query < 0) ? parts[1] : parts[1].left(query);
			continue;
		}
		const auto colon = int(line.indexOf(':'));
		if (colon <= 0) {
			return bad(400);
		}
		const auto name = line.left(colon).trimmed().toLower();
		const auto value = line.mid(colon + 1).trimmed();
		if (name == "transfer-encoding") {
			return bad(411); // Only bodies with a Content-Length.
		} else if (name == "content-length") {
			auto ok = false;
			const auto length = value.toLongLong(&ok);
			if (!ok
				|| length < 0
				|| (contentLength >= 0 && contentLength != length)) {
				return bad(400);
			} else if (length > kMaxBodyBytes) {
				return bad(413);
			}
			contentLength = int(length);
		}
		result.headers.emplace_back(name, value);
	}
	const auto bodyStart = headerEnd + 4;
	const auto bodyLength = (contentLength < 0) ? 0 : contentLength;
	if (int(buffer.size()) < bodyStart + bodyLength) {
		return ParseResult::Incomplete;
	}
	result.body = buffer.mid(bodyStart, bodyLength);
	*request = std::move(result);
	return ParseResult::Ready;
}

QByteArray Serialize(const Response &response) {
	auto result = QByteArray();
	result += "HTTP/1.1 " + QByteArray::number(response.status) + ' '
		+ StatusText(response.status) + "\r\n";
	if (!response.contentType.isEmpty()) {
		result += "Content-Type: " + response.contentType + "\r\n";
	}
	result += "Content-Length: " + QByteArray::number(response.body.size())
		+ "\r\n";
	for (const auto &[name, value] : response.headers) {
		result += name + ": " + value + "\r\n";
	}
	result += "Cache-Control: no-store\r\n"
		"X-Content-Type-Options: nosniff\r\n"
		"Connection: close\r\n"
		"\r\n";
	result += response.body;
	return result;
}

Response Handle(
		const Request &request,
		const Config &config,
		const Backend &backend) {
	// No CORS answer anywhere: a web page can't read a reply, and with the
	// checks below it can't even make a request that is looked at.
	if (!IsLoopbackHost(request.header("host"))) {
		return Plain(403, "Forbidden host.");
	}
	const auto origin = request.header("origin");
	if (!origin.isEmpty() && !IsLoopbackOrigin(origin)) {
		return Plain(403, "Forbidden origin.");
	}
	auto path = request.path;
	if (path.size() > 1 && path.endsWith('/')) {
		path.chop(1);
	}
	if (path != kPath) {
		return Plain(404, "Not found. The MCP endpoint is /mcp.");
	}
	const auto authorization = request.header("authorization");
	const auto prefix = QByteArray("bearer ");
	if (!IsValidToken(config.token)
		|| authorization.left(prefix.size()).toLower() != prefix
		|| !SameToken(
			authorization.mid(prefix.size()).trimmed(),
			config.token.toLatin1())) {
		auto result = Plain(401, "Missing or wrong access token.");
		result.headers.emplace_back("WWW-Authenticate", "Bearer");
		return result;
	}
	if (request.method != "POST") {
		// GET would open the optional server-to-client stream; there is
		// nothing to send on it.
		auto result = Plain(405, "Use POST.");
		result.headers.emplace_back("Allow", "POST");
		return result;
	}
	const auto type = request.header("content-type").toLower();
	if (!type.startsWith("application/json")) {
		return Plain(415, "Content-Type must be application/json.");
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(request.body, &error);
	if (error.error != QJsonParseError::NoError) {
		return Json(400, QJsonDocument(RpcError(
			QJsonValue(QJsonValue::Null),
			kParseError,
			Q("Parse error: ") + error.errorString())));
	}
	if (document.isObject()) {
		const auto reply = HandleMessage(document.object(), backend);
		if (reply.isEmpty()) {
			auto result = Response();
			result.status = 202;
			return result;
		}
		return Json(200, QJsonDocument(reply));
	}
	const auto batch = document.array();
	if (batch.isEmpty()) {
		return Json(400, QJsonDocument(RpcError(
			QJsonValue(QJsonValue::Null),
			kInvalidRequest,
			Q("Invalid request."))));
	}
	auto replies = QJsonArray();
	for (const auto &entry : batch) {
		const auto reply = entry.isObject()
			? HandleMessage(entry.toObject(), backend)
			: RpcError(
				QJsonValue(QJsonValue::Null),
				kInvalidRequest,
				Q("Invalid request."));
		if (!reply.isEmpty()) {
			replies.push_back(reply);
		}
	}
	if (replies.isEmpty()) {
		auto result = Response();
		result.status = 202;
		return result;
	}
	return Json(200, QJsonDocument(replies));
}

QJsonObject HandleMessage(
		const QJsonObject &message,
		const Backend &backend) {
	const auto id = message.value(Q("id"));
	const auto hasId = id.isString() || id.isDouble();
	const auto method = message.value(Q("method"));
	if (!method.isString()) {
		// A response to something this server never asked: dropped.
		if (message.contains(Q("result")) || message.contains(Q("error"))) {
			return QJsonObject();
		}
		return RpcError(
			hasId ? id : QJsonValue(QJsonValue::Null),
			kInvalidRequest,
			Q("Invalid request."));
	} else if (!hasId) {
		return QJsonObject(); // A notification.
	} else if (message.value(Q("jsonrpc")).toString() != Q("2.0")) {
		return RpcError(id, kInvalidRequest, Q("Invalid request."));
	}
	const auto name = method.toString();
	const auto params = message.value(Q("params")).toObject();
	if (name == Q("initialize")) {
		return RpcResult(id, Initialize(params));
	} else if (name == Q("ping")) {
		return RpcResult(id, QJsonObject());
	} else if (name == Q("tools/list")) {
		auto result = QJsonObject();
		result.insert(Q("tools"), Tools());
		return RpcResult(id, result);
	} else if (name == Q("tools/call")) {
		return CallTool(id, params, backend);
	}
	return RpcError(id, kMethodNotFound, Q("Method not found: ") + name);
}

quint64 ResolveAccount(
		const std::vector<AccountInfo> &accounts,
		const QJsonObject &arguments,
		QString *error) {
	const auto fail = [&](const char *text) {
		*error = Q(text);
		return quint64(0);
	};
	const auto notFound = [&] {
		return fail(
			"No such account is logged in. Call list_accounts to see the "
			"accounts.");
	};
	auto given = 0;
	for (const auto key : { "id", "username", "phone", "index" }) {
		const auto value = arguments.value(Q(key));
		if (!value.isUndefined() && !value.isNull()) {
			++given;
		}
	}
	if (given != 1) {
		return fail("Give exactly one of id, username, phone or index.");
	}
	const auto id = arguments.value(Q("id"));
	const auto username = arguments.value(Q("username"));
	const auto phone = arguments.value(Q("phone"));
	const auto index = arguments.value(Q("index"));
	if (id.isString() || id.isDouble()) {
		auto wanted = quint64(0);
		if (id.isString()) {
			auto ok = false;
			wanted = id.toString().trimmed().toULongLong(&ok);
			if (!ok) {
				wanted = 0;
			}
		} else {
			// Exact only below 2^53; larger ids must come as strings.
			const auto number = id.toDouble();
			if (number > 0.
				&& number < 9007199254740992.
				&& std::floor(number) == number) {
				wanted = quint64(number);
			}
		}
		if (!wanted) {
			return fail("id must be a Telegram user id.");
		}
		for (const auto &account : accounts) {
			if (account.id == wanted) {
				return wanted;
			}
		}
		return notFound();
	} else if (username.isString()) {
		auto wanted = username.toString().trimmed();
		if (wanted.startsWith(QChar('@'))) {
			wanted = wanted.mid(1);
		}
		if (wanted.isEmpty()) {
			return fail("username is empty.");
		}
		for (const auto &account : accounts) {
			if (!account.username.compare(wanted, Qt::CaseInsensitive)) {
				return account.id;
			}
		}
		return notFound();
	} else if (phone.isString()) {
		const auto wanted = Digits(phone.toString());
		if (wanted.isEmpty()) {
			return fail("phone has no digits.");
		}
		for (const auto &account : accounts) {
			if (Digits(account.phone) == wanted) {
				return account.id;
			}
		}
		return notFound();
	} else if (index.isDouble()) {
		const auto number = index.toDouble();
		if (std::floor(number) != number) {
			return fail("index must be a whole number.");
		}
		for (const auto &account : accounts) {
			if (double(account.index) == number) {
				return account.id;
			}
		}
		return notFound();
	}
	return fail(
		"Wrong type: id, username and phone are strings, index is a "
		"number.");
}

bool IsValidToken(const QString &token) {
	if (token.size() < 32 || token.size() > 128) {
		return false;
	}
	for (const auto ch : token) {
		const auto code = ch.unicode();
		const auto good = (code >= 'a' && code <= 'z')
			|| (code >= 'A' && code <= 'Z')
			|| (code >= '0' && code <= '9')
			|| (code == '-')
			|| (code == '_');
		if (!good) {
			return false;
		}
	}
	return true;
}

bool SameToken(const QByteArray &a, const QByteArray &b) {
	if (a.size() != b.size()) {
		return false;
	}
	auto difference = 0;
	for (auto i = 0; i != int(a.size()); ++i) {
		difference |= (uchar(a[i]) ^ uchar(b[i]));
	}
	return (difference == 0);
}

QString ServerUrl(int port) {
	return Q("http://127.0.0.1:") + QString::number(port) + Q(kPath);
}

QString ClientConfigJson(const Config &config) {
	auto headers = QJsonObject();
	headers.insert(Q("Authorization"), Q("Bearer ") + config.token);
	auto server = QJsonObject();
	server.insert(Q("type"), Q("http"));
	server.insert(Q("url"), ServerUrl(config.port));
	server.insert(Q("headers"), headers);
	auto servers = QJsonObject();
	servers.insert(Q("luckgram"), server);
	auto root = QJsonObject();
	root.insert(Q("mcpServers"), servers);
	return QString::fromUtf8(
		QJsonDocument(root).toJson(QJsonDocument::Indented)).trimmed();
}

} // namespace Luckgram::Mcp
