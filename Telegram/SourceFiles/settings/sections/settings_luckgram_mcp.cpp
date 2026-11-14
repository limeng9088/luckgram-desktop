/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "settings/sections/settings_luckgram_mcp.h"

#include "luckgram/luckgram_settings.h"
#include "luckgram/luckgram_strings.h"
#include "luckgram/mcp/luckgram_mcp.h"
#include "luckgram/mcp/luckgram_mcp_cloud.h"
#include "luckgram/mcp/luckgram_mcp_logic.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Settings {
namespace {

namespace Mcp = Luckgram::Mcp;
namespace Cloud = Luckgram::Mcp::Cloud;
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

[[nodiscard]] QString CloudStatusText() {
	if (!Cloud::Enabled()) {
		return Tr("未开启。", "Off.");
	}
	const auto name = Tr("设备名：", "Device name: ") + Cloud::DeviceName();
	if (Cloud::Connected()) {
		return Tr("已连接云端。", "Connected to the cloud. ") + name;
	}
	const auto error = Cloud::LastError();
	return (error.isEmpty()
		? Tr("正在连接云端…", "Connecting to the cloud... ")
		: (Tr("连接不上云端，会自动重试：", "Can't reach the cloud, retrying: ")
			+ error
			+ u" "_q))
		+ name;
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
			"下面按账号开启 Skill 后，还能使用该账号的好友、会话、"
			"消息等功能。没开启的 Skill 一律不可用。\n\n"
			"只接受本机（127.0.0.1）的连接，每次请求都要带访问令牌。"
			"拿到令牌的本机程序都能切换账号，不要把令牌发给别人。",
			"Paste the client config into the MCP settings of your AI tool "
			"(Claude Code, Cursor and others). It can then list the "
			"accounts logged in on this computer and switch the active "
			"one. With skills switched on below it also uses that "
			"account's friends, chats and messages. A skill that is "
			"off is never available.\n\n"
			"Only connections from this computer (127.0.0.1) are accepted "
			"and every request needs the access token. Any local program "
			"that has the token can switch accounts, so keep it private.")));
	Ui::AddSkip(container);

	// Cloud MCP: the same tools through mcp.luckgram.cc.
	Ui::AddSubsectionTitle(
		container,
		rpl::single(Tr("云端 MCP", "Cloud MCP")));
	const auto cloudShown = container->lifetime().make_state<
		rpl::variable<bool>>(Cloud::Enabled());
	const auto cloud = AddButtonWithIcon(
		container,
		rpl::single(Tr("通过云端连接", "Connect through the cloud")),
		st::settingsButtonNoIcon);
	cloud->toggleOn(cloudShown->value());
	cloud->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return (toggled != Cloud::Enabled());
	}) | rpl::on_next([=](bool toggled) {
		if (!toggled) {
			Cloud::SetEnabled(false);
			*cloudShown = false;
			return;
		}
		cloudShown->force_assign(false); // Back until confirmed.
		controller->show(Ui::MakeConfirmBox({
			.text = Tr(
				"开启后，这台电脑会主动连接 mcp.luckgram.cc。"
				"拿到云端密钥的 AI 工具可以从任何地方使用这台电脑上"
				"已开启的功能和 Skill。云端只转发请求，不保存聊天内容。"
				"云端会看到这台电脑的设备名（可在下方修改）。",
				"With this on, this computer connects to mcp.luckgram.cc. "
				"AI tools that have the cloud key can use, from anywhere, "
				"what is switched on on this computer. The cloud passes "
				"requests on and stores no chats. It sees the device name "
				"of this computer, which you can change below."),
			.confirmed = [=](Fn<void()> close) {
				Cloud::SetEnabled(true);
				*cloudShown = true;
				close();
			},
			.confirmText = Tr("开启", "Turn on"),
		}));
	}, cloud->lifetime());
	Ui::AddDividerText(
		container,
		rpl::single(CloudStatusText()) | rpl::then(rpl::merge(
			Cloud::StateChanges(),
			Luckgram::Settings::Changes() | rpl::to_empty
		) | rpl::map([] {
			return CloudStatusText();
		})));
	Ui::AddSkip(container);
	AddButtonWithIcon(
		container,
		rpl::single(Tr("复制云端客户端配置", "Copy cloud client config")),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		const auto key = Cloud::Key();
		if (key.isEmpty()) {
			controller->showToast(
				Tr("请先开启云端 MCP", "Turn Cloud MCP on first"));
		} else {
			copy(Mcp::CloudClientConfigJson(key));
		}
	});
	AddButtonWithIcon(
		container,
		rpl::single(Tr("复制云端密钥", "Copy cloud key")),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		const auto key = Cloud::Key();
		if (key.isEmpty()) {
			controller->showToast(
				Tr("请先开启云端 MCP", "Turn Cloud MCP on first"));
		} else {
			copy(key);
		}
	});
	AddButtonWithIcon(
		container,
		rpl::single(Tr(
			"使用另一台电脑的云端密钥",
			"Use the cloud key of another computer")),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(rpl::single(Tr("云端密钥", "Cloud key")));
			const auto field = box->addRow(object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				Ui::InputField::Mode::SingleLine,
				rpl::single(u"lgc_..."_q),
				QString()));
			field->setMaxLength(64);
			box->setFocusCallback([=] { field->setFocusFast(); });
			box->addButton(rpl::single(Tr("使用", "Use")), [=] {
				if (!Cloud::UseKey(field->getLastText())) {
					field->showError();
					return;
				}
				box->closeBox();
				controller->showToast(Tr(
					"已使用这把密钥，这台电脑会出现在同一个云端地址下",
					"Key saved: this computer joins the same cloud address"));
			});
			box->addButton(
				rpl::single(Tr("取消", "Cancel")),
				[=] { box->closeBox(); });
		}));
	});
	AddButtonWithIcon(
		container,
		rpl::single(Tr("修改设备名", "Change the device name")),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(rpl::single(Tr("设备名", "Device name")));
			const auto field = box->addRow(object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				Ui::InputField::Mode::SingleLine,
				rpl::single(Tr(
					"留空则使用电脑名",
					"Empty: the computer's own name")),
				Cloud::DeviceName()));
			field->setMaxLength(64);
			box->setFocusCallback([=] { field->setFocusFast(); });
			box->addButton(rpl::single(Tr("保存", "Save")), [=] {
				Cloud::SetDeviceName(field->getLastText());
				box->closeBox();
			});
			box->addButton(
				rpl::single(Tr("取消", "Cancel")),
				[=] { box->closeBox(); });
		}));
	});
	AddButtonWithIcon(
		container,
		rpl::single(Tr("重新生成云端密钥", "Regenerate cloud key")),
		st::settingsAttentionButton
	)->addClickHandler([=] {
		controller->show(Ui::MakeConfirmBox({
			.text = Tr(
				"旧密钥立即失效。其他电脑和已配置的 AI 工具都要换成新密钥。",
				"The old key stops working at once. The other computers "
				"and the tools set up with it need the new one."),
			.confirmed = [=](Fn<void()> close) {
				Cloud::RegenerateKey();
				close();
				controller->showToast(
					Tr("已生成新密钥", "New key generated"));
			},
			.confirmText = Tr("重新生成", "Regenerate"),
		}));
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		rpl::single(Tr(
			"多台电脑用同一把云端密钥，AI 工具只连一个地址就能操作所有"
			"电脑：先调用 list_devices，再用 device 参数指定电脑。"
			"每台电脑上的 Skill 仍然在各自的应用里按账号开启。",
			"Computers that use the same cloud key are reached at one "
			"address: the tool calls list_devices, then names the "
			"computer with \"device\". Skills are still switched on per "
			"account in the app on each computer.")));
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
				const auto write = skill.write;
				const auto name = account.name;
				// The switch shows what is stored; a write skill goes on
				// only after the user said yes for this account.
				const auto shown = button->lifetime().make_state<
					rpl::variable<bool>>(Mcp::SkillEnabled(id, key));
				button->toggleOn(shown->value());
				button->toggledChanges(
				) | rpl::filter([=](bool toggled) {
					return (toggled != Mcp::SkillEnabled(id, key));
				}) | rpl::on_next([=](bool toggled) {
					if (!toggled || !write) {
						Mcp::SetSkillEnabled(id, key, toggled);
						*shown = toggled;
						return;
					}
					shown->force_assign(false); // Back until confirmed.
					controller->show(Ui::MakeConfirmBox({
						.text = Tr(
							"开启后，拿到访问令牌的 AI 工具可以用账号「",
							"With this on, AI tools that have the access "
							"token can act as \"")
							+ name
							+ Tr("」执行：", "\": ")
							+ Tr(skill.aboutZh, skill.aboutEn)
							+ Tr(
								"。不会再逐次询问，超过频率上限会被拒绝。",
								". They are not asked each time; calls "
								"over the rate limit are refused."),
						.confirmed = [=](Fn<void()> close) {
							Mcp::SetSkillEnabled(id, key, true);
							*shown = true;
							close();
						},
						.confirmText = Tr("开启", "Turn on"),
					}));
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
