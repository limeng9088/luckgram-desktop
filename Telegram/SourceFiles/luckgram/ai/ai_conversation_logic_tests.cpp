/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"
#include "luckgram/ai/ai_agents_logic.h"
#include "luckgram/ai/ai_conversation_logic.h"

#include <QtCore/QJsonDocument>

// 18 (conversation store and request context) and 22 (agents).
namespace {

using namespace Luckgram::AI::Conversations;

[[nodiscard]] Message Text(const char *role, const QString &text) {
	static auto counter = 0;
	auto result = Message();
	result.id = QStringLiteral("m") + QString::number(++counter);
	result.role = QString::fromLatin1(role);
	result.text = text;
	return result;
}

} // namespace

LUCKGRAM_TEST(AiConversationContextIsTheLastTwentyWithSystemFirst) {
	auto c = Conversation();
	c.system = QStringLiteral("Be brief.");
	for (auto i = 0; i < 30; ++i) {
		c.messages.push_back(Text(
			(i % 2) ? kRoleAssistant : kRoleUser,
			QStringLiteral("m") + QString::number(i)));
	}
	const auto request = RequestMessages(c, 60000);
	LG_EQUAL(int(request.size()), 1 + kContextMessages);
	LG_EQUAL(request[0], (RequestMessage{
		QStringLiteral("system"),
		QStringLiteral("Be brief."),
	}));
	LG_EQUAL(request[1].content, QStringLiteral("m10"));
	LG_EQUAL(request[1].role, QStringLiteral("user"));
	LG_EQUAL(request.back().content, QStringLiteral("m29"));
	LG_EQUAL(request.back().role, QStringLiteral("assistant"));
}

LUCKGRAM_TEST(AiConversationFailedPendingAndOtherTypesAreSkipped) {
	auto c = Conversation();
	c.messages.push_back(Text(kRoleUser, QStringLiteral("q1")));
	auto failed = Text(kRoleAssistant, QString());
	failed.errorCode = QStringLiteral("network_error");
	c.messages.push_back(failed);
	auto image = Text(kRoleAssistant, QStringLiteral("ignored"));
	image.type = QStringLiteral("image");
	c.messages.push_back(image);
	auto pending = Text(kRoleAssistant, QStringLiteral("half"));
	pending.pending = true;
	c.messages.push_back(pending);
	const auto request = RequestMessages(c, 60000);
	LG_EQUAL(int(request.size()), 1);
	LG_EQUAL(request[0].content, QStringLiteral("q1"));
	// A registered type tells the model what it was (23: the prompt).
	const auto withImages = RequestMessages(c, 60000, [](const Message &m) {
		return (m.type == QLatin1String("image"))
			? QStringLiteral("[image]")
			: DefaultContextText(m);
	});
	LG_EQUAL(int(withImages.size()), 2);
	LG_EQUAL(withImages[1].content, QStringLiteral("[image]"));
}

LUCKGRAM_TEST(AiConversationOldestDroppedOverTheLimit) {
	auto c = Conversation();
	c.messages.push_back(Text(kRoleUser, QString(60, QChar('a'))));
	c.messages.push_back(Text(kRoleAssistant, QString(30, QChar('b'))));
	c.messages.push_back(Text(kRoleUser, QString(30, QChar('c'))));
	const auto request = RequestMessages(c, 70);
	LG_EQUAL(int(request.size()), 2);
	LG_EQUAL(request[0].content, QString(30, QChar('b')));
	// The newest alone too long: its end is sent.
	auto single = Conversation();
	single.messages.push_back(Text(
		kRoleUser,
		QString(10, QChar('x')) + QString(10, QChar('y'))));
	const auto cut = RequestMessages(single, 10);
	LG_EQUAL(int(cut.size()), 1);
	LG_EQUAL(cut[0].content, QString(10, QChar('y')));
}

