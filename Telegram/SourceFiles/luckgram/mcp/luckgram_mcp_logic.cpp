/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/mcp/luckgram_mcp_logic.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QList>
#include <QtCore/QStringList>

#include <algorithm>
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
	// The charset is said although JSON is always UTF-8: some clients
	// (Windows PowerShell) read names as Latin-1 without it.
	result.contentType = "application/json; charset=utf-8";
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

	const auto account = Property(
		"string",
		"Telegram user id of the logged-in account to use, from "
		"list_accounts. Default: the active account. The account does not "
		"have to be the active one.");
	const auto limit = [](const char *text) {
		return Property("integer", text);
	};
	const auto offset = Property(
		"integer",
		"How many entries to skip, for the next page. Default 0.");

	properties = QJsonObject();
	properties.insert(Q("account"), account);
	properties.insert(Q("query"), Property(
		"string",
		"Only friends whose name or username contains this text."));
	properties.insert(Q("limit"), limit("At most this many, 1-1000. Default 200."));
	properties.insert(Q("offset"), offset);
	result.push_back(Tool(
		"list_friends",
		"List friends",
		"Skill \"friends\". The contacts of an account: id, name, username, "
		"phone (when the account can see it), bot and mutual flags, with "
		"the total count. Needs the friends skill switched on for the "
		"account in Luckgram Desktop.",
		properties,
		true));

	const auto username = Property(
		"string",
		"Username, with or without @.");
	const auto phone = Property(
		"string",
		"Phone number with the country code; only the digits are used.");
	const auto userId = Property(
		"string",
		"Telegram user id, from list_friends, list_chats or find_user.");

	properties = QJsonObject();
	properties.insert(Q("account"), account);
	properties.insert(Q("username"), username);
	properties.insert(Q("phone"), phone);
	result.push_back(Tool(
		"find_user",
		"Find user",
		"Skill \"friends\". Look a user up by exactly one of username or "
		"phone, as seen from the account: id, name, username, whether it "
		"is already a friend. A phone is found only when that user lets "
		"the account find them by number. At most 30 lookups an hour per "
		"account.",
		properties,
		true));

	properties = QJsonObject();
	properties.insert(Q("account"), account);
	properties.insert(Q("user_id"), userId);
	properties.insert(Q("username"), username);
	properties.insert(Q("phone"), phone);
	properties.insert(Q("first_name"), Property(
		"string",
		"Name to save the contact under. Default: the user's own name."));
	properties.insert(Q("last_name"), Property(
		"string",
		"Last name to save the contact under. Default: the user's own."));
	result.push_back(Tool(
		"add_friend",
		"Add friend",
		"Skill \"friends_manage\". Add a user to the contacts of the "
		"account, by exactly one of user_id, username or phone. Sends no "
		"message. At most 5 an hour and 20 a day per account; over the "
		"limit the call is refused and says how long to wait.",
		properties,
		false));

	properties = QJsonObject();
	properties.insert(Q("account"), account);
	properties.insert(Q("user_id"), userId);
	properties.insert(Q("username"), username);
	result.push_back(Tool(
		"remove_friend",
		"Remove friend",
		"Skill \"friends_manage\". Remove a user from the contacts of the "
		"account, by exactly one of user_id or username. The chat with "
		"them stays. At most 20 an hour and 50 a day per account.",
		properties,
		false));

	properties = QJsonObject();
	properties.insert(Q("account"), account);
	properties.insert(Q("unread_only"), Property(
		"boolean",
		"Only chats with unread messages. Default false."));
	properties.insert(Q("limit"), limit("At most this many, 1-200. Default 50."));
	properties.insert(Q("offset"), offset);
	result.push_back(Tool(
		"list_chats",
		"List chats",
		"Skill \"chats\". The chat list of an account in its own order "
		"(pinned first, then by last message): id, type (user, bot, group, "
		"supergroup, channel), name, username, unread count, muted, pinned "
		"and the time of the last message. No message texts. Needs the "
		"chats skill switched on for the account in Luckgram Desktop.",
		properties,
		true));

	const auto chat = Property(
		"string",
		"The chat: its id from list_chats, or a username (with or without "
		"@) of a user, group or channel.");

	properties = QJsonObject();
	properties.insert(Q("account"), account);
	properties.insert(Q("chat"), chat);
	properties.insert(Q("limit"), limit("At most this many, 1-100. Default 20."));
	properties.insert(Q("before_id"), Property(
		"integer",
		"Only messages older than this message id, for the next page. "
		"Default: from the newest."));
	result.push_back(Tool(
		"get_messages",
		"Get messages",
		"Skill \"messages\". The latest messages of a chat, newest first: "
		"id, date, sender, out, text, kind of attachment, the id it "
		"replies to. Reading them this way does not mark the chat as "
		"read.",
		properties,
		true));

	properties = QJsonObject();
	properties.insert(Q("account"), account);
	properties.insert(Q("chat"), chat);
	properties.insert(Q("text"), Property(
		"string",
		"The message text, 1-4096 characters, sent as plain text."));
	properties.insert(Q("reply_to"), Property(
		"integer",
		"Id of the message in this chat to reply to. Optional."));
	result.push_back(Tool(
		"send_message",
		"Send message",
		"Skill \"messages_send\". Send one text message from the account "
		"to one chat, through the app's normal sending. At most 30 an hour "
		"and 200 a day per account; over the limit the call is refused "
		"and says how long to wait.",
		properties,
		false));

	properties = QJsonObject();
	properties.insert(Q("account"), account);
	properties.insert(Q("chat"), chat);
	result.push_back(Tool(
		"mark_chat_read",
		"Mark chat read",
		"Skill \"chats_manage\". Mark every message of one chat as read "
		"for the account. At most 60 an hour and 500 a day per account.",
		properties,
		false));

	properties = QJsonObject();
	properties.insert(Q("account"), account);
	properties.insert(Q("chat"), Property(
		"string",
		"The public username of the group or channel, with or without @, "
		"or its id from list_chats."));
	result.push_back(Tool(
		"join_chat",
		"Join group or channel",
		"Skill \"groups\". Join one public group or channel as the "
		"account. At most 5 an hour and 20 a day per account.",
		properties,
		false));
	result.push_back(Tool(
		"leave_chat",
		"Leave group or channel",
		"Skill \"groups\". Leave one supergroup or channel the account is "
		"in. At most 10 an hour and 30 a day per account.",
		properties,
		false));

	properties = QJsonObject();
	properties.insert(Q("account"), account);
	properties.insert(Q("first_name"), Property(
		"string",
		"New first name, 1-64 characters."));
	properties.insert(Q("last_name"), Property(
		"string",
		"New last name, up to 64 characters; empty removes it."));
	properties.insert(Q("bio"), Property(
		"string",
		"New bio, up to 70 characters; empty removes it."));
	result.push_back(Tool(
		"update_profile",
		"Update profile",
		"Skill \"profile\". Change the name and / or the bio of the "
		"account; what is left out stays. At most 5 an hour and 10 a day "
		"per account.",
		properties,
		false));

	const auto bot = Property(
		"string",
		"Username of one of the account's bots, with or without @.");
	const auto bf = [&](
			const char *name,
			const char *title,
			const char *description,
			const QJsonObject &props,
			bool readOnly) {
		auto all = props;
		all.insert(Q("account"), account);
		result.push_back(Tool(name, title, description, all, readOnly));
	};
	bf("botfather_list_bots", "List my bots",
		"Skill \"botfather\". The bots this account owns, asked from "
		"@BotFather (/mybots): their usernames.",
		QJsonObject(), true);
	properties = QJsonObject();
	properties.insert(Q("name"), Property(
		"string",
		"Display name of the new bot, 1-64 characters."));
	properties.insert(Q("username"), Property(
		"string",
		"Username of the new bot: 5-32 letters, digits and underscores, "
		"ending in \"bot\"."));
	bf("botfather_create_bot", "Create a bot",
		"Skill \"botfather\". Create a new bot with @BotFather (/newbot) "
		"and return its username and its API token. The token is a "
		"secret: whoever has it controls the bot. At most 3 an hour and "
		"10 a day per account.",
		properties, false);
	properties = QJsonObject();
	properties.insert(Q("bot"), bot);
	bf("botfather_get_token", "Get a bot's token",
		"Skill \"botfather\". The current API token of one of the "
		"account's bots (/token). A secret.",
		properties, false);
	bf("botfather_revoke_token", "Revoke a bot's token",
		"Skill \"botfather\". Replace the API token of a bot with a new "
		"one (/revoke). The old token stops working at once: whatever "
		"runs the bot must get the new one.",
		properties, false);
	properties.insert(Q("name"), Property(
		"string",
		"New display name, 1-64 characters."));
	bf("botfather_set_name", "Set a bot's name",
		"Skill \"botfather\". Change the display name of a bot (/setname).",
		properties, false);
	properties = QJsonObject();
	properties.insert(Q("bot"), bot);
	properties.insert(Q("description"), Property(
		"string",
		"What people see in the empty chat with the bot, 1-512 characters."));
	bf("botfather_set_description", "Set a bot's description",
		"Skill \"botfather\". Change the description of a bot "
		"(/setdescription): the text shown before a chat with it starts.",
		properties, false);
	properties = QJsonObject();
	properties.insert(Q("bot"), bot);
	properties.insert(Q("about"), Property(
		"string",
		"The short text on the bot's profile page, 1-120 characters."));
	bf("botfather_set_about", "Set a bot's about text",
		"Skill \"botfather\". Change the about text of a bot "
		"(/setabouttext).",
		properties, false);
	properties = QJsonObject();
	properties.insert(Q("bot"), bot);
	{
		auto command = QJsonObject();
		command.insert(Q("command"), Property(
			"string",
			"1-32 lowercase letters, digits and underscores, no slash."));
		command.insert(Q("description"), Property(
			"string",
			"What the command does, 1-256 characters."));
		auto item = QJsonObject();
		item.insert(Q("type"), Q("object"));
		item.insert(Q("properties"), command);
		auto list = QJsonObject();
		list.insert(Q("type"), Q("array"));
		list.insert(Q("items"), item);
		list.insert(Q("description"), Q(
			"The whole command list, at most 100. An empty list removes "
			"all commands."));
		properties.insert(Q("commands"), list);
	}
	bf("botfather_set_commands", "Set a bot's commands",
		"Skill \"botfather\". Replace the command list of a bot "
		"(/setcommands): what people see when they type / in a chat "
		"with it.",
		properties, false);
	properties = QJsonObject();
	properties.insert(Q("bot"), bot);
	properties.insert(Q("enabled"), Property(
		"boolean",
		"true: the bot sees only commands and replies to it in groups "
		"(privacy mode, the default). false: it sees every message."));
	bf("botfather_set_privacy", "Set a bot's group privacy",
		"Skill \"botfather\". Switch the privacy mode of a bot in groups "
		"(/setprivacy).",
		properties, false);
	properties = QJsonObject();
	properties.insert(Q("bot"), bot);
	properties.insert(Q("enabled"), Property(
		"boolean",
		"true: the bot can be added to groups. false: it cannot."));
	bf("botfather_set_join_groups", "Allow a bot in groups",
		"Skill \"botfather\". Allow or forbid adding a bot to groups "
		"(/setjoingroups).",
		properties, false);
	properties = QJsonObject();
	properties.insert(Q("bot"), bot);
	properties.insert(Q("confirm"), Property(
		"string",
		"The bot's username once more, exactly: deleting cannot be "
		"undone."));
	bf("botfather_delete_bot", "Delete a bot",
		"Skill \"botfather\". Delete one of the account's bots for good "
		"(/deletebot). Its username becomes free and its token stops "
		"working. Ask the user before calling this. At most 3 an hour "
		"and 10 a day per account.",
		properties, false);
	properties = QJsonObject();
	properties.insert(Q("text"), Property(
		"string",
		"What to send to @BotFather, 1-4096 characters: a command such "
		"as /mybots or /setuserpic, or the answer to its last question."));
	bf("botfather_send", "Send to BotFather",
		"Skill \"botfather\". Send one message to @BotFather and return "
		"its answer with the buttons under it. For everything the other "
		"botfather tools do not cover: read what it asks, then answer "
		"with this tool or press a button with botfather_press.",
		properties, false);
	properties = QJsonObject();
	properties.insert(Q("message_id"), Property(
		"integer",
		"Id of the message of @BotFather that has the button, from "
		"botfather_send, botfather_press or botfather_read."));
	properties.insert(Q("button"), Property(
		"string",
		"The text on the button, exactly as returned."));
	bf("botfather_press", "Press a BotFather button",
		"Skill \"botfather\". Press a button under a message of "
		"@BotFather and return what the message says afterwards.",
		properties, false);
	properties = QJsonObject();
	properties.insert(Q("limit"), limit("At most this many, 1-20. Default 5."));
	bf("botfather_read", "Read the BotFather chat",
		"Skill \"botfather\". The latest messages of the account's chat "
		"with @BotFather, newest first, with their buttons. Only that "
		"chat: this skill sees no other chat.",
		properties, true);
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

