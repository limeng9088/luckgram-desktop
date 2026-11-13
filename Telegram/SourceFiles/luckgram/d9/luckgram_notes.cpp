/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/d9/luckgram_notes.h"

#include "base/unixtime.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "lang/lang_keys.h"
#include "luckgram/d9/luckgram_account_store.h"
#include "luckgram/d9/luckgram_notes_text.h"
#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Luckgram::Notes {
namespace {

const auto kFileName = u"notes"_q;

struct State {
	base::flat_map<PeerId, Entry> notes;
	base::flat_set<PeerId> expanded;
	rpl::event_stream<PeerId> changes;
};

[[nodiscard]] base::flat_map<
	not_null<Main::Session*>,
	std::unique_ptr<State>> &States() {
	static auto result = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<State>>();
	return result;
}

[[nodiscard]] QString PeerKey(PeerId peer) {
	return QString::number(SerializePeerId(peer));
}

[[nodiscard]] PeerId ParsePeerKey(const QString &key) {
	auto ok = false;
	const auto value = key.toULongLong(&ok);
	return ok ? DeserializePeerId(value) : PeerId();
}

void Load(not_null<Main::Session*> session, State &state) {
	const auto document = QJsonDocument::fromJson(
		AccountStore::Read(session, kFileName));
	const auto root = document.object();
	for (const auto &value : root.value(u"notes"_q).toArray()) {
		const auto object = value.toObject();
		const auto peer = ParsePeerKey(object.value(u"peer"_q).toString());
		const auto note = NotesText::Normalize(
			object.value(u"note"_q).toString());
		if (peer && !note.isEmpty()) {
			state.notes.emplace(peer, Entry{
				.peer = peer,
				.note = note,
				.updated = TimeId(object.value(u"t"_q).toInteger()),
			});
		}
	}
	for (const auto &value : root.value(u"expanded"_q).toArray()) {
		if (const auto peer = ParsePeerKey(value.toString())) {
			state.expanded.emplace(peer);
		}
	}
}

void Save(not_null<Main::Session*> session, const State &state) {
	auto notes = QJsonArray();
	for (const auto &[peer, entry] : state.notes) {
		notes.append(QJsonObject{
			{ u"peer"_q, PeerKey(peer) },
			{ u"note"_q, entry.note },
			{ u"t"_q, qint64(entry.updated) },
		});
	}
	auto expanded = QJsonArray();
	for (const auto peer : state.expanded) {
		expanded.append(PeerKey(peer));
	}
	const auto root = QJsonObject{
		{ u"v"_q, 1 },
		{ u"notes"_q, notes },
		{ u"expanded"_q, expanded },
	};
	AccountStore::Write(
		session,
		kFileName,
		QJsonDocument(root).toJson(QJsonDocument::Compact));
}

[[nodiscard]] State &Lookup(not_null<Main::Session*> session) {
	auto &states = States();
	if (const auto i = states.find(session); i != end(states)) {
		return *i->second;
	}
	auto state = std::make_unique<State>();
	Load(session, *state);
	const auto raw = state.get();
	states.emplace(session, std::move(state));
	session->lifetime().add([=] {
		States().remove(session);
	});
	return *raw;
}

} // namespace

bool Enabled() {
	return Settings::IsEnabled(QString::fromLatin1(kFeatureKey));
}

rpl::producer<bool> EnabledValue() {
	return Settings::EnabledValue(QString::fromLatin1(kFeatureKey));
}

QString Get(not_null<PeerData*> peer) {
	const auto &notes = Lookup(&peer->session()).notes;
	const auto i = notes.find(peer->id);
	return (i != end(notes)) ? i->second.note : QString();
}

void Set(not_null<PeerData*> peer, const QString &note) {
	const auto session = &peer->session();
	auto &state = Lookup(session);
	const auto value = NotesText::Normalize(note);
	if (value.isEmpty()) {
		if (!state.notes.remove(peer->id)) {
			return;
		}
		state.expanded.remove(peer->id);
	} else {
		const auto i = state.notes.find(peer->id);
		if (i != end(state.notes) && i->second.note == value) {
			return;
		}
		state.notes[peer->id] = Entry{
			.peer = peer->id,
			.note = value,
			.updated = base::unixtime::now(),
		};
	}
	Save(session, state);
	state.changes.fire_copy(peer->id);
}

