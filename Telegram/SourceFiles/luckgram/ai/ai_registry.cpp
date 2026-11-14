/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_registry.h"

#include "luckgram/ai/ai_agents.h"
#include "luckgram/ai/ai_assistant.h"
#include "luckgram/ai/ai_compose_logic.h"
#include "luckgram/ai/ai_image.h"
#include "luckgram/ai/ai_store.h"
#include "luckgram/ai/ai_text_logic.h"
#include "luckgram/luckgram_features.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_session.h"
#include "ui/widgets/labels.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_widgets.h"
#include "window/window_session_controller.h"

namespace Luckgram::AI {
namespace {

struct Registry {
	std::vector<QuickButton> buttons;
	std::vector<InputInterceptor> interceptors;
	std::vector<TopEntry> entries;
	std::vector<MessageType> types;
	std::vector<not_null<Host*>> open;
};

[[nodiscard]] Registry &Get() {
	static auto result = Registry();
	return result;
}

template <typename T>
void ReplaceById(std::vector<T> &list, T &&item, QString T::*id) {
	list.erase(
		ranges::remove(list, item.*id, id),
		end(list));
	list.push_back(std::move(item));
}

// 18 验收「用调试代码各演示一次」: the five extension points, dev builds only.
constexpr auto kDebugCard = "debug_card";

void RegisterDebugDemos() {
	if (!Features::IsDevBuild()) {
		return;
	}
	RegisterQuickButton({
		.id = u"debug_button"_q,
		.titleZh = "测试按钮",
		.titleEn = "Test button",
		.handler = [](not_null<Host*> host) {
			host->sessionController()->showToast(
				u"Quick action: "_q + host->conversation().id);
		},
		.order = 1000,
	});
	RegisterInputInterceptor({
		.id = u"debug_commands"_q,
		.matches = [](const QString &text) {
			return text.startsWith(u"/echo"_q)
				|| text.startsWith(u"/card"_q)
				|| text.startsWith(u"/unknown"_q);
		},
		.handle = [](not_null<Host*> host, const QString &text) {
			const auto space = text.indexOf(QChar(' '));
			const auto command = (space < 0) ? text : text.left(space);
			const auto rest = (space < 0) ? QString() : text.mid(space + 1);
			auto user = Conversations::Message();
			user.id = NewId();
			user.role = QString::fromLatin1(Conversations::kRoleUser);
			user.text = text;
			user.time = NowMs();
			host->addMessage(std::move(user));
			auto reply = Conversations::Message();
			reply.id = NewId();
			reply.role = QString::fromLatin1(Conversations::kRoleAssistant);
			reply.text = rest;
			reply.time = NowMs();
			if (command == u"/echo"_q) {
				reply.text = u"echo: "_q + rest;
			} else if (command == u"/card"_q) {
				reply.type = QString::fromLatin1(kDebugCard);
				reply.data.insert(u"created"_q, double(NowMs()));
			} else {
				reply.type = u"debug_not_registered"_q;
			}
			host->addMessage(std::move(reply));
		},
	});
	RegisterTopEntry({
		.id = u"debug_conversation"_q,
		.titleZh = "测试对话",
		.titleEn = "Test chat",
		.open = [](not_null<Window::SessionController*> controller) {
			const auto conversation = Store::For(
				&controller->session()
			).findOrCreate(
				u"debug_demo"_q,
				QString::fromLatin1(kTaskChat),
				u"Always answer in exactly one sentence."_q,
				QString::fromUtf8("测试对话 / Test chat"));
			OpenAssistant(controller, conversation.id);
		},
		.order = 1000,
	});
	RegisterMessageType({
		.type = QString::fromLatin1(kDebugCard),
		.create = [](
				not_null<QWidget*> parent,
				not_null<Host*>,
				const Conversations::Message &message)
		-> object_ptr<Ui::RpWidget> {
			return object_ptr<Ui::FlatLabel>(
				parent,
				QString::fromUtf8("🧪 debug_card\n")
					+ message.text.value_or(QString()),
				st::luckgramAiTextLabel);
		},
		.contextText = [](const Conversations::Message &) {
			return QString(); // Not sent to the model.
		},
	});
}

} // namespace

Host *FindOpenHost(const QString &conversationId) {
	for (const auto host : Get().open) {
		if (host->conversation().id == conversationId) {
			return host;
		}
	}
	return nullptr;
}

void AddOpenHost(not_null<Host*> host) {
	if (!ranges::contains(Get().open, host)) {
		Get().open.push_back(host);
	}
}

void RemoveOpenHost(not_null<Host*> host) {
	auto &open = Get().open;
	open.erase(ranges::remove(open, host), end(open));
}

void RegisterQuickButton(QuickButton button) {
	ReplaceById(Get().buttons, std::move(button), &QuickButton::id);
}

void RegisterInputInterceptor(InputInterceptor interceptor) {
	ReplaceById(
		Get().interceptors,
		std::move(interceptor),
		&InputInterceptor::id);
}

void RegisterTopEntry(TopEntry entry) {
	ReplaceById(Get().entries, std::move(entry), &TopEntry::id);
}

void RegisterMessageType(MessageType type) {
	ReplaceById(Get().types, std::move(type), &MessageType::type);
}

std::vector<QuickButton> QuickButtons() {
	auto result = std::vector<QuickButton>();
	for (const auto &button : Get().buttons) {
		if (!button.visible || button.visible()) {
			result.push_back(button);
		}
	}
	ranges::stable_sort(result, ranges::less(), &QuickButton::order);
	return result;
}

std::vector<TopEntry> TopEntries() {
	auto result = std::vector<TopEntry>();
	for (const auto &entry : Get().entries) {
		if (!entry.visible || entry.visible()) {
			result.push_back(entry);
		}
	}
	ranges::stable_sort(result, ranges::less(), &TopEntry::order);
	return result;
}

const InputInterceptor *FindInterceptor(const QString &text) {
	for (const auto &interceptor : Get().interceptors) {
		if ((!interceptor.enabled || interceptor.enabled())
			&& interceptor.matches
			&& interceptor.matches(text)) {
			return &interceptor;
		}
	}
	return nullptr;
}

const MessageType *FindMessageType(const QString &type) {
	for (const auto &registered : Get().types) {
		if (registered.type == type) {
			return &registered;
		}
	}
	return nullptr;
}

QString ContextTextOf(const Conversations::Message &message) {
	if (message.type == QLatin1String(Conversations::kTypeText)) {
		return Conversations::DefaultContextText(message);
	}
	const auto registered = FindMessageType(message.type);
	return (registered && registered->contextText)
		? registered->contextText(message)
		: QString();
}

void RegisterAll() {
	static auto registered = false;
	if (registered) {
		return;
	}
	registered = true;

	// 18 需求 3: the four built-in quick buttons.
	const auto prefix = [](const char *id, const char *zh, const char *en, int i) {
		const auto &prefixes = Text::QuickPrefixes();
		RegisterQuickButton({
			.id = QString::fromLatin1(id),
			.titleZh = zh,
			.titleEn = en,
			.prefixZh = prefixes[i],
			.prefixEn = prefixes[i + 4],
			.order = i,
		});
	};
	prefix("summary", "总结", "Summarize", 0);
	prefix("rewrite", "改写", "Rewrite", 1);
	prefix("explain", "讲解", "Explain", 2);
	prefix("solve", "解题", "Solve", 3);

	RegisterDebugDemos();
	// ---- features that extend the assistant register here (22, 23, 30) ----
	Agents::Register(); // 22: 「智能体」 at the top of the assistant page.
	Draw::Register(); // 23 AI 画图
}

} // namespace Luckgram::AI
