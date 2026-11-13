/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/backup/luckgram_backup.h"

#include "apiwrap.h"
#include "base/call_delayed.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "data/data_session.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/pro/luckgram_pro_logic.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace Luckgram::Backup {
namespace {

// The restore of one account answers within this time or counts as failed.
constexpr auto kRestoreTimeout = 30 * crl::time(1000);

// Wrong-password back-off, shared by every restore in this process.
int FailedAttempts = 0;
crl::time NextAttemptAt = 0;

[[nodiscard]] QString Ext() {
	return QString::fromLatin1(kFileExtension);
}

[[nodiscard]] QString FileFilter() {
	return Tr("Luckgram 备份", "Luckgram backup")
		+ u" (*."_q + Ext() + u");;"_q
		+ FileDialog::AllFilesFilter();
}

[[nodiscard]] QString PasswordTitleText() {
	return Tr("设置备份密码", "Set a backup password");
}

[[nodiscard]] QString PasswordHintText() {
	return Tr("密码（至少 8 位）", "Password (at least 8 characters)");
}

[[nodiscard]] QString PasswordRepeatHintText() {
	return Tr("再次输入密码", "Repeat password");
}

[[nodiscard]] QString PasswordTooShortText() {
	return Tr("密码至少 8 位", "The password must have at least 8 characters");
}

[[nodiscard]] QString PasswordMismatchText() {
	return Tr("两次输入的密码不一致", "The passwords don't match");
}

[[nodiscard]] QString EnterPasswordTitleText() {
	return Tr("输入备份密码", "Enter the backup password");
}

[[nodiscard]] QString WrongPasswordText() {
	return Tr("密码不正确", "Incorrect password");
}

[[nodiscard]] QString WaitText(int seconds) {
	return Luckgram::IsChinese()
		? (u"请 "_q + QString::number(seconds) + u" 秒后再试"_q)
		: (u"Try again in "_q + QString::number(seconds) + u" s"_q);
}

[[nodiscard]] QString NotBackupText() {
	return Tr(
		"这不是 Luckgram 备份文件，或文件已损坏",
		"This isn't a Luckgram backup file, or the file is damaged");
}

[[nodiscard]] QString ChooseAccountsText() {
	return Tr("选择要恢复的账号", "Accounts to restore");
}

[[nodiscard]] QString AlreadyLoggedInText() {
	return Tr("已登录", "Already logged in");
}

[[nodiscard]] QString RestoreButtonText() {
	return Tr("恢复", "Restore");
}

[[nodiscard]] QString SelectAtLeastOneText() {
	return Tr("请至少选择一个账号", "Select at least one account");
}

[[nodiscard]] QString NoRoomText() {
	return Tr(
		"已达到账号上限，无法恢复更多账号",
		"The account limit is reached; no more accounts can be restored");
}

[[nodiscard]] QString SavedText() {
	return Tr("备份文件已保存", "Backup file saved");
}

[[nodiscard]] QString ExportFailedText() {
	return Tr("导出失败", "Export failed");
}

[[nodiscard]] QString WorkingText() {
	return Tr("正在处理…", "Working…");
}

[[nodiscard]] QString ResultText(int restored, int skipped, int failed) {
	const auto n = [](int value) { return QString::number(value); };
	auto result = Luckgram::IsChinese()
		? (u"已恢复 "_q + n(restored) + u" 个账号"_q)
		: (n(restored) + u" account(s) restored"_q);
	if (skipped > 0) {
		result += Luckgram::IsChinese()
			? (u"，"_q + n(skipped) + u" 个已登录的账号已跳过"_q)
			: (u", "_q + n(skipped) + u" already logged in skipped"_q);
	}
	if (failed > 0) {
		result += Luckgram::IsChinese()
			? (u"，"_q + n(failed) + u" 个账号恢复失败（会话可能已在其他设备上被终止）"_q)
			: (u", "_q + n(failed) + u" failed (the session may have been terminated on another device)"_q);
	}
	return result;
}

[[nodiscard]] QString AccountLabel(uint64 userId, const QString &name) {
	const auto id = QString::number(userId);
	return name.isEmpty()
		? (Luckgram::IsChinese() ? (u"账号 "_q + id) : (u"Account "_q + id))
		: (name + u" · "_q + id);
}

[[nodiscard]] Main::Domain &Domain() {
	return Core::App().domain();
}

[[nodiscard]] bool IsLoggedIn(uint64 userId) {
	for (const auto &[index, account] : Domain().accounts()) {
		if (account->sessionExists()
			&& account->session().userId().bare == userId) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] int LoggedInCount() {
	auto result = 0;
	for (const auto &[index, account] : Domain().accounts()) {
		if (account->sessionExists()) {
			++result;
		}
	}
	return result;
}

[[nodiscard]] not_null<Ui::PasswordInput*> AddPasswordField(
		not_null<Ui::GenericBox*> box,
		const QString &placeholder) {
	return box->addRow(object_ptr<Ui::PasswordInput>(
		box,
		st::defaultInputField,
		rpl::single(placeholder)));
}

// --- Export.

void SaveFile(std::shared_ptr<Ui::Show> show, QByteArray file) {
	const auto now = QDateTime::currentDateTime().toString(
		u"yyyyMMdd-HHmm"_q);
	const auto folder = QFileInfo(
		filedialogDefaultName(u"luckgram"_q, u"."_q + Ext())
	).absolutePath();
	const auto initial = QDir(folder).filePath(
		u"luckgram-"_q + now + u"."_q + Ext());
	FileDialog::GetWritePath(
		show->toastParent().get(),
		ExportText(),
		FileFilter(),
		initial,
		[=](QString &&path) {
			if (path.isEmpty()) {
				return;
			}
			if (!path.endsWith(u"."_q + Ext(), Qt::CaseInsensitive)) {
				path += u"."_q + Ext();
			}
			auto out = QFile(path);
			const auto ok = out.open(QIODevice::WriteOnly)
				&& (out.write(file) == file.size());
			out.close();
			show->showToast(ok ? SavedText() : ExportFailedText());
		});
}

void EncryptAndSave(
		std::shared_ptr<Ui::Show> show,
		std::vector<not_null<Main::Account*>> accounts,
		QString password) {
	const auto items = std::make_shared<std::vector<Account>>(
		Collect(accounts));
	if (items->empty()) {
		show->showToast(ExportFailedText());
		return;
	}
	show->showToast(WorkingText());
	crl::async([=]() mutable {
		auto file = Backup::Encrypt(*items, password);
		Wipe(*items);
		password.fill(QChar(0));
		crl::on_main([=] {
			if (file.isEmpty()) {
				show->showToast(ExportFailedText());
			} else {
				SaveFile(show, file);
			}
		});
	});
}

// --- Restore.

struct RestoreState {
	std::vector<Account> accounts;
	bool restoring = false;
};

struct RestoreProgress {
	int restored = 0;
	int skipped = 0;
	int failed = 0;
	int noRoom = 0;
	int pending = 0;
	bool finished = false;
	Main::Account *first = nullptr;
	std::shared_ptr<Ui::Show> show;
};

void FinishRestore(const std::shared_ptr<RestoreProgress> &progress) {
	if (progress->finished || progress->pending > 0) {
		return;
	}
	progress->finished = true;
	progress->show->showToast((progress->noRoom > 0 && !progress->restored)
		? NoRoomText()
		: ResultText(
			progress->restored,
			progress->skipped,
			progress->failed + progress->noRoom));
	if (const auto first = progress->first) {
		Domain().activate(first);
	}
}

// Adds one account with the key (Main::Domain::add with the serialized
// authorization) and checks it with users.getUsers([inputUserSelf]). A key
// the server doesn't know any more is logged out by Telegram Desktop
// itself (401); nothing is sent to end the original session.
void RestoreOne(
		Account account,
		const std::shared_ptr<RestoreProgress> &progress) {
	auto authorization = WriteMtpAuthorization(
		account.userId,
		account.dcId,
		account.authKey);
	const auto userId = account.userId;
	account.wipe();
	const auto added = Domain().add(
		MTP::Environment::Production,
		authorization);
	WipeBytes(authorization);
	if (!added->sessionExists()) {
		++progress->failed;
		return;
	}
	++progress->pending;
	const auto done = std::make_shared<bool>(false);
	const auto finish = [=](bool ok) {
		if (*done) {
			return;
		}
		*done = true;
		--progress->pending;
		if (ok) {
			++progress->restored;
			if (!progress->first) {
				progress->first = added;
			}
		} else {
			++progress->failed;
		}
		FinishRestore(progress);
	};
	const auto session = &added->session();
	session->api().request(MTPusers_GetUsers(
		MTP_vector<MTPInputUser>(1, MTP_inputUserSelf())
	)).done([=](const MTPVector<MTPUser> &result) {
		const auto user = session->data().processUsers(result);
		finish(user && user->id == peerFromUser(UserId(userId)));
	}).fail([=] {
		finish(false);
	}).send();
	base::call_delayed(kRestoreTimeout, session, [=] {
		finish(false);
	});
}

void Restore(
		std::shared_ptr<Ui::Show> show,
		std::vector<Account> accounts,
		int alreadyLoggedIn) {
	const auto progress = std::make_shared<RestoreProgress>();
	progress->show = show;
	progress->skipped = alreadyLoggedIn;
	auto logged = LoggedInCount();
	for (auto &account : accounts) {
		if (IsLoggedIn(account.userId)) {
			++progress->skipped;
			account.wipe();
		} else if (!Pro::CanAddAccount(logged, Domain().maxAccounts())
			|| Domain().accounts().size()
				>= size_t(Main::Domain::kPremiumMaxAccounts)) {
			++progress->noRoom;
			account.wipe();
		} else {
			++logged;
			RestoreOne(std::move(account), progress);
		}
	}
	Wipe(accounts);
	FinishRestore(progress);
}

void ChooseAccountsBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::Show> show,
		std::shared_ptr<RestoreState> state) {
	box->setTitle(rpl::single(RestoreText()));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(ChooseAccountsText()),
		st::boxLabel));
	auto checkboxes = std::vector<Ui::Checkbox*>();
	for (const auto &account : state->accounts) {
		const auto loggedIn = IsLoggedIn(account.userId);
		auto label = AccountLabel(account.userId, account.name);
		if (loggedIn) {
			label += u" · "_q + AlreadyLoggedInText();
		}
		const auto checkbox = box->addRow(object_ptr<Ui::Checkbox>(
			box,
			label,
			!loggedIn,
			st::defaultBoxCheckbox));
		checkbox->setDisabled(loggedIn);
		checkboxes.push_back(checkbox);
	}
	box->addButton(rpl::single(RestoreButtonText()), [=] {
		auto chosen = std::vector<Account>();
		auto alreadyLoggedIn = 0;
		for (auto i = 0; i != int(state->accounts.size()); ++i) {
			auto &account = state->accounts[i];
			if (IsLoggedIn(account.userId)) {
				++alreadyLoggedIn;
			} else if (checkboxes[i]->checked()) {
				chosen.push_back(account);
			}
		}
		if (chosen.empty()) {
			show->showToast(SelectAtLeastOneText());
			return;
		}
		state->restoring = true;
		Wipe(state->accounts); // The chosen ones were copied.
		box->closeBox();
		Restore(show, std::move(chosen), alreadyLoggedIn);
	});
	box->addButton(TrValue("取消", "Cancel"), [=] { box->closeBox(); });
	box->lifetime().add([=] {
		Wipe(state->accounts);
	});
}

void AskPasswordBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::Show> show,
		QByteArray file);

void DecryptAndChoose(
		std::shared_ptr<Ui::Show> show,
		QByteArray file,
		QString password) {
	show->showToast(WorkingText());
	crl::async([=]() mutable {
		auto result = std::make_shared<Decrypted>(
			Backup::Decrypt(file, password));
		password.fill(QChar(0));
		crl::on_main([=] {
			switch (result->error) {
			case Error::WrongPassword:
				++FailedAttempts;
				NextAttemptAt = crl::now()
					+ WaitSeconds(FailedAttempts) * crl::time(1000);
				show->showToast(WrongPasswordText());
				show->showBox(Box(AskPasswordBox, show, file));
				return;
			case Error::Format:
				show->showToast(NotBackupText());
				return;
			case Error::None:
				break;
			}
			FailedAttempts = 0;
			NextAttemptAt = 0;
			const auto state = std::make_shared<RestoreState>();
			state->accounts = std::move(result->accounts);
			show->showBox(Box(ChooseAccountsBox, show, state));
		});
	});
}

void AskPasswordBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::Show> show,
		QByteArray file) {
	box->setTitle(rpl::single(EnterPasswordTitleText()));
	const auto field = AddPasswordField(box, PasswordHintText());
	box->setFocusCallback([=] { field->setFocusFast(); });
	const auto submit = [=] {
		const auto wait = NextAttemptAt - crl::now();
		if (wait > 0) {
			field->showError();
			show->showToast(WaitText(int((wait + 999) / 1000)));
			return;
		}
		auto password = field->getLastText();
		if (password.isEmpty()) {
			field->showError();
			return;
		}
		field->clear();
		box->closeBox();
		DecryptAndChoose(show, file, std::move(password));
	};
	QObject::connect(field, &Ui::MaskedInputField::submitted, submit);
	box->addButton(TrValue("确定", "OK"), submit);
	box->addButton(TrValue("取消", "Cancel"), [=] { box->closeBox(); });
}

void NewPasswordBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::Show> show,
		std::vector<not_null<Main::Account*>> accounts) {
	box->setTitle(rpl::single(PasswordTitleText()));
	const auto warning = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(WarningText()),
		st::boxLabel));
	warning->setTextColorOverride(st::attentionButtonFg->c);
	const auto first = AddPasswordField(box, PasswordHintText());
	const auto second = AddPasswordField(box, PasswordRepeatHintText());
	box->setFocusCallback([=] { first->setFocusFast(); });
	const auto submit = [=] {
		auto password = first->getLastText();
		if (password.size() < kMinPasswordLength) {
			first->showError();
			show->showToast(PasswordTooShortText());
			return;
		} else if (password != second->getLastText()) {
			second->showError();
			show->showToast(PasswordMismatchText());
			return;
		}
		first->clear();
		second->clear();
		box->closeBox();
		EncryptAndSave(show, accounts, std::move(password));
	};
	QObject::connect(first, &Ui::MaskedInputField::submitted, [=] {
		second->setFocus();
	});
	QObject::connect(second, &Ui::MaskedInputField::submitted, submit);
	box->addButton(rpl::single(ExportText()), submit);
	box->addButton(TrValue("取消", "Cancel"), [=] { box->closeBox(); });
}

} // namespace

