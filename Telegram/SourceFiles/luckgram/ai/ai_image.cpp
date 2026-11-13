/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_image.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "core/file_utilities.h"
#include "data/data_chat_participant_status.h"
#include "data/data_thread.h"
#include "lang/lang_keys.h"
#include "luckgram/ai/ai_conversation_logic.h"
#include "luckgram/ai/ai_image_logic.h"
#include "luckgram/ai/ai_registry.h"
#include "luckgram/ai/ai_store.h"
#include "luckgram/luckgram_ai.h"
#include "luckgram/luckgram_api.h"
#include "luckgram/luckgram_double_bottom.h"
#include "luckgram/luckgram_features.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_storage.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/pro/luckgram_pro.h"
#include "main/main_session.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/effects/radial_animation.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QtCore/QFile>
#include <QtGui/QImage>

namespace Luckgram::AI::Draw {
namespace {

using Message = Conversations::Message;

// The list of pictures this device wrote (Written), so the pictures of
// removed conversations can be deleted.
const auto kIndexName = u"ai_images"_q;
constexpr auto kCachedImages = 8;
constexpr auto kSide = 240; // As mobile.

[[nodiscard]] bool Enabled() {
	return Settings::IsEnabled(QString::fromLatin1(Features::kAiImage));
}

[[nodiscard]] int Scaled(int px) {
	return style::ConvertScale(px);
}

// Per account: the picture index, pictures that could not be written
// (storage unavailable: memory only), running requests, decoded pictures.
struct SessionState {
	~SessionState() {
		for (const auto &[id, request] : requests) {
			if (request) {
				request->cancel();
			}
		}
	}

