/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/transcribe/luckgram_transcribe.h"

#include "api/api_transcribes.h"
#include "apiwrap.h"
#include "base/timer.h"
#include "core/application.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "luckgram/luckgram_ai.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_double_bottom.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_storage.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/transcribe/luckgram_transcribe_logic.h"
#include "luckgram/translate/translate_logic.h"
#include "luckgram/translate/translate_service.h"
#include "main/main_session.h"
#include "spellcheck/platform/platform_language.h"
#include "ui/text/text_utilities.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QDateTime>
#include <QtCore/QFile>

namespace Luckgram::Transcribe {
namespace {

using Translate::Cache;

constexpr auto kSaveDelay = crl::time(2000);
constexpr auto kResultsName = "transcribe_cache";
constexpr auto kTranslationsName = "transcribe_translations";

[[nodiscard]] int64 Now() {
	return QDateTime::currentMSecsSinceEpoch();
}

// Per account, in its encrypted storage (D2): loaded once the storage is
// usable (local key, not hidden in the second space), nothing written
// before that.
struct Store {
	explicit Store(not_null<Main::Session*> session)
	: session(session)
	, saveTimer([=] { save(); }) {
	}

	void ensureLoaded() {
		if (loaded || !Storage::AccountAvailable(session)) {
			return;
		}
		loaded = true;
		const auto now = Now();
		results = Cache::Parse(
			Storage::ReadAccount(session, QString::fromLatin1(kResultsName)),
			now);
		translations = Cache::Parse(
			Storage::ReadAccount(
				session,
				QString::fromLatin1(kTranslationsName)),
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
		translations.prune(now);
		Storage::WriteAccount(
			session,
			QString::fromLatin1(kResultsName),
			results.serialize());
		Storage::WriteAccount(
			session,
			QString::fromLatin1(kTranslationsName),
			translations.serialize());
	}
	void flush() {
		if (saveTimer.isActive()) {
			saveTimer.cancel();
			save();
		}
	}

	const not_null<Main::Session*> session;
	bool loaded = false;
	Cache results; // CacheKey -> transcript.
	Cache translations; // CacheKey -> translation, from>to languages.
	std::map<FullMsgId, Api::RequestPtr> requests;
	std::map<FullMsgId, rpl::lifetime> downloads;
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
				for (const auto &[id, request] : j->second->requests) {
					if (request) {
						request->cancel();
					}
				}
				j->second->flush();
				stores.erase(j);
			}
		});
	}
	i->second->ensureLoaded();
	return *i->second;
}

[[nodiscard]] QString KeyFor(not_null<const HistoryItem*> item) {
	return CacheKey(
		int64_t(item->history()->peer->id.value),
		int64_t(item->id.bare));
}

[[nodiscard]] DocumentData *DocumentOf(not_null<const HistoryItem*> item) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	return (document
		&& (document->isVoiceMessage() || document->isVideoMessage()))
		? document
		: nullptr;
}

[[nodiscard]] std::shared_ptr<Ui::Show> ActiveShow() {
	if (const auto window = Core::App().activeWindow()) {
		return window->uiShow();
	}
	return nullptr;
}

[[nodiscard]] QString DetectedLanguage(const QString &text) {
	const auto id = Platform::Language::Recognize(text);
	return id.known() ? id.twoLetterCode() : QString();
}

[[nodiscard]] QByteArray BytesOf(
		not_null<DocumentData*> document,
		const std::shared_ptr<Data::DocumentMedia> &media) {
	auto result = media->bytes();
	if (result.isEmpty()) {
		if (const auto path = document->filepath(true); !path.isEmpty()) {
			auto file = QFile(path);
			if (file.open(QIODevice::ReadOnly)) {
				result = file.readAll();
			}
		}
	}
	return result;
}