// Which skill a tool belongs to, or nullptr for the account tools.
[[nodiscard]] const char *SkillOf(const QString &tool) {
	if (tool == Q("list_friends") || tool == Q("find_user")) {
		return kSkillFriends;
	} else if (tool == Q("add_friend") || tool == Q("remove_friend")) {
		return kSkillFriendsManage;
	} else if (tool == Q("list_chats")) {
		return kSkillChats;
	} else if (tool == Q("get_messages")) {
		return kSkillMessages;
	} else if (tool == Q("send_message")) {
		return kSkillMessagesSend;
	} else if (tool == Q("mark_chat_read")) {
		return kSkillChatsManage;
	} else if (tool == Q("join_chat") || tool == Q("leave_chat")) {
		return kSkillGroups;
	} else if (tool == Q("update_profile")) {
		return kSkillProfile;
	} else if (tool.startsWith(Q("botfather_"))) {
		return kSkillBotFather;
	}
	return nullptr;
}

// A whole number argument within [min, max], or the default when absent.
[[nodiscard]] bool IntArgument(
		const QJsonObject &arguments,
		const char *key,
		int def,
		int min,
		int max,
		int *result,
		QString *error) {
	const auto value = arguments.value(Q(key));
	if (value.isUndefined() || value.isNull()) {
		*result = def;
		return true;
	}
	const auto number = value.toDouble(-1.);
	if (!value.isDouble()
		|| std::floor(number) != number
		|| number < double(min)
		|| number > double(max)) {
		*error = Q(key)
			+ Q(" must be a whole number from ")
			+ QString::number(min)
			+ Q(" to ")
			+ QString::number(max)
			+ Q(".");
		return false;
	}
	*result = int(number);
	return true;
}