LUCKGRAM_TEST(AiConversationJsonRoundTripLikeAndroid) {
	// What Android LuckgramAiStore writes.
	const auto stored = QJsonDocument::fromJson(QByteArray(
		"{\"id\":\"c1\",\"task\":\"agent\",\"created\":1000,\"updated\":2000,"
		"\"key\":\"market:email-pro\",\"title\":\"Work email\","
		"\"params\":{\"agent_id\":\"email-pro\"},"
		"\"messages\":[{\"id\":\"m1\",\"role\":\"user\",\"type\":\"text\","
		"\"time\":1500,\"text\":\"hi\"},{\"id\":\"m2\",\"role\":\"assistant\","
		"\"type\":\"text\",\"time\":1600,\"error\":\"quota_exceeded\"}]}"))
		.object();
	const auto c = Conversation::FromJson(stored);
	LG_EQUAL(c.task, QStringLiteral("agent"));
	LG_EQUAL(*c.key, QStringLiteral("market:email-pro"));
	LG_CHECK(!c.system.has_value());
	LG_EQUAL(
		c.params.value(QStringLiteral("agent_id")).toString(),
		QStringLiteral("email-pro"));
	LG_EQUAL(int(c.messages.size()), 2);
	LG_EQUAL(*c.messages[1].errorCode, QStringLiteral("quota_exceeded"));
	LG_CHECK(!c.messages[1].text.has_value());
	LG_EQUAL(c.updatedAt, qint64(2000));
	const auto back = Conversation::FromJson(c.toJson());
	LG_EQUAL(back.toJson(), c.toJson());
	// Pending messages are not saved.
	auto withPending = c;
	withPending.messages.push_back(Text(kRoleAssistant, QStringLiteral("…")));
	withPending.messages.back().pending = true;
	LG_EQUAL(
		Conversation::FromJson(withPending.toJson()).messages.size(),
		c.messages.size());
}

LUCKGRAM_TEST(AiConversationTitlesAndHistoryOrder) {
	LG_EQUAL(
		AutoTitle(QStringLiteral(" Explain\nsolar panels ")),
		QStringLiteral("Explain solar panels"));
	LG_EQUAL(int(AutoTitle(QString(50, QChar('q'))).size()), 41);
	auto c = Conversation();
	c.messages.push_back(Text(kRoleUser, QStringLiteral("a")));
	c.messages.push_back(Text(kRoleAssistant, QStringLiteral("line 1\nline 2")));
	LG_EQUAL(PreviewText(c), QStringLiteral("line 1 line 2"));
	auto list = std::vector<Conversation>(3);
	list[0].id = QStringLiteral("old");
	list[0].updatedAt = 1;
	list[1].id = QStringLiteral("new");
	list[1].updatedAt = 3;
	list[2].id = QStringLiteral("mid");
	list[2].updatedAt = 2;
	SortForHistory(list);
	LG_EQUAL(list[0].id, QStringLiteral("new"));
	LG_EQUAL(list[2].id, QStringLiteral("old"));
}

// ---- 22 agents

LUCKGRAM_TEST(AiAgentsPresets) {
	using namespace Luckgram::AI::Agents;
	const auto &presets = Presets();
	LG_EQUAL(int(presets.size()), 5);
	auto ids = QStringList();
	for (const auto &agent : presets) {
		ids.push_back(agent.id);
		LG_CHECK(agent.preset);
		LG_CHECK(!agent.prompt.isEmpty());
	}
	LG_EQUAL(ids, QStringList({
		QStringLiteral("travel"),
		QStringLiteral("copywriter"),
		QStringLiteral("language"),
		QStringLiteral("researcher"),
		QStringLiteral("coder"),
	}));
	LG_EQUAL(presets[0].name(true), QString::fromUtf8("旅行规划师"));
	LG_EQUAL(presets[0].name(false), QStringLiteral("Travel planner"));
	LG_EQUAL(presets[4].initial(false), QStringLiteral("C"));
	LG_EQUAL(ConversationKey(QStringLiteral("travel")), QStringLiteral("agent:travel"));
	LG_EQUAL(MarketAgentId(QStringLiteral("x")), QStringLiteral("market:x"));
}

LUCKGRAM_TEST(AiAgentsValidate) {
	using namespace Luckgram::AI::Agents;
	LG_CHECK(!Validate(QStringLiteral("Translator"), QStringLiteral("English only")));
	LG_CHECK(Validate(QStringLiteral("  "), QStringLiteral("x")) == Invalid::EmptyName);
	LG_CHECK(Validate(QString(31, QChar('n')), QStringLiteral("x")) == Invalid::NameTooLong);
	LG_CHECK(Validate(QStringLiteral("n"), QStringLiteral("\n")) == Invalid::EmptyPrompt);
	LG_CHECK(Validate(QStringLiteral("n"), QString(4001, QChar('p'))) == Invalid::PromptTooLong);
	LG_CHECK(!Validate(QString(30, QChar('n')), QString(4000, QChar('p'))));
}