rpl::producer<QString> Value(not_null<PeerData*> peer) {
	const auto id = peer->id;
	auto note = rpl::single(
		id
	) | rpl::then(
		Changes(&peer->session()) | rpl::filter(rpl::mappers::_1 == id)
	) | rpl::map([=] {
		return Get(peer);
	});
	return rpl::combine(
		EnabledValue(),
		std::move(note)
	) | rpl::map([](bool enabled, const QString &note) {
		return enabled ? note : QString();
	}) | rpl::distinct_until_changed();
}

bool BarExpanded(not_null<PeerData*> peer) {
	return Lookup(&peer->session()).expanded.contains(peer->id);
}

void SetBarExpanded(not_null<PeerData*> peer, bool expanded) {
	const auto session = &peer->session();
	auto &state = Lookup(session);
	const auto changed = expanded
		? state.expanded.emplace(peer->id).second
		: state.expanded.remove(peer->id);
	if (changed) {
		Save(session, state);
	}
}

std::vector<Entry> All(not_null<Main::Session*> session) {
	auto result = std::vector<Entry>();
	for (const auto &[peer, entry] : Lookup(session).notes) {
		result.push_back(entry);
	}
	ranges::stable_sort(result, std::greater<>(), &Entry::updated);
	return result;
}

rpl::producer<PeerId> Changes(not_null<Main::Session*> session) {
	return Lookup(session).changes.events();
}

QString RowTitle() {
	return Tr("私人备注", "Private note");
}

QString OnlyYou() {
	return Tr("只有你能看到", "only visible to you");
}

QString AddNote() {
	return Tr("添加备注…", "Add a note…");
}

QString BarTitle() {
	return Tr("私人备注 · 只有你能看到", "Private note · only visible to you");
}

QString AllNotes() {
	return Tr("全部备注", "All notes");
}

void EditBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	const auto initial = Get(peer);
	box->setTitle(rpl::single(Tr("私人备注", "Private note")));
	box->setWidth(st::boxWideWidth);
	box->setCloseByOutsideClick(false);

	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine,
		rpl::single(Tr(
			"写点只有你能看到的内容",
			"Write something only you can see")),
		initial));
	field->setSubmitSettings(Ui::InputField::SubmitSettings::CtrlEnter);
	box->setFocusCallback([=] {
		field->setFocusFast();
		field->setCursorPosition(field->getLastText().size());
	});

	const auto counter = box->addRow(
		object_ptr<Ui::FlatLabel>(box, QString(), st::boxDividerLabel),
		st::boxRowPadding);
	const auto updateCounter = [=] {
		const auto left = NotesText::Remaining(field->getLastText().trimmed());
		counter->setText((left < 100)
			? Tr("还能输入 %1 字", "%1 characters left").arg(left)
			: QString());
	};
	field->changes() | rpl::on_next(updateCounter, field->lifetime());
	updateCounter();

	Ui::AddDividerText(
		box->verticalLayout(),
		rpl::single(Tr(
			"只有你能看到这条备注。它只保存在这台设备的当前账号中，不会上传。",
			"Only you can see this note. It is stored on this device for the current account and is never uploaded.")));

	const auto save = [=] {
		const auto text = field->getLastText().trimmed();
		if (NotesText::GraphemeCount(text) > NotesText::kMaxLength) {
			controller->showToast(Tr(
				"备注最多 2000 字",
				"A note can be up to 2000 characters"));
			return;
		}
		Set(peer, text);
		box->closeBox();
	};
	field->submits() | rpl::on_next(save, field->lifetime());
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] {
		if (field->getLastText().trimmed() == initial.trimmed()) {
			box->closeBox();
			return;
		}
		controller->show(Ui::MakeConfirmBox({
			.text = Tr("放弃修改？", "Discard changes?"),
			.confirmed = [=](Fn<void()> close) {
				close();
				box->closeBox();
			},
			.confirmText = Tr("放弃", "Discard"),
			.cancelText = Tr("取消", "Cancel"),
			.confirmStyle = &st::attentionBoxButton,
		}));
	});
	if (!initial.isEmpty()) {
		box->addLeftButton(rpl::single(Tr("删除备注", "Delete note")), [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = Tr("确定删除这条备注吗？", "Delete this note?"),
				.confirmed = [=](Fn<void()> close) {
					Set(peer, QString());
					close();
					box->closeBox();
				},
				.confirmText = Tr("删除", "Delete"),
				.cancelText = Tr("取消", "Cancel"),
				.confirmStyle = &st::attentionBoxButton,
			}));
		}, st::attentionBoxButton);
	}
}

} // namespace Luckgram::Notes