// The arguments of a skill tool, checked, with the defaults filled in and
// without "account". An empty result with *error set when they are wrong.
[[nodiscard]] QJsonObject SkillArguments(
		const QString &tool,
		const QJsonObject &arguments,
		QString *error) {
	auto result = QJsonObject();
	if (tool == Q("find_user")
		|| tool == Q("add_friend")
		|| tool == Q("remove_friend")) {
		// Who: exactly one way of naming the user.
		const auto find = (tool == Q("find_user"));
		const auto remove = (tool == Q("remove_friend"));
		auto given = 0;
		for (const auto key : { "user_id", "username", "phone" }) {
			const auto value = arguments.value(Q(key));
			if (value.isUndefined() || value.isNull()) {
				continue;
			} else if ((find && Q(key) == Q("user_id"))
				|| (remove && Q(key) == Q("phone"))) {
				*error = Q(key) + Q(" is not accepted by this tool.");
				return QJsonObject();
			}
			++given;
		}
		if (given != 1) {
			*error = find
				? Q("Give exactly one of username or phone.")
				: remove
				? Q("Give exactly one of user_id or username.")
				: Q("Give exactly one of user_id, username or phone.");
			return QJsonObject();
		}
		const auto id = arguments.value(Q("user_id"));
		const auto username = arguments.value(Q("username"));
		const auto phone = arguments.value(Q("phone"));
		if (id.isString() || id.isDouble()) {
			auto ok = false;
			const auto number = id.isString()
				? id.toString().trimmed().toULongLong(&ok)
				: quint64(0);
			if (id.isDouble()) {
				const auto value = id.toDouble();
				ok = (value > 0.
					&& value < 9007199254740992.
					&& std::floor(value) == value);
			}
			const auto parsed = id.isString() ? number : quint64(id.toDouble());
			if (!ok || !parsed) {
				*error = Q("user_id must be a Telegram user id.");
				return QJsonObject();
			}
			result.insert(Q("user_id"), QString::number(parsed));
		} else if (username.isString()) {
			auto name = username.toString().trimmed();
			if (name.startsWith(QChar('@'))) {
				name = name.mid(1);
			}
			auto good = (name.size() >= 4 && name.size() <= 32);
			for (const auto ch : name) {
				const auto code = ch.unicode();
				good = good && ((code >= 'a' && code <= 'z')
					|| (code >= 'A' && code <= 'Z')
					|| (code >= '0' && code <= '9')
					|| (code == '_'));
			}
			if (!good) {
				*error = Q("username is not a valid Telegram username.");
				return QJsonObject();
			}
			result.insert(Q("username"), name);
		} else if (phone.isString()) {
			const auto digits = Digits(phone.toString());
			if (digits.size() < 7 || digits.size() > 15) {
				*error = Q("phone must be a full number with the country code.");
				return QJsonObject();
			}
			result.insert(Q("phone"), digits);
		} else {
			*error = Q("user_id, username and phone are strings.");
			return QJsonObject();
		}
		for (const auto key : { "first_name", "last_name" }) {
			const auto value = arguments.value(Q(key));
			if (value.isUndefined() || value.isNull()) {
				continue;
			} else if (tool != Q("add_friend") || !value.isString()) {
				*error = Q(key) + Q(" must be a string, for add_friend only.");
				return QJsonObject();
			}
			result.insert(Q(key), value.toString().trimmed().left(64));
		}
		return result;
	}
	if (tool.startsWith(Q("botfather_"))) {
		const auto text = [&](
				const char *key,
				int min,
				int max,
				bool required) {
			const auto value = arguments.value(Q(key));
			const auto missing = value.isUndefined() || value.isNull();
			const auto string = value.toString().trimmed();
			if ((missing && required)
				|| (!missing && !value.isString())
				|| (!missing && (string.size() < min || string.size() > max))) {
				*error = Q(key)
					+ Q(" must be a string of ")
					+ QString::number(min)
					+ Q(" to ")
					+ QString::number(max)
					+ Q(" characters.");
				return false;
			}
			if (!missing) {
				result.insert(Q(key), string);
			}
			return true;
		};
		const auto bot = [&] {
			const auto value = arguments.value(Q("bot"));
			auto name = value.toString().trimmed();
			if (name.startsWith(QChar('@'))) {
				name = name.mid(1);
			}
			if (!value.isString() || !IsBotUsername(name)) {
				*error = Q("bot must be the username of a bot: 5-32 "
					"letters, digits and underscores, ending in \"bot\".");
				return false;
			}
			result.insert(Q("bot"), name);
			return true;
		};
		const auto flag = [&](const char *key) {
			const auto value = arguments.value(Q(key));
			if (!value.isBool()) {
				*error = Q(key) + Q(" must be true or false.");
				return false;
			}
			result.insert(Q(key), value.toBool());
			return true;
		};
		auto ok = true;
		if (tool == Q("botfather_list_bots")) {
		} else if (tool == Q("botfather_create_bot")) {
			ok = text("name", 1, 64, true);
			if (ok) {
				const auto value = arguments.value(Q("username"));
				auto name = value.toString().trimmed();
				if (name.startsWith(QChar('@'))) {
					name = name.mid(1);
				}
				if (!value.isString() || !IsBotUsername(name)) {
					*error = Q("username must be 5-32 letters, digits and "
						"underscores, start with a letter and end in "
						"\"bot\".");
					ok = false;
				} else {
					result.insert(Q("username"), name);
				}
			}
		} else if (tool == Q("botfather_get_token")
			|| tool == Q("botfather_revoke_token")) {
			ok = bot();
		} else if (tool == Q("botfather_set_name")) {
			ok = bot() && text("name", 1, 64, true);
		} else if (tool == Q("botfather_set_description")) {
			ok = bot() && text("description", 1, 512, true);
		} else if (tool == Q("botfather_set_about")) {
			ok = bot() && text("about", 1, 120, true);
		} else if (tool == Q("botfather_set_privacy")
			|| tool == Q("botfather_set_join_groups")) {
			ok = bot() && flag("enabled");
		} else if (tool == Q("botfather_delete_bot")) {
			ok = bot();
			auto confirm = arguments.value(Q("confirm")).toString().trimmed();
			if (confirm.startsWith(QChar('@'))) {
				confirm = confirm.mid(1);
			}
			if (ok && confirm != result.value(Q("bot")).toString()) {
				*error = Q("confirm must repeat the bot's username exactly. "
					"Deleting a bot cannot be undone: ask the user first.");
				ok = false;
			}
		} else if (tool == Q("botfather_set_commands")) {
			ok = bot();
			const auto value = arguments.value(Q("commands"));
			auto list = QJsonArray();
			if (ok && (!value.isArray() || value.toArray().size() > 100)) {
				*error = Q("commands must be a list of at most 100 "
					"{command, description}.");
				ok = false;
			}
			for (const auto &entry : value.toArray()) {
				if (!ok) {
					break;
				}
				const auto object = entry.toObject();
				auto command = object.value(Q("command")).toString().trimmed();
				if (command.startsWith(QChar('/'))) {
					command = command.mid(1);
				}
				const auto about = object.value(
					Q("description")).toString().simplified();
				auto good = (command.size() >= 1 && command.size() <= 32)
					&& (about.size() >= 1 && about.size() <= 256);
				for (const auto ch : command) {
					const auto code = ch.unicode();
					good = good && ((code >= 'a' && code <= 'z')
						|| (code >= '0' && code <= '9')
						|| (code == '_'));
				}
				if (!good) {
					*error = Q("Each command needs \"command\" (1-32 "
						"lowercase letters, digits, underscores) and "
						"\"description\" (1-256 characters).");
					ok = false;
					break;
				}
				auto item = QJsonObject();
				item.insert(Q("command"), command);
				item.insert(Q("description"), about);
				list.push_back(item);
			}
			result.insert(Q("commands"), list);
		} else if (tool == Q("botfather_send")) {
			ok = text("text", 1, 4096, true);
		} else if (tool == Q("botfather_press")) {
			auto id = 0;
			ok = text("button", 1, 128, true);
			if (ok && (arguments.value(Q("message_id")).isUndefined()
				|| !IntArgument(
					arguments,
					"message_id",
					0,
					1,
					2000000000,
					&id,
					error))) {
				if (error->isEmpty()) {
					*error = Q("message_id is required.");
				}
				ok = false;
			}
			result.insert(Q("message_id"), id);
		} else if (tool == Q("botfather_read")) {
			auto limit = 0;
			ok = IntArgument(arguments, "limit", 5, 1, 20, &limit, error);
			result.insert(Q("limit"), limit);
		} else {
			*error = Q("This tool is not available.");
			ok = false;
		}
		return ok ? result : QJsonObject();
	}
	if (tool == Q("update_profile")) {
		auto given = 0;
		for (const auto key : { "first_name", "last_name", "bio" }) {
			const auto value = arguments.value(Q(key));
			if (value.isUndefined() || value.isNull()) {
				continue;
			} else if (!value.isString()) {
				*error = Q(key) + Q(" must be a string.");
				return QJsonObject();
			}
			const auto text = value.toString().trimmed();
			const auto max = (Q(key) == Q("bio")) ? 70 : 64;
			if (text.size() > max
				|| (Q(key) == Q("first_name") && text.isEmpty())) {
				*error = Q(key)
					+ Q(" is too long or empty: at most ")
					+ QString::number(max)
					+ Q(" characters, first_name at least 1.");
				return QJsonObject();
			}
			result.insert(Q(key), text);
			++given;
		}
		if (!given) {
			*error = Q("Give at least one of first_name, last_name or bio.");
		}
		return given ? result : QJsonObject();
	}
	if (tool == Q("get_messages")
		|| tool == Q("send_message")
		|| tool == Q("mark_chat_read")
		|| tool == Q("join_chat")
		|| tool == Q("leave_chat")) {
		const auto send = (tool == Q("send_message"));
		const auto read = (tool == Q("get_messages"));
		const auto chat = arguments.value(Q("chat"));
		if (!chat.isString() && !chat.isDouble()) {
			*error = Q("chat is required: an id from list_chats or a username.");
			return QJsonObject();
		}
		auto text = chat.isString()
			? chat.toString().trimmed()
			: QString::number(quint64(std::max(chat.toDouble(), 0.)));
		if (text.startsWith(QChar('@'))) {
			text = text.mid(1);
		}
		auto digits = !text.isEmpty();
		auto name = (text.size() >= 4 && text.size() <= 32);
		for (const auto ch : text) {
			const auto code = ch.unicode();
			const auto digit = (code >= '0' && code <= '9');
			digits = digits && digit;
			name = name && (digit
				|| (code >= 'a' && code <= 'z')
				|| (code >= 'A' && code <= 'Z')
				|| (code == '_'));
		}
		if (digits && text.toULongLong() > 0) {
			result.insert(Q("chat_id"), text);
		} else if (name && !digits) {
			result.insert(Q("chat_username"), text);
		} else {
			*error = Q("chat must be an id from list_chats or a username.");
			return QJsonObject();
		}
		if (!send && !read) {
			return result;
		} else if (!send) {
			auto limit = 0;
			auto before = 0;
			if (!IntArgument(arguments, "limit", 20, 1, 100, &limit, error)
				|| !IntArgument(
					arguments,
					"before_id",
					0,
					1,
					2000000000,
					&before,
					error)) {
				return QJsonObject();
			}
			result.insert(Q("limit"), limit);
			result.insert(Q("before_id"), before);
			return result;
		}
		const auto body = arguments.value(Q("text"));
		// Trimmed like the app's own input field; counted in UTF-16 units
		// as Telegram counts.
		const auto message = body.toString().trimmed();
		if (!body.isString() || message.isEmpty() || message.size() > 4096) {
			*error = Q("text must be a string of 1 to 4096 characters.");
			return QJsonObject();
		}
		auto reply = 0;
		if (!IntArgument(
				arguments,
				"reply_to",
				0,
				1,
				2000000000,
				&reply,
				error)) {
			return QJsonObject();
		}
		result.insert(Q("text"), message);
		result.insert(Q("reply_to"), reply);
		return result;
	}
	auto limit = 0;
	auto offset = 0;
	const auto friends = (tool == Q("list_friends"));
	if (!IntArgument(
			arguments,
			"limit",
			friends ? 200 : 50,
			1,
			friends ? 1000 : 200,
			&limit,
			error)
		|| !IntArgument(
			arguments,
			"offset",
			0,
			0,
			1000000,
			&offset,
			error)) {
		return QJsonObject();
	}
	result.insert(Q("limit"), limit);
	result.insert(Q("offset"), offset);
	if (friends) {
		const auto query = arguments.value(Q("query"));
		if (!query.isUndefined() && !query.isNull() && !query.isString()) {
			*error = Q("query must be a string.");
			return QJsonObject();
		}
		result.insert(Q("query"), query.toString().trimmed().left(64));
	} else {
		const auto unread = arguments.value(Q("unread_only"));
		if (!unread.isUndefined() && !unread.isNull() && !unread.isBool()) {
			*error = Q("unread_only must be true or false.");
			return QJsonObject();
		}
		result.insert(Q("unread_only"), unread.toBool());
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
		"The Telegram accounts logged in to the Luckgram Desktop app on "
		"this computer. Call list_accounts first. switch_account changes "
		"which account the window shows. Skill tools (list_friends, "
		"list_chats) take an optional \"account\" id and work on any "
		"logged-in account without switching; each skill is switched on "
		"per account by the user in the app, and a call to one that is "
		"off says so."));
	return result;
}

