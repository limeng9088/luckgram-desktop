/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/translate/translate_service.h"

#include "base/call_delayed.h"
#include "base/timer.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/core_settings.h"
#include "data/data_msg_id.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/history_item.h"
#include "lang/translate_mtproto_provider.h"
#include "luckgram/luckgram_ai.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_double_bottom.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_storage.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/translate/translate_logic.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "spellcheck/platform/platform_language.h"
#include "ui/boxes/choose_language_box.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/toast/toast.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <translate_provider.h>

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Luckgram::Translate {
namespace {

constexpr auto kSaveDelay = crl::time(2000);
constexpr auto kToLangKey = "translate_in_to_lang";
constexpr auto kChatToLangPrefix = "translate_in_chat_to_lang.";
constexpr auto kRecentKey = "translate_in_recent_langs"; // Codes only.

[[nodiscard]] int64 Now() {
	return QDateTime::currentMSecsSinceEpoch();
}

// Per account, encrypted with its local key (D2 Luckgram::Storage).
constexpr auto kResultsName = "translate_cache";
constexpr auto kStatesName = "translate_state";
constexpr auto kChatLanguagesName = "translate_chat_langs";
constexpr auto kOutLanguagesName = "translate_out_chat_langs";

struct Shown {
	QString original; // Shown only while the message text is the same.
	TextWithEntities display;
};

struct Detected {
	size_t textHash = 0;
	QString language;
};

struct Store {
	explicit Store(not_null<Main::Session*> session)
	: session(session)
	, saveTimer([=] { save(); }) {
	}

	// Loads once the account storage is usable: not before its local key
	// exists, not while it is hidden in the second space (D8). Until then
	// everything stays in memory and nothing is written, so the saved file
	// is never replaced by a partial state.
	void ensureLoaded() {
		if (loaded || !Storage::AccountAvailable(session)) {
			return;
		}
		loaded = true;
		const auto now = Now();
		results = Cache::Parse(
			Storage::ReadAccount(session, QString::fromLatin1(kResultsName)),
			now);
		states = Cache::Parse(
			Storage::ReadAccount(session, QString::fromLatin1(kStatesName)),
			now);
		auto saved = DecodeChatLanguages(Storage::ReadAccount(
			session,
			QString::fromLatin1(kChatLanguagesName)));
		// #25-#43 kept them as plain luckgram.json keys: move all of this
		// account's at once (the encrypted value wins).
		const auto prefix = QString::fromLatin1(kChatToLangPrefix)
			+ QString::number(session->uniqueId())
			+ QChar('.');
		const auto legacy = Settings::KeysWithPrefix(prefix);
		for (const auto &key : legacy) {
			const auto peerId = LegacyChatPeerId(key, prefix);
			const auto code = TargetLanguage(Settings::GetString(key));
			if (peerId && !code.isEmpty()) {
				saved.emplace(*peerId, code);
			}
		}
		// Choices made before loading are newer than the saved ones.
		chatLanguages = MergeChatLanguages(
			std::move(saved),
			chatLanguages,
			chatLanguagesRemoved);
		chatLanguagesRemoved.clear();
		outLanguages = MergeChatLanguages(
			DecodeChatLanguages(Storage::ReadAccount(
				session,
				QString::fromLatin1(kOutLanguagesName))),
			outLanguages,
			outLanguagesRemoved);
		outLanguagesRemoved.clear();
		if (!legacy.isEmpty()) {
			save(); // Encrypted copy queued first, then the plain keys go.
			for (const auto &key : legacy) {
				Settings::Remove(key);
			}
		}
	}
	void scheduleSave() {
		if (loaded && !saveTimer.isActive()) {
			saveTimer.callOnce(kSaveDelay);
		}
	}
	void save() {
		if (!loaded) {
			return;
		}
		const auto now = Now();
		results.prune(now);
		states.prune(now);
		Storage::WriteAccount(
			session,
			QString::fromLatin1(kResultsName),
			results.serialize());
		Storage::WriteAccount(
			session,
			QString::fromLatin1(kStatesName),
			states.serialize());
		Storage::WriteAccount(
			session,
			QString::fromLatin1(kChatLanguagesName),
			EncodeChatLanguages(chatLanguages));
		Storage::WriteAccount(
			session,
			QString::fromLatin1(kOutLanguagesName),
			EncodeChatLanguages(outLanguages));
	}
	void flush() {
		if (saveTimer.isActive()) {
			saveTimer.cancel();
			save();
		}
	}

	const not_null<Main::Session*> session;
	bool loaded = false;
	Cache results;
	Cache states; // State key -> translated text, from>to languages.
	std::map<int64_t, QString> chatLanguages; // specs#105, peer id -> code.
	std::set<int64_t> chatLanguagesRemoved; // Reset before the load.
	std::map<int64_t, QString> outLanguages; // 01 C.3, peer id -> code.
	std::set<int64_t> outLanguagesRemoved;
	std::map<FullMsgId, Shown> shown;
	std::map<FullMsgId, Detected> detected; // A.2 icon, memory only.
	base::Timer saveTimer;
};

using StoreMap = std::map<not_null<Main::Session*>, std::unique_ptr<Store>>;

[[nodiscard]] StoreMap &Stores() {
	static const auto result = new StoreMap();
	return *result;
}

void SubscribeToSecondSpace() {
	static auto lifetime = (rpl::lifetime*)nullptr;
	if (lifetime) {
		return;
	}
	lifetime = new rpl::lifetime();
	// Second space switched: load what was not readable before.
	DoubleBottom::ActiveChanges(
	) | rpl::on_next([](bool) {
		for (const auto &entry : Stores()) {
			entry.second->ensureLoaded();
		}
	}, *lifetime);
}

[[nodiscard]] Store &GetStore(not_null<Main::Session*> session) {
	auto &stores = Stores();
	auto i = stores.find(session);
	if (i == end(stores)) {
		SubscribeToSecondSpace();
		i = stores.emplace(session, std::make_unique<Store>(session)).first;
		session->lifetime().add([=] {
			auto &stores = Stores();
			const auto j = stores.find(session);
			if (j != end(stores)) {
				j->second->flush();
				stores.erase(j);
			}
		});
	}
	i->second->ensureLoaded();
	return *i->second;
}

[[nodiscard]] Store &GetStore(not_null<const HistoryItem*> item) {
	return GetStore(&item->history()->session());
}

[[nodiscard]] QString StateKeyFor(not_null<const HistoryItem*> item) {
	const auto history = item->history();
	return StateKey(
		history->session().uniqueId(),
		int64(history->peer->id.value),
		int64(item->id.bare),
		Cache::Key(QString(), item->originalText().text));
}

[[nodiscard]] std::shared_ptr<Ui::Show> ActiveShow() {
	if (const auto window = Core::App().activeWindow()) {
		return window->uiShow();
	}
	return nullptr;
}

void ShowFailure(const Api::Error &error) {
	if (const auto show = ActiveShow()) {
		show->showToast(
			FailureMessage(FailureKind(error.code), IsChinese()));
	}
}

[[nodiscard]] QString HeaderFor(const QString &from, const QString &to);

[[nodiscard]] TextWithEntities BuildDisplay(
		not_null<const HistoryItem*> item,
		const TextWithEntities &original,
		const QString &translated,
		const QString &from,
		const QString &to) {
	// 「已翻译 · 英语 → 中文  🌐 中文 ▾」 and the translation in a quote block
	// below the original: the desktop form of mobile's separator inside the
	// bubble. The 「🌐 中文 ▾」 link changes the language (specs#105).
	const auto header = HeaderFor(from, to);
	auto tail = TextWithEntities{ header };
	tail.entities.push_back(
		EntityInText(EntityType::Italic, 0, header.size()));
	const auto button = LanguageButton(LanguageName(to));
	tail.append(u"  "_q);
	const auto offset = tail.text.size();
	tail.append(button);
	tail.entities.push_back(EntityInText(
		EntityType::CustomUrl,
		offset,
		button.size(),
		LanguageLink(
			int64_t(item->history()->peer->id.value),
			int64_t(item->id.bare))));
	tail.append(QChar('\n')).append(translated);
	tail.entities.insert(
		tail.entities.begin(),
		EntityInText(EntityType::Blockquote, 0, tail.text.size()));
	auto result = original;
	if (!result.text.isEmpty()) {
		result.append(QChar('\n'));
	}
	return result.append(std::move(tail));
}

[[nodiscard]] QString DetectedLanguage(const QString &text) {
	const auto id = Platform::Language::Recognize(text);
	return id.known() ? id.twoLetterCode() : QString();
}

[[nodiscard]] QString HeaderFor(const QString &from, const QString &to) {
	return Header(
		from.isEmpty() ? QString() : LanguageName(from),
		LanguageName(to),
		IsChinese());
}

void Remember(
		not_null<HistoryItem*> item,
		const QString &translated,
		const QString &from,
		const QString &to) {
	auto &store = GetStore(item);
	const auto original = item->originalText();
	store.shown[item->fullId()] = Shown{
		.original = original.text,
		.display = BuildDisplay(item, original, translated, from, to),
	};
	if (item->isRegular()) {
		store.states.put(
			StateKeyFor(item),
			translated,
			EncodeLanguages(from, to),
			Now());
		store.scheduleSave();
	}
	item->history()->owner().requestItemTextRefresh(item);
}

// Luckgram AI translation (POST /v1/ai/chat task=translate).
void ViaAi(
		not_null<Main::Session*> session,
		const char *source,
		const QString &text,
		const QString &to,
		Fn<void(QString text, QString from)> done,
		Fn<void(Api::Error)> fail) {
	const auto lang = AiLanguage(to);
	if (text.trimmed().isEmpty() || lang.isEmpty()) {
		fail({ .code = u"invalid_request"_q });
		return;
	}
	auto &store = GetStore(session);
	const auto key = Cache::Key(lang, text);
	if (const auto hit = store.results.get(key, Now())) {
		done(hit->text, hit->fromLang);
		return;
	}
	const auto max = Settings::RemoteNumber(u"ai.max_input_chars"_q, 60000);
	if (text.size() > max) {
		fail({ .code = u"input_too_large"_q });
		return;
	}
	const auto name = QString::fromLatin1(source);
	const auto weak = base::make_weak(session);
	const auto send = [=] {
		auto message = QJsonObject();
		message.insert(u"role"_q, u"user"_q);
		message.insert(u"content"_q, text);
		auto body = QJsonObject();
		body.insert(u"task"_q, u"translate"_q);
		body.insert(u"to_lang"_q, lang);
		body.insert(u"stream"_q, false);
		body.insert(u"messages"_q, QJsonArray{ message });
		Api::Send({
			.method = "POST",
			.path = u"/v1/ai/chat"_q,
			.body = QJsonDocument(body).toJson(QJsonDocument::Compact),
			.contentType = "application/json",
		}, [=](Api::Response response) {
			const auto json = response.json();
			const auto result = json.value(u"text"_q).toString();
			if (result.trimmed().isEmpty()) {
				LOG(("Luckgram translate: Luckgram AI empty, source=%1"
					).arg(name));
				fail({ .code = QString::fromLatin1(
					Api::kUpstreamUnavailable) });
				return;
			}
			const auto from = AiLanguage(
				json.value(u"from_lang"_q).toString());
			LOG(("Luckgram translate: Luckgram AI ok, source=%1").arg(name));
			if (const auto strong = weak.get()) {
				auto &store = GetStore(strong);
				store.results.put(key, result, from, Now());
				store.scheduleSave();
			}
			done(result, from);
		}, [=](Api::Error error) {
			LOG(("Luckgram translate: Luckgram AI failed, source=%1 code=%2"
				).arg(name, error.code));
			fail(error);
		});
	};
	if (AI::HasConsent()) {
		send();
	} else if (const auto show = ActiveShow()) {
		AI::WithConsent(show, send, [=] {
			fail({ .code = QString::fromLatin1(Api::kConsentDenied) });
		});
	} else {
		fail({ .code = QString::fromLatin1(Api::kConsentDenied) });
	}
}

class Provider final : public Ui::TranslateProvider {
public:
	Provider(not_null<Main::Session*> session, const char *source)
	: _session(session)
	, _telegram(Ui::CreateMTProtoTranslateProvider(session))
	, _source(source) {
	}
	~Provider() {
		*_alive = false;
	}

	[[nodiscard]] bool supportsMessageId() const override {
		return true;
	}

	void request(
			Ui::TranslateProviderRequest request,
			LanguageId to,
			Fn<void(Ui::TranslateProviderResult)> done) override {
		struct State {
			bool finished = false;
		};
		const auto state = std::make_shared<State>();
		const auto alive = _alive;
		const auto source = _source;
		const auto name = QString::fromLatin1(source);
		auto text = request.text.text;
		if (text.isEmpty() && request.msgId) {
			const auto peer = PeerId(request.peerId);
			if (const auto item = _session->data().message(
					peer,
					MsgId(request.msgId))) {
				text = item->originalText().text;
			}
		}
		const auto target = to.twoLetterCode();
		const auto session = _session;
		const auto fallback = [=](QString reason) {
			if (!*alive || state->finished) {
				return;
			}
			state->finished = true;
			LOG(("Luckgram translate: telegram failed, source=%1 error=%2"
				" -> Luckgram AI").arg(name, reason));
			ViaAi(session, source, text, target, [=](QString result, QString) {
				if (*alive) {
					done({ .text = TextWithEntities{ result } });
				}
			}, [=](Api::Error error) {
				ShowFailure(error);
				if (*alive) {
					done({ .error = Ui::TranslateProviderError::Unknown });
				}
			});
		};
		base::call_delayed(kTelegramTimeoutMs, [=] {
			fallback(TelegramFailure(QString(), true));
		});
		_telegram->request(std::move(request), to, [=](
				Ui::TranslateProviderResult result) {
			if (!*alive || state->finished) {
				return; // Late answer after the AI fallback started.
			}
			const auto gave = result.text && !result.text->text.isEmpty();
			if (!ShouldFallback(gave)) {
				state->finished = true;
				done(std::move(result));
				return;
			}
			fallback(TelegramFailure(
				(result.error == Ui::TranslateProviderError::None)
					? QString()
					: u"ERROR"_q,
				false));
		});
	}

private:
	const not_null<Main::Session*> _session;
	const std::unique_ptr<Ui::TranslateProvider> _telegram;
	const char * const _source;
	const std::shared_ptr<bool> _alive = std::make_shared<bool>(true);

};

void RequestInto(
		not_null<HistoryItem*> item,
		const QString &to) {
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	const auto original = item->originalText();
	const auto provider = std::make_shared<Provider>(
		session,
		kSourceMessage);
	const auto weak = base::make_weak(session);
	provider->request({
		.peerId = uint64(item->history()->peer->id.value),
		.msgId = IsServerMsgId(id.msg) ? id.msg.bare : 0,
		.text = original,
	}, LanguageId::FromName(to), [=, keep = provider](
			Ui::TranslateProviderResult result) {
		const auto strong = weak.get();
		if (!strong || !result.text || result.text->text.isEmpty()) {
			return;
		}
		if (const auto item = strong->data().message(id)) {
			if (item->originalText().text == original.text) {
				Remember(
					item,
					result.text->text,
					DetectedLanguage(original.text),
					to);
			}
		}
	});
}

} // namespace

std::unique_ptr<Ui::TranslateProvider> MaybeCreateProvider(
		not_null<Main::Session*> session) {
	if (!InEnabled()) {
		return nullptr;
	}
	return std::make_unique<Provider>(session, kSourceChat);
}

bool InEnabled() {
	return Settings::IsEnabled(QString::fromLatin1(kFeatureIn));
}

QString DefaultTarget() {
	return TargetLanguage(Settings::GetString(QString::fromLatin1(kToLangKey)));
}

void SetDefaultTarget(const QString &language) {
	Settings::SetString(QString::fromLatin1(kToLangKey), language.trimmed());
}

namespace {

// #25-#43 kept the chat languages in plain luckgram.json, moved to the
// encrypted storage when it loads (Store::ensureLoaded).
[[nodiscard]] QString LegacyChatKey(not_null<PeerData*> peer) {
	return QString::fromLatin1(kChatToLangPrefix)
		+ QString::number(peer->session().uniqueId())
		+ QChar('.')
		+ QString::number(peer->id.value);
}

} // namespace

QString ChatTarget(not_null<PeerData*> peer) {
	auto &store = GetStore(&peer->session());
	const auto peerId = int64_t(peer->id.value);
	const auto i = store.chatLanguages.find(peerId);
	if (i != end(store.chatLanguages)) {
		return TargetLanguage(i->second);
	}
	// Not loaded yet (or hidden): an old plain key is still in place.
	return (store.loaded || store.chatLanguagesRemoved.contains(peerId))
		? QString()
		: TargetLanguage(Settings::GetString(LegacyChatKey(peer)));
}

void SetChatTarget(not_null<PeerData*> peer, const QString &language) {
	auto &store = GetStore(&peer->session());
	const auto peerId = int64_t(peer->id.value);
	const auto code = TargetLanguage(language);
	if (code.isEmpty()) {
		store.chatLanguages.erase(peerId);
		if (!store.loaded) {
			store.chatLanguagesRemoved.emplace(peerId);
		}
	} else {
		store.chatLanguages[peerId] = code;
		store.chatLanguagesRemoved.erase(peerId);
	}
	store.scheduleSave();
	if (store.loaded) {
		Settings::Remove(LegacyChatKey(peer));
	}
}

QString OutChatTarget(not_null<PeerData*> peer) {
	const auto &languages = GetStore(&peer->session()).outLanguages;
	const auto i = languages.find(int64_t(peer->id.value));
	return (i != end(languages)) ? TargetLanguage(i->second) : QString();
}

void SetOutChatTarget(not_null<PeerData*> peer, const QString &language) {
	auto &store = GetStore(&peer->session());
	const auto peerId = int64_t(peer->id.value);
	const auto code = TargetLanguage(language);
	if (code.isEmpty()) {
		store.outLanguages.erase(peerId);
		if (!store.loaded) {
			store.outLanguagesRemoved.emplace(peerId);
		}
	} else {
		store.outLanguages[peerId] = code;
		store.outLanguagesRemoved.erase(peerId);
	}
	store.scheduleSave();
}

QString LastIncomingLanguage(not_null<History*> history) {
	constexpr auto kMaxChecked = 50;
	auto checked = 0;
	for (auto i = history->blocks.rbegin(); i != history->blocks.rend(); ++i) {
		const auto &messages = (*i)->messages;
		for (auto j = messages.rbegin(); j != messages.rend(); ++j) {
			const auto item = (*j)->data();
			if (item->out() || item->isService()) {
				continue;
			} else if (++checked > kMaxChecked) {
				return QString();
			}
			const auto &text = item->originalText().text;
			if (!text.trimmed().isEmpty()) {
				const auto language = DetectedLanguage(text);
				if (!language.isEmpty()) {
					return language;
				}
			}
		}
	}
	return QString();
}

void TranslateText(
		not_null<Main::Session*> session,
		const TextWithEntities &text,
		const QString &to,
		Fn<void(TextWithEntities)> done,
		Fn<void()> fail,
		const char *source) {
	const auto provider = std::make_shared<Provider>(
		session,
		source ? source : kSourceOut);
	provider->request({
		.text = text,
	}, LanguageId::FromName(to), [=, keep = provider](
			Ui::TranslateProviderResult result) {
		if (result.text && !result.text->text.trimmed().isEmpty()) {
			done(std::move(*result.text));
		} else if (fail) {
			fail(); // The failure toast is shown by the provider.
		}
	});
}

QString EffectiveTarget(not_null<PeerData*> peer) {
	return EffectiveLanguage(ChatTarget(peer), DefaultTarget());
}

QStringList DoNotTranslate() {
	auto result = QStringList();
	for (const auto &id : Core::App().settings().skipTranslationLanguages()) {
		result.push_back(id.twoLetterCode());
	}
	return result;
}

const TextWithEntities *ShownText(not_null<const HistoryItem*> item) {
	auto &store = GetStore(item);
	if ((store.shown.empty() && !store.states.size())
		|| item->isService()
		|| !InEnabled()) {
		return nullptr; // Cheapest first: called on every text layout.
	}
	const auto &original = item->originalText();
	const auto i = store.shown.find(item->fullId());
	if (i != end(store.shown)) {
		return (i->second.original == original.text)
			? &i->second.display
			: nullptr;
	} else if (!store.states.size() || !item->isRegular()) {
		return nullptr;
	}
	// Translated before (this or an earlier run): show it again.
	const auto saved = store.states.get(StateKeyFor(item), Now());
	if (!saved) {
		return nullptr;
	}
	const auto languages = DecodeLanguages(saved->fromLang);
	const auto j = store.shown.emplace(item->fullId(), Shown{
		.original = original.text,
		.display = BuildDisplay(
			item,
			original,
			saved->text,
			languages.from,
			languages.to),
	}).first;
	return &j->second.display;
}

bool IsShown(not_null<const HistoryItem*> item) {
	return ShownText(item) != nullptr;
}

bool ShowIcon(not_null<const HistoryItem*> item) {
	if (!InEnabled()
		|| item->out()
		|| item->isService()
		|| item->isSponsored()
		|| !item->isRegular()) {
		return false;
	}
	const auto &text = item->originalText().text;
	if (text.trimmed().isEmpty()) {
		return false;
	}
	// Called on every layout: detect once per message text.
	constexpr auto kMaxDetected = 4096;
	auto &detected = GetStore(item).detected;
	const auto hash = qHash(text);
	auto i = detected.find(item->fullId());
	if (i == end(detected) || i->second.textHash != hash) {
		if (i == end(detected) && detected.size() >= kMaxDetected) {
			detected.clear();
		}
		i = detected.insert_or_assign(item->fullId(), Detected{
			.textHash = hash,
			.language = DetectedLanguage(text),
		}).first;
	}
	return ShowButton(
		true,
		false,
		true,
		i->second.language,
		DoNotTranslate(),
		EffectiveTarget(item->history()->peer));
}

ClickHandlerPtr IconLink(not_null<const HistoryItem*> item) {
	class Link final : public LambdaClickHandler {
	public:
		Link(
			not_null<Main::Session*> session,
			FullMsgId id,
			Fn<void(ClickContext)> handler)
		: LambdaClickHandler(std::move(handler))
		, _session(session)
		, _id(id) {
		}
		QString tooltip() const override {
			const auto item = _session->data().message(_id);
			return MenuLabel(item && IsShown(item), IsChinese());
		}

	private:
		const not_null<Main::Session*> _session;
		const FullMsgId _id;

	};
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	return std::make_shared<Link>(session, id, [=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto controller = my.sessionWindow.get();
		if (!controller || &controller->session() != session) {
			return;
		}
		if (const auto item = session->data().message(id)) {
			Toggle(controller, item);
		}
	});
}

void Toggle(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	auto &store = GetStore(item);
	if (IsShown(item)) {
		store.shown.erase(item->fullId());
		if (item->isRegular()) {
			store.states.remove(StateKeyFor(item));
			store.scheduleSave();
		}
		item->history()->owner().requestItemTextRefresh(item);
		return;
	}
	const auto peer = item->history()->peer;
	const auto to = EffectiveTarget(peer);
	if (!to.isEmpty()) {
		RequestInto(item, to);
		return;
	}
	// specs#93: the first translation asks for the language.
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	ChooseLanguage(controller, QString(), [=](QString code) {
		SetDefaultTarget(code);
		if (const auto item = session->data().message(id)) {
			RequestInto(item, code);
		}
	});
}

bool AddMessageMenuAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	if (!InEnabled()
		|| item->isService()
		|| item->originalText().text.trimmed().isEmpty()) {
		return false;
	}
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	const auto weak = base::make_weak(controller);
	menu->addAction(MenuLabel(IsShown(item), IsChinese()), [=] {
		const auto strong = weak.get();
		const auto item = session->data().message(id);
		if (strong && item) {
			Toggle(strong, item);
		}
	}, &st::menuIconTranslate);
	return true;
}