// Downloads the voice / round video if needed, then done(bytes).
void Download(
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document,
		Fn<void(QByteArray)> done,
		Fn<void()> fail) {
	const auto media = document->createMediaView();
	if (media->loaded()) {
		done(BytesOf(document, media));
		return;
	}
	const auto session = &document->session();
	const auto id = item->fullId();
	document->save(Data::FileOrigin(id), QString());
	auto &lifetime = GetStore(session).downloads[id];
	lifetime.destroy();
	session->downloaderTaskFinished(
	) | rpl::filter([=] {
		return media->loaded()
			|| (document->status == FileDownloadFailed)
			|| document->cancelled();
	}) | rpl::take(1) | rpl::on_next([=] {
		const auto ok = media->loaded();
		const auto bytes = ok ? BytesOf(document, media) : QByteArray();
		// Not from inside the subscription that is being destroyed.
		crl::on_main(session, [=] {
			GetStore(session).downloads.erase(id);
		});
		if (bytes.isEmpty()) {
			fail();
		} else {
			done(bytes);
		}
	}, lifetime);
}

void Upload(
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document,
		QByteArray bytes,
		Fn<void(QString)> done,
		Fn<void(bool)> fail) {
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	const auto maxMb = Settings::RemoteNumber(
		u"ai.transcribe_max_mb"_q,
		kDefaultMaxMb);
	if (TooLarge(bytes.size(), maxMb)) {
		fail(true);
		return;
	}
	const auto weak = base::make_weak(session);
	const auto finish = [=] {
		if (const auto strong = weak.get()) {
			GetStore(strong).requests.erase(id);
		}
	};
	auto request = AI::Transcribe(
		std::move(bytes),
		UploadMime(document->isVideoMessage(), document->mimeString()),
		QString(),
		[=](QString text) {
			finish();
			if (!weak.get()) {
				return;
			}
			LOG(("Luckgram transcribe: ok."));
			done(text);
		},
		[=](Api::Error error) {
			finish();
			if (!weak.get()) {
				return;
			}
			LOG(("Luckgram transcribe: failed, code=%1").arg(error.code));
			fail(error.code == u"input_too_large"_q
				|| error.code == u"file_too_large"_q);
		});
	GetStore(session).requests[id] = std::move(request);
}

[[nodiscard]] TextWithEntities TranslationBlock(
		const QString &translated,
		const QString &languages) {
	const auto decoded = Translate::DecodeLanguages(languages);
	const auto header = Translate::Header(
		(decoded.from.isEmpty()
			? QString()
			: Translate::LanguageName(decoded.from)),
		Translate::LanguageName(decoded.to),
		IsChinese());
	auto result = TextWithEntities{ header };
	result.entities.push_back(
		EntityInText(EntityType::Italic, 0, header.size()));
	result.append(QChar('\n')).append(translated);
	result.entities.insert(
		result.entities.begin(),
		EntityInText(EntityType::Blockquote, 0, result.text.size()));
	return result;
}

void ToggleTranslation(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	auto &store = GetStore(session);
	const auto key = KeyFor(item);
	if (store.translations.get(key, Now())) {
		store.translations.remove(key);
		store.scheduleSave();
		session->data().requestItemResize(item);
		return;
	}
	const auto result = session->api().transcribes().entry(item).result;
	if (result.trimmed().isEmpty()) {
		return;
	}
	const auto id = item->fullId();
	const auto weak = base::make_weak(session);
	const auto translate = [=](QString to) {
		Translate::TranslateText(session, { result }, to, [=](
				TextWithEntities translated) {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			auto &store = GetStore(strong);
			store.translations.put(
				key,
				translated.text,
				Translate::EncodeLanguages(DetectedLanguage(result), to),
				Now());
			store.scheduleSave();
			if (const auto item = strong->data().message(id)) {
				strong->data().requestItemResize(item);
			}
		}, nullptr, Translate::kSourceVoice);
	};
	// 01 B.1: same language rules as 01 A (this chat, else the default;
	// the first translation asks).
	const auto to = Translate::EffectiveTarget(item->history()->peer);
	if (!to.isEmpty()) {
		translate(to);
		return;
	}
	Translate::ChooseLanguage(controller, QString(), [=](QString code) {
		Translate::SetDefaultTarget(code);
		translate(code);
	});
}

} // namespace

bool FreeFor(not_null<const HistoryItem*> item) {
	const auto session = &item->history()->session();
	return item->isRegular()
		&& DocumentOf(item)
		&& UseLuckgram(
			Settings::IsEnabled(QString::fromLatin1(kFeature)),
			session->premium());
}