LUCKGRAM_TEST(AiAgentsCustomStorageLikeAndroid) {
	using namespace Luckgram::AI::Agents;
	// Android writes the color as a signed int (0xFFE91E63 = -1499549).
	const auto list = ParseCustom(QStringLiteral(
		"[{\"id\":\"u1\",\"name\":\"EN only\",\"desc\":\"d\","
		"\"prompt\":\"Answer in English.\",\"color\":-1499549},"
		"{\"name\":\"no id\",\"prompt\":\"p\"}, 5]"));
	LG_EQUAL(int(list.size()), 2);
	LG_EQUAL(list[0].color, quint32(0xFFE91E63U));
	LG_EQUAL(list[0].name(false), QStringLiteral("EN only"));
	LG_CHECK(!list[0].preset);
	LG_CHECK(!list[1].id.isEmpty());
	LG_EQUAL(list[1].color, Colors().front());
	const auto back = ParseCustom(CustomToString(list));
	LG_EQUAL(back[0].color, quint32(0xFFE91E63U));
	LG_EQUAL(back[0].prompt, QStringLiteral("Answer in English."));
	LG_EQUAL(back[1].id, list[1].id);
	auto edited = back[0];
	edited.prompt = QStringLiteral("Only English.");
	auto saved = SaveCustom(back, edited);
	LG_EQUAL(int(saved.size()), 2);
	LG_EQUAL(saved[0].prompt, QStringLiteral("Only English."));
	saved = SaveCustom(saved, Custom(QString(), QStringLiteral("new"), QString(), QStringLiteral("p"), Colors()[2]));
	LG_EQUAL(int(saved.size()), 3);
	LG_EQUAL(int(DeleteCustom(saved, QStringLiteral("u1")).size()), 2);
	LG_EQUAL(int(ParseCustom(QStringLiteral("garbage")).size()), 0);
}

LUCKGRAM_TEST(AiAgentsPinned) {
	using namespace Luckgram::AI::Agents;
	auto pinned = ParsePinned(QStringLiteral("[\"travel\",\"travel\",\"coder\"]"));
	LG_EQUAL(pinned, QStringList({ QStringLiteral("travel"), QStringLiteral("coder") }));
	pinned = SetPinned(pinned, QStringLiteral("market:x"), true);
	pinned = SetPinned(pinned, QStringLiteral("travel"), false);
	pinned = SetPinned(pinned, QStringLiteral("coder"), true);
	LG_EQUAL(ParsePinned(PinnedToString(pinned)), QStringList({
		QStringLiteral("coder"),
		QStringLiteral("market:x"),
	}));
	LG_CHECK(ParsePinned(QString()).isEmpty());
}

// ---- 18 需求 5 "问 AI"

LUCKGRAM_TEST(AiAskBuildsTextLinksAndFiles) {
	using namespace Luckgram::AI::Ask;
	auto first = Part();
	first.text = QStringLiteral(" Long article text ");
	first.links = { QStringLiteral("https://a.example/x") };
	first.webpageUrl = QStringLiteral("https://b.example/y");
	auto second = Part();
	second.text = QStringLiteral("see https://b.example/y");
	second.links = { QStringLiteral("https://a.example/x") }; // Once only.
	second.webpageUrl = QStringLiteral("https://c.example/z");
	auto file = Part();
	file.fileName = QStringLiteral("notes.md");
	file.fileContent = QStringLiteral("# Notes\n");
	LG_EQUAL(BuildPrompt({ first, second, file }, false), QStringLiteral(
		"Long article text\n\nsee https://b.example/y\n"
		"Link: https://a.example/x\n"
		"Link: https://b.example/y\n"
		"Link: https://c.example/z\n"
		"File: notes.md\n"
		"File content:\n```\n# Notes\n```\n\n"));
	auto zh = Part();
	zh.fileName = QStringLiteral("a.pdf");
	LG_EQUAL(BuildPrompt({ zh }, true), QString::fromUtf8("文件：a.pdf\n\n"));
	LG_EQUAL(BuildPrompt({ Part() }, true), QString());
}

LUCKGRAM_TEST(AiAskTextFiles) {
	using namespace Luckgram::AI::Ask;
	LG_CHECK(IsTextFile(QStringLiteral("text/plain"), QString()));
	LG_CHECK(IsTextFile(QStringLiteral("application/json"), QString()));
	LG_CHECK(IsTextFile(QStringLiteral("application/octet-stream"), QStringLiteral("main.CPP")));
	LG_CHECK(!IsTextFile(QStringLiteral("application/pdf"), QStringLiteral("a.pdf")));
	LG_CHECK(!IsTextFile(QString(), QStringLiteral("README")));
}
