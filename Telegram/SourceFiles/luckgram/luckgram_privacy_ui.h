/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

namespace Ui {
class GenericBox;
class Show;
} // namespace Ui

// D8: boxes shared by double space and hidden chats.
namespace Luckgram::PrivacyUi {

// 「输入主密码」: one passcode field, checked against the local passcode on
// a worker (Domain::tryPasscode, which doesn't count lock-screen tries).
// done gets the typed passcode once it is correct; the box then closes.
// Shows 「主密码不正确」 and stays open on a wrong one; cancelled comes when
// the box closes without a correct passcode.
void AskMainPasscode(
	std::shared_ptr<Ui::Show> show,
	const QString &title,
	Fn<void(QString passcode)> done,
	Fn<void()> cancelled = nullptr);

// 「设置第二密码」: the main passcode, then the second one twice
// (CheckNewSecondPasscode). done gets both once they pass.
void AskNewSecondPasscode(
	std::shared_ptr<Ui::Show> show,
	Fn<void(QString second, QString main)> done);

} // namespace Luckgram::PrivacyUi
