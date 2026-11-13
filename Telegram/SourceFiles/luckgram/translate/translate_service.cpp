/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/translate/translate_service.h"

#include "base/call_delayed.h"
#include "base/timer.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_msg_id.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
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
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
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

[[nodiscard]] int64 Now() {
	return QDateTime::currentMSecsSinceEpoch();
}

// Per account, encrypted with its local key (D2 Luckgram::Storage).
constexpr auto kResultsName = "translate_cache";
constexpr auto kStatesName = "translate_state";

struct Shown {
	QString original; // Shown only while the message text is the same.
	TextWithEntities display;
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
	std::map<FullMsgId, Shown> shown;
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

[[nodiscard]] TextWithEntities BuildDisplay(
		const TextWithEntities &original,
		const QString &translated,
		const QString &header) {
	// 「已翻译 · 英语 → 中文」 and the translation in a quote block below the
	// original: the desktop form of mobile's separator inside the bubble.
	auto tail = TextWithEntities{ header };
	tail.entities.push_back(
		EntityInText(EntityType::Italic, 0, header.size()));
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
		.display = BuildDisplay(original, translated, HeaderFor(from, to)),
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

[[nodiscard]] QString ChatKey(not_null<PeerData*> peer) {
	return QString::fromLatin1(kChatToLangPrefix)
		+ QString::number(peer->session().uniqueId())
		+ QChar('.')
		+ QString::number(peer->id.value);
}

} // namespace

QString ChatTarget(not_null<PeerData*> peer) {
	return TargetLanguage(Settings::GetString(ChatKey(peer)));
}

void SetChatTarget(not_null<PeerData*> peer, const QString &language) {
	if (language.trimmed().isEmpty()) {
		Settings::Remove(ChatKey(peer));
	} else {
		Settings::SetString(ChatKey(peer), language.trimmed());
	}
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
			original,
			saved->text,
			HeaderFor(languages.from, languages.to)),
	}).first;
	return &j->second.display;
}

bool IsShown(not_null<const HistoryItem*> item) {
	return ShownText(item) != nullptr;
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

void ChooseLanguage(
		not_null<Window::SessionController*> controller,
		const QString &current,
		Fn<void(QString)> chosen) {
	auto selected = std::vector<LanguageId>();
	if (!current.isEmpty()) {
		selected.push_back(LanguageId::FromName(current));
	}
	controller->show(Box(
		Ui::ChooseLanguageBox,
		TrValue("翻译成", "Translate to"),
		[=](std::vector<LanguageId> list) {
			if (!list.empty() && chosen) {
				chosen(list.front().twoLetterCode());
			}
		},
		std::move(selected),
		false,
		nullptr));
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
		store->shown.clear();
	}
}

} // namespace Luckgram::Translate