namespace {

[[nodiscard]] QStringList Recent() {
	return Settings::GetString(QString::fromLatin1(kRecentKey)).split(
		QChar(','),
		Qt::SkipEmptyParts);
}

void PushToRecent(const QString &language) {
	Settings::SetString(
		QString::fromLatin1(kRecentKey),
		PushRecent(Recent(), language).join(QChar(',')));
}

// specs#105 2: 「仅本聊天 / 设为默认」.
void AskScope(
		not_null<Window::SessionController*> controller,
		const QString &language,
		Fn<void(Scope)> done) {
	const auto name = LanguageName(language);
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(TrValue("翻译语言", "Translation language"));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(IsChinese()
				? (u"以后翻译成%1：只用于这个聊天，还是设为默认？"_q).arg(name)
				: (u"Translate into %1 from now on: in this chat only, "
					"or by default?"_q).arg(name)),
			st::boxLabel));
		box->addButton(TrValue("设为默认", "Set as default"), [=] {
			done(Scope::Default);
			box->closeBox();
		});
		box->addButton(TrValue("仅本聊天", "This chat only"), [=] {
			done(Scope::ThisChat);
			box->closeBox();
		});
	}));
}

} // namespace

void ChooseLanguage(
		not_null<Window::SessionController*> controller,
		const QString &current,
		Fn<void(QString)> chosen) {
	// specs#105 3: the current and the recent languages go first.
	auto selected = std::vector<LanguageId>();
	auto codes = Recent();
	if (!current.isEmpty()) {
		codes.push_front(current);
	}
	for (const auto &code : codes) {
		const auto id = LanguageId::FromName(code);
		if (id.known() && !ranges::contains(selected, id)) {
			selected.push_back(id);
		}
	}
	controller->show(Box(
		Ui::ChooseLanguageBox,
		TrValue("翻译成", "Translate to"),
		[=](std::vector<LanguageId> list) {
			if (!list.empty() && chosen) {
				const auto code = list.front().twoLetterCode();
				PushToRecent(code);
				chosen(code);
			}
		},
		std::move(selected),
		false,
		nullptr));
}