	std::vector<Written> written;
	bool indexLoaded = false;
	base::flat_map<QString, QByteArray> memory;
	base::flat_map<QString, Api::RequestPtr> requests; // Message id.
	std::vector<std::pair<QString, QImage>> cache; // Newest last.
	rpl::lifetime lifetime;
};

[[nodiscard]] base::flat_map<
	not_null<Main::Session*>,
	std::unique_ptr<SessionState>> &States() {
	static auto result = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<SessionState>>();
	return result;
}

void LoadIndex(not_null<Main::Session*> session, SessionState &state) {
	if (state.indexLoaded || !Storage::AccountAvailable(session)) {
		return;
	}
	state.written = DeserializeIndex(
		Storage::ReadAccount(session, kIndexName));
	state.indexLoaded = true;
}

void WriteIndex(not_null<Main::Session*> session, const SessionState &state) {
	if (state.indexLoaded) {
		Storage::WriteAccount(session, kIndexName, SerializeIndex(state.written));
	}
}

[[nodiscard]] QStringList Referenced(
		const Conversations::Conversation &conversation) {
	auto result = QStringList();
	for (const auto &message : conversation.messages) {
		if (message.type == QLatin1String(kType)) {
			const auto data = Data::FromJson(message.data);
			if (!data.file.isEmpty()) {
				result.push_back(data.file);
			}
		}
	}
	return result;
}

void Forget(SessionState &state, const QString &file) {
	state.memory.remove(file);
	state.cache.erase(
		ranges::remove(state.cache, file, &std::pair<QString, QImage>::first),
		end(state.cache));
}

// Store::changes() with an id: that conversation was saved or removed.
void Cleanup(
		not_null<Main::Session*> session,
		SessionState &state,
		const QString &conversationId) {
	LoadIndex(session, state);
	if (!state.indexLoaded) {
		return;
	}
	const auto conversation = Store::For(session).find(conversationId);
	auto referenced = std::optional<QStringList>();
	if (conversation) {
		referenced = Referenced(*conversation);
	}
	const auto orphans = Orphans(state.written, conversationId, referenced);
	if (orphans.isEmpty()) {
		return;
	}
	for (const auto &file : orphans) {
		Storage::RemoveAccount(session, file);
		Forget(state, file);
	}
	state.written.erase(
		ranges::remove_if(state.written, [&](const Written &entry) {
			return orphans.contains(entry.file);
		}),
		end(state.written));
	WriteIndex(session, state);
}

[[nodiscard]] SessionState &StateFor(not_null<Main::Session*> session) {
	auto &states = States();
	if (const auto i = states.find(session); i != end(states)) {
		return *i->second;
	}
	const auto raw = states.emplace(
		session,
		std::make_unique<SessionState>()).first->second.get();
	session->lifetime().add([=] {
		States().remove(session);
	});
	LoadIndex(session, *raw);
	Store::For(session).changes(
	) | rpl::filter([](const QString &id) {
		return !id.isEmpty();
	}) | rpl::on_next([=](const QString &id) {
		Cleanup(session, *raw, id);
	}, raw->lifetime);
	// Switching spaces (D8) can show or hide this account's storage.
	DoubleBottom::ActiveChanges(
	) | rpl::on_next([=] {
		raw->indexLoaded = false;
		raw->written.clear();
		raw->cache.clear();
		LoadIndex(session, *raw);
	}, raw->lifetime);
	return *raw;
}

[[nodiscard]] QByteArray Picture(
		not_null<Main::Session*> session,
		const QString &file) {
	auto &state = StateFor(session);
	if (const auto i = state.memory.find(file); i != end(state.memory)) {
		return i->second;
	}
	return Storage::ReadAccount(session, file);
}

[[nodiscard]] QImage CachedImage(
		not_null<Main::Session*> session,
		const QString &file) {
	auto &state = StateFor(session);
	const auto i = ranges::find(
		state.cache,
		file,
		&std::pair<QString, QImage>::first);
	if (i != end(state.cache)) {
		auto result = i->second;
		state.cache.erase(i);
		state.cache.emplace_back(file, result);
		return result;
	}
	auto image = QImage::fromData(Picture(session, file));
	if (image.isNull()) {
		return image;
	}
	state.cache.emplace_back(file, image);
	if (int(state.cache.size()) > kCachedImages) {
		state.cache.erase(begin(state.cache));
	}
	return image;
}

[[nodiscard]] QString ErrorText(const QString &code) {
	if (code == u"content_rejected"_q) {
		// 23 需求 5.
		return Tr(
			"这个描述无法生成图片，换个说法试试",
			"This description can't be drawn. Try wording it differently.");
	}
	return Api::ErrorText(code);
}

// The open page when there is one, else the stored conversation: the
// picture arrives after tens of seconds (ai_registry.h FindOpenHost).
void UpdateMessage(
		not_null<Main::Session*> session,
		const QString &conversationId,
		const Message &message) {
	if (const auto host = FindOpenHost(conversationId)) {
		host->updateMessage(message);
		return;
	}
	auto &store = Store::For(session);
	auto conversation = store.find(conversationId);
	if (!conversation) {
		return; // Removed meanwhile.
	}
	for (auto &existing : conversation->messages) {
		if (existing.id == message.id) {
			existing = message;
			store.save(*conversation);
			return;
		}
	}
}

void Start(
		not_null<Window::SessionController*> controller,
		const QString &conversationId,
		Message message,
		const QString &prompt) {
	const auto session = &controller->session();
	const auto weak = base::make_weak(controller);
	const auto finished = std::make_shared<bool>(false);
	const auto id = message.id;
	const auto finish = [=](Data data) {
		*finished = true;
		auto &state = StateFor(session);
		state.requests.remove(id);
		auto updated = message;
		updated.data = data.toJson();
		UpdateMessage(session, conversationId, updated);
	};
	auto request = GenerateImage(prompt, crl::guard(session, [=](
			AI::Image image) {
		const auto file = StorageName(id);
		if (file.isEmpty() || QImage::fromData(image.data).isNull()) {
			finish({
				.state = State::Error,
				.error = QString::fromLatin1(Api::kUpstreamUnavailable),
			});
			return;
		}
		auto &state = StateFor(session);
		if (Storage::WriteAccount(session, file, image.data)) {
			LoadIndex(session, state);
			state.written.push_back({ conversationId, file });
			WriteIndex(session, state);
		} else {
			// Storage unavailable (D8 hidden account): memory only.
			state.memory[file] = image.data;
		}
		finish({
			.state = State::Done,
			.file = file,
			.mime = image.mime,
		});
	}), crl::guard(session, [=](Api::Error error) {
		if (const auto strong = weak.get()) {
			// quota_exceeded / not_pro: 29's upgrade box.
			Pro::HandleError(strong, error);
		}
		finish({ .state = State::Error, .error = error.code });
	}));
	if (!*finished) {
		StateFor(session).requests[id] = std::move(request);
	}
}

// Adds an image message to the open page and draws prompt into it.
void Generate(not_null<Host*> host, const QString &prompt) {
	const auto controller = host->sessionController();
	auto message = Message();
	message.id = NewId();
	message.role = QString::fromLatin1(Conversations::kRoleAssistant);
	message.type = QString::fromLatin1(kType);
	message.text = prompt;
	message.time = NowMs();
	message.data = Data().toJson(); // Pending.
	// Known as running before its bubble is built (inside addMessage).
	StateFor(&controller->session()).requests[message.id] = nullptr;
	host->addMessage(message);
	Start(controller, host->conversation().id, std::move(message), prompt);
}

void SendTo(
		not_null<Window::SessionController*> controller,
		QByteArray bytes) {
	auto chosen = [=](not_null<::Data::Thread*> thread) {
		if (!::Data::CanSend(thread, ChatRestriction::SendPhotos)) {
			controller->showToast(Tr(
				"无法在这个聊天里发送图片",
				"You can't send images in this chat"));
			return false;
		}
		auto image = QImage::fromData(bytes);
		if (image.isNull()) {
			return false;
		}
		auto list = ::Storage::PrepareMediaFromImage(
			std::move(image),
			QByteArray(),
			st::sendMediaPreviewSize);
		thread->session().api().sendFiles(
			std::move(list),
			SendMediaType::Photo,
			nullptr,
			::Api::SendAction(thread));
		controller->showToast(Tr("图片已发送", "Image sent"));
		return true;
	};
	Window::ShowChooseRecipientBox(controller, std::move(chosen));
}

void Save(
		not_null<Window::SessionController*> controller,
		QByteArray bytes,
		const QString &mime) {
	const auto extension = Extension(mime);
	const auto weak = base::make_weak(controller);
	FileDialog::GetWritePath(
		controller->widget().get(),
		tr::lng_save_photo(tr::now),
		u"Image (*."_q + extension + u")"_q,
		filedialogDefaultName(u"luckgram_ai"_q, u"."_q + extension),
		[=](QString &&path) {
			if (path.isEmpty()) {
				return;
			}
			auto f = QFile(path);
			const auto ok = f.open(QIODevice::WriteOnly)
				&& (f.write(bytes) == bytes.size());
			if (const auto strong = weak.get()) {
				strong->showToast(ok
					? Tr("已保存", "Saved")
					: Tr("保存失败", "Couldn't save"));
			}
		});
}

// The image bubble: placeholder while drawing, then the picture with
// 「发送给…」 / 「保存」 / 「重画」; an error shows its text and 「重画」.
class Bubble final : public Ui::RpWidget {
public:
	Bubble(
		QWidget *parent,
		not_null<Host*> host,
		const Message &message);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void addLink(const QString &text, Fn<void()> callback);
	[[nodiscard]] bool hasPictureArea() const;
	[[nodiscard]] QByteArray bytes() const;