void CallSkillTool(
		const QJsonValue &id,
		const QString &name,
		const char *skill,
		const QJsonObject &arguments,
		const Backend &backend,
		std::function<void(QJsonObject)> done) {
	const auto fail = [&](const QString &text) {
		done(RpcResult(id, ToolError(text)));
	};
	if (backend.locked()) {
		return fail(Q(
			"Luckgram Desktop is locked with a passcode. Unlock it on the "
			"computer first."));
	}
	const auto accounts = backend.accounts();
	auto account = quint64(0);
	const auto given = arguments.value(Q("account"));
	if (given.isUndefined() || given.isNull()) {
		for (const auto &entry : accounts) {
			if (entry.active) {
				account = entry.id;
			}
		}
		if (!account) {
			return fail(Q(
				"No account is active: Luckgram Desktop shows a login "
				"screen. Give \"account\" from list_accounts."));
		}
	} else {
		auto wanted = QJsonObject();
		wanted.insert(Q("id"), given);
		auto error = QString();
		account = ResolveAccount(accounts, wanted, &error);
		if (!account) {
			return fail(Q("account: ") + error);
		}
	}
	if (!backend.skillEnabled || !backend.skillEnabled(account, Q(skill))) {
		return fail(Q("The \"")
			+ Q(skill)
			+ Q("\" skill is off for account ")
			+ QString::number(account)
			+ Q(". The user switches it on in Luckgram Desktop: Settings > "
				"Luckgram > Local MCP server."));
	}
	auto error = QString();
	const auto checked = SkillArguments(name, arguments, &error);
	if (!error.isEmpty()) {
		return fail(error);
	} else if (!backend.call) {
		return fail(Q("This tool is not available."));
	}
	const auto limit = LimitOf(name);
	if (limit.perHour > 0 || limit.perDay > 0) {
		if (!backend.loadTimes || !backend.saveTimes || !backend.nowMs) {
			return fail(Q("This tool is not available."));
		}
		// Counted when asked, done or not: a failing loop is a loop too.
		const auto key = RateSettingKey(name, account);
		const auto now = backend.nowMs();
		auto times = backend.loadTimes(key);
		const auto wait = RateWaitSeconds(times, now, limit);
		if (wait > 0) {
			backend.saveTimes(key, times);
			return fail(Q("Rate limit for ")
				+ name
				+ Q(" on this account (")
				+ QString::number(limit.perHour)
				+ Q(" an hour, ")
				+ QString::number(limit.perDay)
				+ Q(" a day). Try again in ")
				+ QString::number(wait)
				+ Q(" seconds. Do not retry sooner."));
		}
		times.push_back(now);
		backend.saveTimes(key, times);
	}
	backend.call(name, account, checked, [=](ToolOutcome outcome) {
		if (!outcome.error.isEmpty()) {
			done(RpcResult(id, ToolError(outcome.error)));
			return;
		}
		// Which account answered: the default may have been used.
		outcome.value.insert(Q("account"), QString::number(account));
		done(RpcResult(id, ToolResult(outcome.value)));
	});
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

namespace {

// Everything that needs no JSON: the answer when the request is refused.
[[nodiscard]] bool Refused(
		const Request &request,
		const Config &config,
		Response *response) {
	// No CORS answer anywhere: a web page can't read a reply, and with the
	// checks below it can't even make a request that is looked at.
	if (!IsLoopbackHost(request.header("host"))) {
		*response = Plain(403, "Forbidden host.");
		return true;
	}
	const auto origin = request.header("origin");
	if (!origin.isEmpty() && !IsLoopbackOrigin(origin)) {
		*response = Plain(403, "Forbidden origin.");
		return true;
	}
	auto path = request.path;
	if (path.size() > 1 && path.endsWith('/')) {
		path.chop(1);
	}
	if (path != kPath) {
		*response = Plain(404, "Not found. The MCP endpoint is /mcp.");
		return true;
	}
	const auto authorization = request.header("authorization");
	const auto prefix = QByteArray("bearer ");
	if (!IsValidToken(config.token)
		|| authorization.left(prefix.size()).toLower() != prefix
		|| !SameToken(
			authorization.mid(prefix.size()).trimmed(),
			config.token.toLatin1())) {
		*response = Plain(401, "Missing or wrong access token.");
		response->headers.emplace_back("WWW-Authenticate", "Bearer");
		return true;
	}
	if (request.method != "POST") {
		// GET would open the optional server-to-client stream; there is
		// nothing to send on it.
		*response = Plain(405, "Use POST.");
		response->headers.emplace_back("Allow", "POST");
		return true;
	}
	const auto type = request.header("content-type").toLower();
	if (!type.startsWith("application/json")) {
		*response = Plain(415, "Content-Type must be application/json.");
		return true;
	}
	return false;
}

[[nodiscard]] Response Accepted202() {
	auto result = Response();
	result.status = 202;
	return result;
}

} // namespace

void HandleAsync(
		const Request &request,
		const Config &config,
		const Backend &backend,
		std::function<void(Response)> done) {
	auto refused = Response();
	if (Refused(request, config, &refused)) {
		done(std::move(refused));
		return;
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(request.body, &error);
	if (error.error != QJsonParseError::NoError) {
		done(Json(400, QJsonDocument(RpcError(
			QJsonValue(QJsonValue::Null),
			kParseError,
			Q("Parse error: ") + error.errorString()))));
		return;
	}
	if (document.isObject()) {
		HandleMessageAsync(document.object(), backend, [=](QJsonObject reply) {
			done(reply.isEmpty()
				? Accepted202()
				: Json(200, QJsonDocument(reply)));
		});
		return;
	}
	const auto batch = document.array();
	if (batch.isEmpty()) {
		done(Json(400, QJsonDocument(RpcError(
			QJsonValue(QJsonValue::Null),
			kInvalidRequest,
			Q("Invalid request.")))));
		return;
	}
	// Answers keep the order of the requests, whenever each one comes.
	struct Collected {
		std::vector<QJsonObject> replies;
		int left = 0;
		std::function<void(Response)> done;
	};
	const auto collected = std::make_shared<Collected>();
	collected->replies.resize(batch.size());
	collected->left = int(batch.size());
	collected->done = std::move(done);
	const auto finish = [=](int index, QJsonObject reply) {
		collected->replies[index] = std::move(reply);
		if (--collected->left) {
			return;
		}
		auto replies = QJsonArray();
		for (const auto &entry : collected->replies) {
			if (!entry.isEmpty()) {
				replies.push_back(entry);
			}
		}
		const auto onstack = std::exchange(collected->done, nullptr);
		onstack(replies.isEmpty()
			? Accepted202()
			: Json(200, QJsonDocument(replies)));
	};
	for (auto i = 0; i != int(batch.size()); ++i) {
		const auto entry = batch.at(i);
		if (!entry.isObject()) {
			finish(i, RpcError(
				QJsonValue(QJsonValue::Null),
				kInvalidRequest,
				Q("Invalid request.")));
			continue;
		}
		HandleMessageAsync(entry.toObject(), backend, [=](QJsonObject reply) {
			finish(i, std::move(reply));
		});
	}
}

void HandleMessageAsync(
		const QJsonObject &message,
		const Backend &backend,
		std::function<void(QJsonObject)> done) {
	const auto id = message.value(Q("id"));
	const auto hasId = id.isString() || id.isDouble();
	const auto method = message.value(Q("method"));
	if (!method.isString()) {
		// A response to something this server never asked: dropped.
		if (message.contains(Q("result")) || message.contains(Q("error"))) {
			done(QJsonObject());
			return;
		}
		done(RpcError(
			hasId ? id : QJsonValue(QJsonValue::Null),
			kInvalidRequest,
			Q("Invalid request.")));
		return;
	} else if (!hasId) {
		done(QJsonObject()); // A notification.
		return;
	} else if (message.value(Q("jsonrpc")).toString() != Q("2.0")) {
		done(RpcError(id, kInvalidRequest, Q("Invalid request.")));
		return;
	}
	const auto name = method.toString();
	const auto params = message.value(Q("params")).toObject();
	if (name == Q("initialize")) {
		done(RpcResult(id, Initialize(params)));
	} else if (name == Q("ping")) {
		done(RpcResult(id, QJsonObject()));
	} else if (name == Q("tools/list")) {
		auto result = QJsonObject();
		result.insert(Q("tools"), Tools());
		done(RpcResult(id, result));
	} else if (name == Q("tools/call")) {
		const auto tool = params.value(Q("name")).toString();
		if (const auto skill = SkillOf(tool)) {
			CallSkillTool(
				id,
				tool,
				skill,
				params.value(Q("arguments")).toObject(),
				backend,
				std::move(done));
		} else {
			done(CallTool(id, params, backend));
		}
	} else {
		done(RpcError(id, kMethodNotFound, Q("Method not found: ") + name));
	}
}

Response Handle(
		const Request &request,
		const Config &config,
		const Backend &backend) {
	auto result = Response();
	result.status = 500;
	HandleAsync(request, config, backend, [&](Response response) {
		result = std::move(response);
	});
	return result;
}

QJsonObject HandleMessage(
		const QJsonObject &message,
		const Backend &backend) {
	auto result = QJsonObject();
	HandleMessageAsync(message, backend, [&](QJsonObject reply) {
		result = std::move(reply);
	});
	return result;
}

const std::vector<SkillInfo> &Skills() {
	static const auto result = std::vector<SkillInfo>{
		{
			kSkillFriends,
			"好友",
			"Friends",
			"查看好友列表，按用户名或手机号查找用户",
			"See the list of friends, find a user by username or phone",
		},
		{
			kSkillFriendsManage,
			"管理好友",
			"Manage friends",
			"加好友、删好友，有频率上限",
			"Add and remove friends, rate limited",
			true,
		},
		{
			kSkillChats,
			"会话",
			"Chats",
			"查看会话列表和未读数，不含消息内容",
			"See the chat list and unread counts, no message texts",
		},
		{
			kSkillMessages,
			"读消息",
			"Read messages",
			"读取会话里的消息内容，不会标记已读",
			"Read the messages of a chat, without marking it read",
		},
		{
			kSkillMessagesSend,
			"发消息",
			"Send messages",
			"以该账号的身份发送文字消息，有频率上限",
			"Send text messages as this account, rate limited",
			true,
		},
		{
			kSkillChatsManage,
			"管理会话",
			"Manage chats",
			"把会话标记为已读",
			"Mark chats as read",
			true,
		},
		{
			kSkillGroups,
			"群组和频道",
			"Groups and channels",
			"加入公开的群组或频道、退出群组或频道，有频率上限",
			"Join public groups and channels, leave them; rate limited",
			true,
		},
		{
			kSkillProfile,
			"资料",
			"Profile",
			"修改昵称和简介，有频率上限",
			"Change the name and the bio, rate limited",
			true,
		},
		{
			kSkillBotFather,
			"BotFather 机器人管理",
			"BotFather: manage bots",
			"通过 @BotFather 创建和管理该账号的机器人，可取得机器人令牌",
			"Create and manage this account's bots through @BotFather, "
			"including their API tokens",
			true,
		},
	};
	return result;
}

RateLimit LimitOf(const QString &tool) {
	if (tool == Q("add_friend")) {
		return { 5, 20 };
	} else if (tool == Q("remove_friend")) {
		return { 20, 50 };
	} else if (tool == Q("find_user")) {
		return { 30, 100 }; // Telegram limits lookups hard.
	} else if (tool == Q("send_message")) {
		return { 30, 200 };
	} else if (tool == Q("mark_chat_read")) {
		return { 60, 500 };
	} else if (tool == Q("join_chat")) {
		return { 5, 20 };
	} else if (tool == Q("leave_chat")) {
		return { 10, 30 };
	} else if (tool == Q("update_profile")) {
		return { 5, 10 };
	} else if (tool == Q("botfather_create_bot")
		|| tool == Q("botfather_delete_bot")) {
		return { 3, 10 };
	} else if (tool == Q("botfather_read")
		|| tool == Q("botfather_list_bots")) {
		return { 60, 300 };
	} else if (tool.startsWith(Q("botfather_"))) {
		return { 30, 150 };
	}
	return {};
}

qint64 RateWaitSeconds(
		std::vector<qint64> &times,
		qint64 nowMs,
		RateLimit limit) {
	constexpr auto kHour = qint64(60) * 60 * 1000;
	constexpr auto kDay = 24 * kHour;
	// A clock set back must not lock the tool: such entries are dropped.
	times.erase(
		std::remove_if(times.begin(), times.end(), [&](qint64 time) {
			return (time > nowMs) || (nowMs - time >= kDay);
		}),
		times.end());
	std::sort(times.begin(), times.end());
	const auto waitFor = [&](int allowed, qint64 window) {
		if (allowed <= 0) {
			return qint64(0);
		}
		auto inside = std::vector<qint64>();
		for (const auto time : times) {
			if (nowMs - time < window) {
				inside.push_back(time);
			}
		}
		if (int(inside.size()) < allowed) {
			return qint64(0);
		}
		// Free again when the oldest of the last `allowed` leaves.
		const auto leaves = inside[inside.size() - allowed] + window;
		return std::max(qint64(1), (leaves - nowMs + 999) / 1000);
	};
	return std::max(
		waitFor(limit.perHour, kHour),
		waitFor(limit.perDay, kDay));
}

QString RateSettingKey(const QString &tool, quint64 account) {
	return Q("mcp_rate_") + tool + Q("_") + QString::number(account);
}

QString SerializeTimes(const std::vector<qint64> &times) {
	auto list = QStringList();
	for (const auto time : times) {
		list.push_back(QString::number(time));
	}
	return list.join(QChar(','));
}

std::vector<qint64> ParseTimes(const QString &text) {
	auto result = std::vector<qint64>();
	for (const auto &part : text.split(QChar(','), Qt::SkipEmptyParts)) {
		auto ok = false;
		const auto time = part.toLongLong(&ok);
		if (ok && time > 0) {
			result.push_back(time);
		}
	}
	return result;
}

QString SkillSettingKey(const QString &skill, quint64 account) {
	return Q("mcp_skill_") + skill + Q("_") + QString::number(account);
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

QString ExtractBotToken(const QString &text) {
	// <bot id>:<35 characters>. Scanned by hand: no regular expressions in
	// the logic files.
	const auto digit = [](QChar ch) {
		return ch >= QChar('0') && ch <= QChar('9');
	};
	const auto secret = [&](QChar ch) {
		const auto code = ch.unicode();
		return digit(ch)
			|| (code >= 'a' && code <= 'z')
			|| (code >= 'A' && code <= 'Z')
			|| (code == '_')
			|| (code == '-');
	};
	for (auto colon = text.indexOf(QChar(':'));
		colon >= 0;
		colon = text.indexOf(QChar(':'), colon + 1)) {
		auto from = colon;
		while (from > 0 && digit(text[from - 1])) {
			--from;
		}
		auto till = colon + 1;
		while (till < text.size() && secret(text[till])) {
			++till;
		}
		const auto idLength = colon - from;
		const auto secretLength = till - colon - 1;
		if (idLength >= 6
			&& idLength <= 12
			&& secretLength >= 30
			&& secretLength <= 64
			&& (from == 0 || !secret(text[from - 1]))) {
			return text.mid(from, till - from);
		}
	}
	return QString();
}

bool IsBotUsername(const QString &username) {
	if (username.size() < 5
		|| username.size() > 32
		|| !username.endsWith(Q("bot"), Qt::CaseInsensitive)) {
		return false;
	}
	const auto first = username[0].unicode();
	if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z'))) {
		return false;
	}
	for (const auto ch : username) {
		const auto code = ch.unicode();
		const auto good = (code >= 'a' && code <= 'z')
			|| (code >= 'A' && code <= 'Z')
			|| (code >= '0' && code <= '9')
			|| (code == '_');
		if (!good) {
			return false;
		}
	}
	return true;
}

QStringList BotFatherScript(
		const QString &tool,
		const QJsonObject &arguments) {
	const auto bot = Q("@") + arguments.value(Q("bot")).toString();
	const auto value = [&](const char *key) {
		return arguments.value(Q(key)).toString();
	};
	const auto enabled = arguments.value(Q("enabled")).toBool();
	if (tool == Q("botfather_list_bots")) {
		return { Q("/mybots") };
	} else if (tool == Q("botfather_create_bot")) {
		return { Q("/newbot"), value("name"), value("username") };
	} else if (tool == Q("botfather_get_token")) {
		return { Q("/token"), bot };
	} else if (tool == Q("botfather_revoke_token")) {
		return { Q("/revoke"), bot };
	} else if (tool == Q("botfather_set_name")) {
		return { Q("/setname"), bot, value("name") };
	} else if (tool == Q("botfather_set_description")) {
		return { Q("/setdescription"), bot, value("description") };
	} else if (tool == Q("botfather_set_about")) {
		return { Q("/setabouttext"), bot, value("about") };
	} else if (tool == Q("botfather_set_privacy")) {
		return { Q("/setprivacy"), bot, enabled ? Q("Enable") : Q("Disable") };
	} else if (tool == Q("botfather_set_join_groups")) {
		return {
			Q("/setjoingroups"),
			bot,
			enabled ? Q("Enable") : Q("Disable"),
		};
	} else if (tool == Q("botfather_delete_bot")) {
		return { Q("/deletebot"), bot, Q("Yes, I am totally sure.") };
	} else if (tool == Q("botfather_set_commands")) {
		auto lines = QStringList();
		for (const auto &entry : arguments.value(Q("commands")).toArray()) {
			const auto object = entry.toObject();
			lines.push_back(object.value(Q("command")).toString()
				+ Q(" - ")
				+ object.value(Q("description")).toString());
		}
		return {
			Q("/setcommands"),
			bot,
			lines.isEmpty() ? Q("/empty") : lines.join(QChar('\n')),
		};
	} else if (tool == Q("botfather_send")) {
		return { value("text") };
	}
	return {};
}

bool BotFatherRefused(const QString &reply) {
	const auto text = reply.trimmed();
	return text.startsWith(Q("Sorry"), Qt::CaseInsensitive)
		|| text.startsWith(Q("Invalid"), Qt::CaseInsensitive)
		|| text.startsWith(Q("Unrecognized"), Qt::CaseInsensitive)
		|| text.startsWith(Q("Oops"), Qt::CaseInsensitive)
		|| text.contains(Q("too many attempts"), Qt::CaseInsensitive)
		|| text.contains(Q("I don't have a bot"), Qt::CaseInsensitive)
		|| text.contains(Q("You have no bots"), Qt::CaseInsensitive)
		|| text.contains(Q("try again"), Qt::CaseInsensitive);
}

bool BotFatherSucceeded(const QString &tool, const QString &reply) {
	if (tool == Q("botfather_send") || tool == Q("botfather_list_bots")) {
		return !reply.trimmed().isEmpty();
	} else if (tool == Q("botfather_create_bot")
		|| tool == Q("botfather_get_token")
		|| tool == Q("botfather_revoke_token")) {
		return !ExtractBotToken(reply).isEmpty();
	} else if (BotFatherRefused(reply)) {
		return false;
	} else if (tool == Q("botfather_delete_bot")) {
		return reply.contains(Q("Done"), Qt::CaseInsensitive)
			|| reply.contains(Q("is gone"), Qt::CaseInsensitive);
	}
	return reply.contains(Q("Success"), Qt::CaseInsensitive)
		|| reply.contains(Q("Done"), Qt::CaseInsensitive);
}

bool IsValidCloudKey(const QString &key) {
	const auto prefix = Q(kCloudKeyPrefix);
	return key.startsWith(prefix)
		&& (key.size() == prefix.size() + 43)
		&& IsValidToken(key.mid(prefix.size()));
}

QString CloudKeyFromBytes(const QByteArray &bytes32) {
	if (bytes32.size() != 32) {
		return QString();
	}
	return Q(kCloudKeyPrefix) + QString::fromLatin1(bytes32.toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool IsValidDeviceId(const QString &id) {
	if (id.size() < 8 || id.size() > 64) {
		return false;
	}
	for (const auto ch : id) {
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

QByteArray CloudPollBody(
		const QString &deviceId,
		const QString &deviceName,
		const QString &version,
		const std::vector<CloudResult> &results) {
	auto list = QJsonArray();
	for (const auto &result : results) {
		auto entry = QJsonObject();
		entry.insert(Q("id"), result.id);
		entry.insert(Q("response"), result.response);
		list.push_back(entry);
	}
	auto body = QJsonObject();
	body.insert(Q("device_id"), deviceId);
	body.insert(Q("device_name"), deviceName);
	body.insert(Q("version"), version);
	body.insert(Q("results"), list);
	return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

std::vector<CloudRequest> ParseCloudRequests(
		const QByteArray &json,
		bool *ok) {
	auto result = std::vector<CloudRequest>();
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(json, &error);
	const auto requests = document.object().value(Q("requests"));
	*ok = (error.error == QJsonParseError::NoError) && requests.isArray();
	if (!*ok) {
		return result;
	}
	for (const auto &value : requests.toArray()) {
		const auto entry = value.toObject();
		const auto id = entry.value(Q("id")).toString();
		const auto message = entry.value(Q("message"));
		if (id.isEmpty() || id.size() > 64 || !message.isObject()) {
			continue;
		}
		result.push_back({ id, message.toObject() });
	}
	return result;
}

qint64 CloudRetryDelayMs(int failures) {
	const auto step = std::clamp(failures, 1, 6);
	return std::min(qint64(1000) << step, qint64(60) * 1000);
}

QString CloudHostUrl(int host) {
	return Q((host == 1) ? kCloudBackupUrl : kCloudUrl);
}

int NextCloudHost(int host, int failuresOnHost) {
	const auto current = (host == 1) ? 1 : 0;
	return (failuresOnHost >= 2) ? (1 - current) : current;
}

QString CloudClientConfigJson(const QString &key, int host) {
	auto headers = QJsonObject();
	headers.insert(Q("Authorization"), Q("Bearer ") + key);
	auto server = QJsonObject();
	server.insert(Q("type"), Q("http"));
	server.insert(Q("url"), CloudHostUrl(host) + Q(kPath));
	server.insert(Q("headers"), headers);
	auto servers = QJsonObject();
	servers.insert(Q("luckgram-cloud"), server);
	auto root = QJsonObject();
	root.insert(Q("mcpServers"), servers);
	return QString::fromUtf8(
		QJsonDocument(root).toJson(QJsonDocument::Indented)).trimmed();
}

} // namespace Luckgram::Mcp
