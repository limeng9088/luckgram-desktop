/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_store.h"

#include "luckgram/luckgram_double_bottom.h"
#include "luckgram/luckgram_storage.h"
#include "main/main_session.h"
#include "settings.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QUuid>

namespace Luckgram::AI {
namespace {

// Account storage "luckgram_ai_conversations" (D2 luckgram_storage.h).
const auto kStorageName = QStringLiteral("ai_conversations");

[[nodiscard]] auto &Stores() {
	static auto result = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<Store>>();
	return result;
}

// Before the D2 encrypted storage, development builds kept conversations
// in plain tdata/luckgram_ai/<user id>.json. Removed once, not migrated.
void RemoveLegacyPlainFolder() {
	static auto done = false;
	if (!std::exchange(done, true)) {
		QDir(cWorkingDir() + u"tdata/luckgram_ai"_q).removeRecursively();
	}
}

} // namespace

qint64 NowMs() {
	return QDateTime::currentMSecsSinceEpoch();
}

QString NewId() {
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

Store &Store::For(not_null<Main::Session*> session) {
	RemoveLegacyPlainFolder();
	auto &stores = Stores();
	auto i = stores.find(session);
	if (i == end(stores)) {
		i = stores.emplace(session, std::make_unique<Store>(session)).first;
		session->lifetime().add([=] {
			Stores().remove(session);
		});
	}
	return *i->second;
}

Store::Store(not_null<Main::Session*> session) : _session(session) {
	load();
	// D8 second space: the storage of a hidden account comes and goes.
	DoubleBottom::ActiveChanges(
	) | rpl::on_next([=] {
		load();
	}, _lifetime);
}

void Store::load() {
	if (_loaded || !Storage::AccountAvailable(_session)) {
		return;
	}
	_loaded = true;
	const auto bytes = Storage::ReadAccount(_session, kStorageName);
	const auto object = QJsonDocument::fromJson(bytes).object();
	const auto list = object.value(QStringLiteral("conversations")).toArray();
	auto stored = std::vector<Conversation>();
	for (const auto &value : list) {
		if (value.isObject()) {
			stored.push_back(Conversation::FromJson(value.toObject()));
		}
	}
	for (auto &conversation : stored) {
		if (!_removedUnsaved.contains(conversation.id)
			&& !ranges::contains(_list, conversation.id, &Conversation::id)) {
			_list.push_back(std::move(conversation));
		}
	}
	Conversations::SortForHistory(_list);
	_removedUnsaved.clear();
	if (base::take(_unsaved)) {
		write();
	}
	_changes.fire(QString());
}

void Store::write() {
	if (!_loaded) {
		// Not read yet: writing now would drop what is stored.
		_unsaved = true;
		return;
	}
	auto list = QJsonArray();
	for (const auto &conversation : _list) {
		list.push_back(conversation.toJson());
	}
	auto object = QJsonObject();
	object.insert(QStringLiteral("conversations"), list);
	const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
	if (!Storage::WriteAccount(_session, kStorageName, bytes)) {
		// Storage went away (account hidden): memory only until load().
		_unsaved = true;
		_loaded = false;
	}
}

std::vector<Store::Conversation> Store::list() const {
	auto result = _list;
	Conversations::SortForHistory(result);
	return result;
}

std::optional<Store::Conversation> Store::find(const QString &id) const {
	for (const auto &conversation : _list) {
		if (conversation.id == id) {
			return conversation;
		}
	}
	return std::nullopt;
}

std::optional<Store::Conversation> Store::findByKey(const QString &key) const {
	for (const auto &conversation : _list) {
		if (conversation.key && *conversation.key == key) {
			return conversation;
		}
	}
	return std::nullopt;
}

Store::Conversation Store::create(
		const QString &task,
		const std::optional<QString> &system,
		const std::optional<QString> &title,
		const std::optional<QString> &key,
		const QJsonObject &params) {
	auto result = Conversation();
	result.id = NewId();
	if (!task.isEmpty()) {
		result.task = task;
	}
	result.system = system;
	result.title = title;
	result.key = key;
	result.params = params;
	result.createdAt = result.updatedAt = NowMs();
	save(result);
	return result;
}

Store::Conversation Store::findOrCreate(
		const QString &key,
		const QString &task,
		const std::optional<QString> &system,
		const QString &title,
		const QJsonObject &params) {
	if (auto existing = findByKey(key)) {
		auto changed = false;
		if (existing->task != task) {
			existing->task = task;
			changed = true;
		}
		if (existing->system != system) {
			existing->system = system;
			changed = true;
		}
		if (existing->params != params) {
			existing->params = params;
			changed = true;
		}
		if (!existing->title || *existing->title != title) {
			existing->title = title;
			changed = true;
		}
		if (changed) {
			save(*existing);
		}
		return *existing;
	}
	return create(task, system, title, key, params);
}

void Store::save(const Conversation &conversation) {
	auto found = false;
	for (auto &existing : _list) {
		if (existing.id == conversation.id) {
			existing = conversation;
			found = true;
			break;
		}
	}
	if (!found) {
		_list.push_back(conversation);
	}
	write();
	_changes.fire_copy(conversation.id);
}

void Store::remove(const QString &id) {
	const auto was = _list.size();
	_list.erase(ranges::remove(_list, id, &Conversation::id), end(_list));
	if (_list.size() != was || !_loaded) {
		if (!_loaded) {
			_removedUnsaved.emplace(id);
		}
		write();
		_changes.fire_copy(id);
	}
}

rpl::producer<QString> Store::changes() const {
	return _changes.events();
}

} // namespace Luckgram::AI