	const not_null<Host*> _host;
	const not_null<Main::Session*> _session;
	const Message _message;
	Data _data;
	QImage _image;
	Ui::InfiniteRadialAnimation _radial;
	object_ptr<Ui::FlatLabel> _status;
	std::vector<object_ptr<Ui::LinkButton>> _links;
	QRect _picture;

};

Bubble::Bubble(
	QWidget *parent,
	not_null<Host*> host,
	const Message &message)
: RpWidget(parent)
, _host(host)
, _session(&host->sessionController()->session())
, _message(message)
, _data(Data::FromJson(message.data))
, _radial([=] { update(_picture); }, st::windowShellLoading)
, _status(this, QString(), st::defaultFlatLabel) {
	const auto prompt = _message.text.value_or(QString());
	if (_data.state == State::Pending
		&& !StateFor(_session).requests.contains(_message.id)) {
		// Its request is gone (the app was closed while drawing).
		_data = Data{
			.state = State::Error,
			.error = QString::fromLatin1(Api::kUpstreamUnavailable),
		};
	}
	if (_data.state == State::Done) {
		_image = CachedImage(_session, _data.file);
		if (_image.isNull()) {
			// Picture gone (or hidden with the account): offer 重画.
			_data = Data{
				.state = State::Error,
				.error = QString::fromLatin1(Api::kUpstreamUnavailable),
			};
		}
	}
	switch (_data.state) {
	case State::Pending:
		_status->setText(Tr("正在画…", "Drawing…"));
		_status->setTextColorOverride(st::windowSubTextFg->c);
		_radial.start();
		break;
	case State::Done:
		_status->setText(prompt);
		break;
	case State::Error:
		_status->setText(ErrorText(_data.error));
		_status->setTextColorOverride(st::attentionButtonFg->c);
		break;
	}
	_status->setSelectable(_data.state == State::Done);
	if (_data.state == State::Done) {
		addLink(Tr("发送给…", "Send to…"), [=] {
			SendTo(_host->sessionController(), bytes());
		});
		addLink(Tr("保存", "Save"), [=] {
			Save(_host->sessionController(), bytes(), _data.mime);
		});
	}
	if (_data.state != State::Pending && !prompt.isEmpty()) {
		addLink(Tr("重画", "Redraw"), [=] {
			if (Enabled()) {
				Generate(_host, prompt);
			}
		});
	}
}

void Bubble::addLink(const QString &text, Fn<void()> callback) {
	auto link = object_ptr<Ui::LinkButton>(this, text);
	link->setClickedCallback(std::move(callback));
	link->show();
	_links.push_back(std::move(link));
}

bool Bubble::hasPictureArea() const {
	return (_data.state != State::Error);
}

QByteArray Bubble::bytes() const {
	return Picture(_session, _data.file);
}

int Bubble::resizeGetHeight(int newWidth) {
	const auto side = std::min(newWidth, Scaled(kSide));
	auto top = 0;
	_picture = hasPictureArea() ? QRect(0, 0, side, side) : QRect();
	if (hasPictureArea()) {
		top += side + Scaled(6);
	}
	_status->resizeToWidth(std::max(newWidth, 1));
	_status->moveToLeft(0, top, newWidth);
	top += _status->height();
	if (!_links.empty()) {
		top += Scaled(4);
		auto x = 0;
		auto height = 0;
		for (const auto &link : _links) {
			link->moveToLeft(x, top, newWidth);
			x += link->width() + Scaled(16);
			height = std::max(height, link->height());
		}
		top += height;
	}
	return top;
}

void Bubble::paintEvent(QPaintEvent *e) {
	if (_picture.isEmpty()) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = Scaled(12);
	auto path = QPainterPath();
	path.addRoundedRect(_picture, radius, radius);
	if (_data.state == State::Done && !_image.isNull()) {
		p.setClipPath(path);
		// Cover the square, cropping the longer side (as mobile).
		const auto ratio = style::DevicePixelRatio();
		const auto scaled = _image.scaled(
			_picture.size() * ratio,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
		const auto source = QRect(
			(scaled.width() - _picture.width() * ratio) / 2,
			(scaled.height() - _picture.height() * ratio) / 2,
			_picture.width() * ratio,
			_picture.height() * ratio);
		p.drawImage(_picture, scaled, source);
		return;
	}
	p.fillPath(path, st::windowBgOver);
	const auto size = st::windowShellLoading.size;
	_radial.draw(
		p,
		QPoint(
			_picture.x() + (_picture.width() - size.width()) / 2,
			_picture.y() + (_picture.height() - size.height()) / 2),
		width());
}

} // namespace

void Register() {
	// 23 需求 1: the 「画图」 quick button and the 「画」 / "/image" prefixes.
	RegisterQuickButton({
		.id = u"image"_q,
		.titleZh = "画图",
		.titleEn = "Draw",
		.handler = [](not_null<Host*> host) {
			host->setInputText(QuickPrefix(IsChinese()));
		},
		.visible = Enabled,
		.order = 10,
	});
	RegisterInputInterceptor({
		.id = u"image"_q,
		.matches = [](const QString &text) {
			return Intercept(text).has_value();
		},
		.handle = [](not_null<Host*> host, const QString &text) {
			const auto intercepted = Intercept(text);
			if (!intercepted) {
				return;
			}
			const auto prompt = intercepted->prompt;
			const auto conversationId = host->conversation().id;
			const auto controller = host->sessionController();
			// 00: the consent prompt before the description is sent.
			WithConsent(controller->uiShow(), [=] {
				const auto open = FindOpenHost(conversationId);
				if (!open) {
					return;
				}
				auto user = Message();
				user.id = NewId();
				user.role = QString::fromLatin1(Conversations::kRoleUser);
				user.text = text.trimmed();
				user.time = NowMs();
				open->addMessage(std::move(user));
				Generate(open, prompt);
			});
		},
		.enabled = Enabled,
	});
	// 23 需求 2-3: the image bubble. Images are not sent to the chat model.
	RegisterMessageType({
		.type = QString::fromLatin1(kType),
		.create = [](
				not_null<QWidget*> parent,
				not_null<Host*> host,
				const Message &message) -> object_ptr<Ui::RpWidget> {
			return object_ptr<Bubble>(parent.get(), host, message);
		},
		.contextText = [](const Message &) {
			return QString();
		},
	});
}

} // namespace Luckgram::AI::Draw
