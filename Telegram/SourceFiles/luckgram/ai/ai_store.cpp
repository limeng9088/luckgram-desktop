/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/ai/ai_store.h"

#include "main/main_session.h"
#include "settings.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QSaveFile>
#include <QtCore/QUuid>

namespace Luckgram::AI {
namespace {

constexpr auto kFolder = "tdata/luckgram_ai/";

[[nodiscard]] std::map<uint64, std::unique_ptr<Store>> &Stores() {
	static auto result = std::map<uint64, std::unique_ptr<Store>>();
	return result;
}

[[nodiscard]] QString Folder() {
	return cWorkingDir() + QString::fromLatin1(kFolder);
}

} // namespace

qint64 NowMs() {
	return QDateTime::currentMSecsSinceEpoch();
}

QString NewId() {
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

Store &Store::For(not_null<Main::Session*> session) {
	const auto userId = session->userId().bare;
	auto &stores = Stores();
	auto i = stores.find(userId);
	if (i == end(stores)) {
		i = stores.emplace(userId, std::make_unique<Store>(userId)).first;
	}
	return *i->second;
}

void Store::ClearAll() {
	for (const auto &[userId, store] : Stores()) {
		store->_list.clear();
		store->_changes.fire(QString());
	}
	QDir(Folder()).removeRecursively();
}

Store::Store(uint64 userId) : _userId(userId) {
	load();
}

QString Store::path() const {
	return Folder() + QString::number(_userId) + QStringLiteral(".json");
}

void Store::load() {
	auto file = QFile(path());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	const auto list = object.value(QStringLiteral("conversations")).toArray();
	for (const auto &value : list) {
		if (value.isObject()) {
			_list.push_back(Conversation::FromJson(value.toObject()));
		}
	}
	Conversations::SortForHistory(_list);
}

void Store::write() const {
	auto list = QJsonArray();
	for (const auto &conversation : _list) {
		list.push_back(conversation.toJson());
	}
	auto object = QJsonObject();
	object.insert(QStringLiteral("conversations"), list);
	QDir().mkpath(Folder());
	auto file = QSaveFile(path());
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("Luckgram Error: could not write %1").arg(path()));
		return;
	}
	file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
	file.commit();
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
	if (_list.size() != was) {
		write();
		_changes.fire_copy(id);
	}
}

rpl::producer<QString> Store::changes() const {
	return _changes.events();
}

} // namespace Luckgram::AI
