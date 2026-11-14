/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "settings/sections/settings_luckgram_mcp.h"

#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/mcp/luckgram_mcp.h"
#include "luckgram/mcp/luckgram_mcp_logic.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Settings {
namespace {

namespace Mcp = Luckgram::Mcp;
using Luckgram::Tr;

[[nodiscard]] QString StatusText() {
	const auto enabled = Luckgram::Settings::IsEnabled(
		QString::fromLatin1(Mcp::kFeatureKey));
	if (!enabled) {
		return Tr(
			"未开启。在「Luckgram 功能」里打开「本机 MCP 接口」后生效。",
			"Off. Turn on \"Local MCP server\" on the Luckgram page.");
	} else if (Mcp::Running()) {
		return Tr("运行中：", "Running: ") + Mcp::ServerUrl(Mcp::Port());
	}
	const auto error = Mcp::LastError();
	return error.isEmpty() ? Tr("未运行", "Not running") : error;
}

[[nodiscard]] Mcp::Config CurrentConfig() {
	auto result = Mcp::Config();
	result.token = Mcp::Token();
	result.port = Mcp::Running() ? Mcp::Port() : int(Mcp::kDefaultPort);
	return result;
}

} // namespace

LuckgramMcp::LuckgramMcp(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> LuckgramMcp::title() {
	return rpl::single(Tr("本机 MCP 接口", "Local MCP server"));
}

void LuckgramMcp::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto copy = [=](const QString &text) {
		QGuiApplication::clipboard()->setText(text);
		controller->showToast(Tr("已复制", "Copied"));
	};

	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		rpl::single(StatusText()) | rpl::then(rpl::merge(
			Mcp::StateChanges(),
			Luckgram::Settings::Changes() | rpl::to_empty
		) | rpl::map([] {
			return StatusText();
		})));
	Ui::AddSkip(container);

	AddButtonWithIcon(
		container,
		rpl::single(Tr("复制客户端配置", "Copy client config")),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		copy(Mcp::ClientConfigJson(CurrentConfig()));
	});
	AddButtonWithIcon(
		container,
		rpl::single(Tr("复制地址", "Copy address")),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		copy(Mcp::ServerUrl(CurrentConfig().port));
	});
	AddButtonWithIcon(
		container,
		rpl::single(Tr("复制访问令牌", "Copy access token")),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		copy(Mcp::Token());
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		rpl::single(Tr(
			"把「客户端配置」粘贴到 AI 工具的 MCP 设置里（Claude Code、"
			"Cursor 等），它就能列出这台电脑上已登录的账号并切换当前账号。"
			"下面按账号开启 Skill 后，还能查看该账号的好友和会话列表。"
			"读不到消息内容，也不能发消息。\n\n"
			"只接受本机（127.0.0.1）的连接，每次请求都要带访问令牌。"
			"拿到令牌的本机程序都能切换账号，不要把令牌发给别人。",
			"Paste the client config into the MCP settings of your AI tool "
			"(Claude Code, Cursor and others). It can then list the "
			"accounts logged in on this computer and switch the active "
			"one. With a skill switched on below it also sees that "
			"account's friends or chat list. It never reads message "
			"texts and never sends anything.\n\n"
			"Only connections from this computer (127.0.0.1) are accepted "
			"and every request needs the access token. Any local program "
			"that has the token can switch accounts, so keep it private.")));
	Ui::AddSkip(container);

	// Skills, per account, all off until switched on here.
	const auto accounts = Mcp::AccountRows();
	if (!accounts.empty()) {
		Ui::AddSubsectionTitle(
			container,
			rpl::single(Tr("每个账号的 Skill", "Skills of each account")));
		Ui::AddDividerText(
			container,
			rpl::single(Tr(
				"Skill 是一组功能，按账号单独开启，默认全部关闭。"
				"关闭的 Skill，AI 工具调用时会被拒绝。",
				"A skill is a group of tools switched on per account; all "
				"are off at first. Calls to a skill that is off are "
				"refused.")));
		for (const auto &account : accounts) {
			Ui::AddSkip(container);
			Ui::AddSubsectionTitle(container, rpl::single(account.name));
			for (const auto &skill : Mcp::Skills()) {
				const auto key = QString::fromLatin1(skill.key);
				const auto id = account.id;
				const auto button = AddButtonWithIcon(
					container,
					rpl::single(Tr(skill.titleZh, skill.titleEn)
						+ u" · "_q
						+ Tr(skill.aboutZh, skill.aboutEn)),
					st::settingsButtonNoIcon);
				button->toggleOn(rpl::single(Mcp::SkillEnabled(id, key)));
				button->toggledChanges(
				) | rpl::filter([=](bool toggled) {
					return (toggled != Mcp::SkillEnabled(id, key));
				}) | rpl::on_next([=](bool toggled) {
					Mcp::SetSkillEnabled(id, key, toggled);
				}, button->lifetime());
			}
		}
		Ui::AddSkip(container);
		Ui::AddDivider(container);
		Ui::AddSkip(container);
	}

	AddButtonWithIcon(
		container,
		rpl::single(Tr("重新生成访问令牌", "Regenerate access token")),
		st::settingsAttentionButton
	)->addClickHandler([=] {
		controller->show(Ui::MakeConfirmBox({
			.text = Tr(
				"旧令牌立即失效，已配置的 AI 工具需要换成新令牌。",
				"The old token stops working at once. Tools set up with it "
				"need the new one."),
			.confirmed = [=](Fn<void()> close) {
				Mcp::RegenerateToken();
				close();
				controller->showToast(
					Tr("已生成新令牌", "New token generated"));
			},
			.confirmText = Tr("重新生成", "Regenerate"),
		}));
	});
	Ui::AddSkip(container);

	Ui::ResizeFitChild(this, container);
}

} // namespace Settings