bool HandleLanguageLink(
		not_null<Window::SessionController*> controller,
		const QString &link) {
	const auto target = ParseLanguageLink(link);
	if (!target || !InEnabled()) {
		return false;
	}
	const auto session = &controller->session();
	const auto peerId = PeerId(PeerIdHelper(uint64(target->peerId)));
	const auto id = FullMsgId(peerId, MsgId(target->msgId));
	const auto item = session->data().message(id);
	if (!item) {
		return false;
	}
	const auto peer = item->history()->peer;
	ChooseLanguage(controller, EffectiveTarget(peer), [=](QString code) {
		const auto strong = session->data().message(id);
		if (!strong) {
			return;
		}
		AskScope(controller, code, [=](Scope scope) {
			if (scope == Scope::Default) {
				SetDefaultTarget(code);
			}
			SetChatTarget(peer, ChatOverrideAfter(scope, code));
			if (const auto item = session->data().message(id)) {
				RequestInto(item, code); // Re-translate this one now.
			}
		});
	});
	return true;
}

void AddChatMenuAction(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const Ui::Menu::MenuCallback &addAction) {
	if (!InEnabled() || peer->isSelf()) {
		return;
	}
	const auto weak = base::make_weak(controller);
	addAction(Tr("翻译设置…", "Translation settings…"), [=] {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		// The chat's own reading language (specs#105 1).
		ChooseLanguage(strong, EffectiveTarget(peer), [=](QString code) {
			SetChatTarget(peer, code);
			if (const auto strong = weak.get()) {
				strong->showToast(IsChinese()
					? (u"本聊天的译文语言：%1"_q).arg(LanguageName(code))
					: (u"Translation language in this chat: %1"_q).arg(
						LanguageName(code)));
			}
		});
	}, &st::menuIconTranslate);
}

QString LanguageName(const QString &code) {
	return Ui::LanguageName(LanguageId::FromName(code));
}

void ClearAll() {
	// The encrypted files go with each account (Storage::Account::reset).
	for (const auto &entry : Stores()) {
		const auto store = entry.second.get();
		store->saveTimer.cancel();
		store->results = Cache();
		store->states = Cache();
		store->chatLanguages.clear();
		store->chatLanguagesRemoved.clear();
		store->outLanguages.clear();
		store->outLanguagesRemoved.clear();
		store->shown.clear();
	}
}

} // namespace Luckgram::Translate