void Request(
		not_null<HistoryItem*> item,
		Fn<void(QString text)> done,
		Fn<void(bool tooLong)> fail) {
	const auto session = &item->history()->session();
	auto &store = GetStore(session);
	const auto key = KeyFor(item);
	if (const auto cached = store.results.get(key, Now())) {
		done(cached->text); // 17.3: no new request.
		return;
	}
	const auto document = DocumentOf(item);
	if (!document) {
		fail(false);
		return;
	}
	const auto id = item->fullId();
	const auto weak = base::make_weak(session);
	const auto save = [=](QString text) {
		if (const auto strong = weak.get()) {
			auto &store = GetStore(strong);
			store.results.put(key, text, DetectedLanguage(text), Now());
			store.scheduleSave();
		}
		done(text);
	};
	// Every path ends in done() or fail(), so the entry never stays
	// "loading" (a message deleted meanwhile counts as a failure).
	const auto run = [=] {
		const auto strong = weak.get();
		const auto item = strong ? strong->data().message(id) : nullptr;
		if (!item) {
			if (strong) {
				fail(false);
			}
			return;
		}
		Download(item, document, [=](QByteArray bytes) {
			const auto strong = weak.get();
			const auto item = strong ? strong->data().message(id) : nullptr;
			if (item) {
				Upload(item, document, std::move(bytes), save, fail);
			} else if (strong) {
				fail(false);
			}
		}, [=] {
			if (weak.get()) {
				fail(false);
			}
		});
	};
	// 17.7: the AI consent first.
	if (AI::HasConsent()) {
		run();
	} else if (const auto show = ActiveShow()) {
		AI::WithConsent(show, run, [=] {
			if (weak.get()) {
				fail(false);
			}
		});
	} else {
		fail(false);
	}
}

TextWithEntities ResultText(
		not_null<HistoryItem*> item,
		const QString &result) {
	auto text = TextWithEntities{ result };
	if (!Translate::InEnabled() || result.trimmed().isEmpty()) {
		return text;
	}
	const auto peer = item->history()->peer;
	const auto peerId = int64_t(peer->id.value);
	const auto msgId = int64_t(item->id.bare);
	const auto chinese = IsChinese();
	const auto link = [&](bool translated) {
		return Ui::Text::Link(
			Translate::MenuLabel(translated, chinese),
			Link(LinkAction::Translate, peerId, msgId));
	};
	auto &store = GetStore(&item->history()->session());
	if (const auto saved = store.translations.get(KeyFor(item), Now())) {
		text.append(QChar('\n'));
		text.append(TranslationBlock(saved->text, saved->fromLang));
		text.append(QChar('\n')).append(link(true));
	} else if (Translate::ShowButton(
			true,
			false,
			true,
			DetectedLanguage(result),
			Translate::DoNotTranslate(),
			Translate::EffectiveTarget(peer))) {
		// 01 B.2: not for a language in 「不翻译的语言」.
		text.append(QChar('\n')).append(link(false));
	}
	return text;
}

TextWithEntities FailedText(not_null<HistoryItem*> item) {
	const auto chinese = IsChinese();
	const auto failed = Transcribe::FailedText(chinese);
	auto result = TextWithEntities{ failed };
	result.entities.push_back(
		EntityInText(EntityType::Italic, 0, failed.size()));
	return result.append(Ui::Text::Link(
		RetryLabel(chinese),
		Link(
			LinkAction::Retry,
			int64_t(item->history()->peer->id.value),
			int64_t(item->id.bare))));
}

bool HandleLink(
		not_null<Window::SessionController*> controller,
		const QString &link) {
	const auto target = ParseLink(link);
	if (!target) {
		return false;
	}
	const auto session = &controller->session();
	const auto peerId = PeerId(PeerIdHelper(uint64(target->peerId)));
	const auto item = session->data().message(
		FullMsgId(peerId, MsgId(target->msgId)));
	if (!item || !FreeFor(item)) {
		return false;
	}
	switch (target->action) {
	case LinkAction::Retry:
		session->api().transcribes().toggle(item); // Failed: loads again.
		break;
	case LinkAction::Translate:
		ToggleTranslation(controller, item);
		break;
	}
	return true;
}

} // namespace Luckgram::Transcribe