QString TitleText() {
	return Tr("账号备份", "Account backup");
}

QString AboutText() {
	return Tr(
		"把账号导出为加密文件，换手机后导入",
		"Export accounts to an encrypted file and import them on a new phone");
}

QString SelectHeaderText() {
	return Tr("选择要导出的账号", "Accounts to export");
}

QString ExportText() {
	return Tr("导出为文件", "Export to file");
}

QString RestoreText() {
	return Tr("从备份恢复", "Restore from backup");
}

QString ExportHintText() {
	return Luckgram::IsChinese()
		? (u"导出的文件以 ."_q + Ext() + u" 结尾，例如 luckgram-20261113-0930."_q + Ext() + u"。请存到你之后能找到的地方（如「下载」或「文件」）。"_q)
		: (u"The exported file ends in ."_q + Ext() + u", e.g. luckgram-20261113-0930."_q + Ext() + u". Keep it somewhere you can find it later (e.g. Downloads or Files)."_q);
}

QString RestoreHintText() {
	return Luckgram::IsChinese()
		? (u"恢复时选择以 ."_q + Ext() + u" 结尾的备份文件（一般在「下载」或「文件」里）。"_q)
		: (u"To restore, pick the backup file ending in ."_q + Ext() + u" (usually in Downloads or Files)."_q);
}

QString WarningText() {
	return Tr(
		"这个文件等于你账号的钥匙。任何人拿到文件和密码就能登录你的账号。不要发给别人，不要存到不信任的地方。",
		"This file is the key to your accounts. Anyone with the file and the password can log in to them. Don't send it to anyone and don't keep it anywhere you don't trust.");
}

QString InfoText() {
	return Tr(
		"备份文件里只有登录信息和备注名，不包含消息、联系人或媒体。文件用你设置的密码加密，iPhone 和 Android 通用。",
		"The file holds only the logins and the local names, no messages, contacts or media. It is encrypted with your password and works on both iPhone and Android.");
}

std::vector<Account> Collect(
		const std::vector<not_null<Main::Account*>> &accounts) {
	auto result = std::vector<Account>();
	for (const auto account : accounts) {
		if (!account->sessionExists()) {
			continue;
		}
		auto serialized = account->serializeMtpAuthorization();
		auto read = ReadMtpAuthorization(serialized);
		WipeBytes(serialized);
		const auto userId = account->session().userId().bare;
		if (read.mainDcKey.size() != size_t(kAuthKeySize)
			|| read.mainDcId <= 0
			|| read.userId != userId) {
			WipeKey(read.mainDcKey);
			continue;
		}
		result.push_back({
			.userId = userId,
			.dcId = read.mainDcId,
			.authKey = std::move(read.mainDcKey),
			.serverSalt = 0, // Not kept by the desktop app, like iOS.
			.name = QString(), // No local account names on the desktop.
		});
	}
	return result;
}

void StartExport(
		std::shared_ptr<Ui::Show> show,
		std::vector<not_null<Main::Account*>> accounts) {
	if (accounts.empty()) {
		show->showToast(SelectAtLeastOneText());
		return;
	}
	show->showBox(Box(NewPasswordBox, show, std::move(accounts)));
}

void StartRestore(std::shared_ptr<Ui::Show> show) {
	FileDialog::GetOpenPath(
		show->toastParent().get(),
		RestoreText(),
		FileFilter(),
		[=](FileDialog::OpenResult &&result) {
			if (result.paths.isEmpty()) {
				return;
			}
			auto in = QFile(result.paths.front());
			const auto data = in.open(QIODevice::ReadOnly)
				? in.read(kMaxFileSize + 1)
				: QByteArray();
			if (data.size() > kMaxFileSize || !LooksLikeBackup(data)) {
				show->showToast(NotBackupText());
				return;
			}
			show->showBox(Box(AskPasswordBox, show, data));
		});
}

} // namespace Luckgram::Backup
