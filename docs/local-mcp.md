# 本机 MCP 接口（切换账号）

让这台电脑上的 AI 工具（Claude Code、Cursor 等）查看 Luckgram Desktop 里已登录的账号、切换当前账号，并在按账号开启 Skill 后查看该账号的好友和会话列表。读消息和发消息是两个单独的 Skill，不开启就读不到消息内容，也不能发消息。

## 开启

1. 设置 → Luckgram 功能 → 打开「本机 MCP 接口」（默认关闭）。
2. 点进这一行，点「复制客户端配置」，粘贴到 AI 工具的 MCP 配置里：

```json
{
  "mcpServers": {
    "luckgram": {
      "type": "http",
      "url": "http://127.0.0.1:28765/mcp",
      "headers": { "Authorization": "Bearer <访问令牌>" }
    }
  }
}
```

Claude Code 也可以用命令添加：

```bash
claude mcp add --transport http luckgram http://127.0.0.1:28765/mcp \
  --header "Authorization: Bearer <访问令牌>"
```

## 工具

| 工具 | 参数 | 作用 |
|---|---|---|
| `list_accounts` | 无 | 按账号列表的顺序列出已登录账号：`id`（Telegram 用户 id，字符串）、`index`（从 1 开始）、`name`、`username`、`phone`、`unread`、`active` |
| `get_active_account` | 无 | 主窗口当前显示的账号 |
| `switch_account` | `id` / `username` / `phone` / `index` 四选一 | 切换到该账号，和点侧栏里的账号是同一条路径。建议用 `id`：昵称会重复，`index` 会随增删账号变化 |

## Skill（按账号开启）

Skill 是一组工具，在设置页「本机 MCP 接口」里按账号单独开启，默认全部关闭。调用未开启的 Skill 会返回错误并说明是哪个账号、哪个 Skill。

Skill 工具都有可选参数 `account`（`list_accounts` 里的 `id`），不填就是当前账号；指定别的账号时不需要先切换，界面也不会变。

| Skill | 工具 | 参数 | 返回 |
|---|---|---|---|
| 好友 `friends` | `list_friends` | `account`、`query`（按昵称或用户名过滤）、`limit`（1–1000，默认 200）、`offset` | `total`、`friends`：`id`、`name`、`username`、`phone`、`bot`、`mutual` |
| 好友 `friends` | `find_user` | `account`，`username` / `phone` 二选一 | `user`：`id`、`name`、`username`、`phone`、`bot`、`friend`。每账号每小时 30 次、每天 100 次 |
| 管理好友 `friends_manage` | `add_friend` | `account`，`user_id` / `username` / `phone` 三选一，`first_name`、`last_name`（保存的备注名，可选） | `added`、`already_friend`、`user`。不发任何消息。每账号每小时 5 次、每天 20 次 |
| 管理好友 `friends_manage` | `remove_friend` | `account`，`user_id` / `username` 二选一 | `removed`、`was_friend`、`user`。聊天记录保留。每账号每小时 20 次、每天 50 次 |
| 会话 `chats` | `list_chats` | `account`、`unread_only`、`limit`（1–200，默认 50）、`offset` | `total`、`complete`、`chats`：`id`、`type`（user / bot / group / supergroup / channel）、`name`、`username`、`unread`、`unread_mark`、`muted`、`pinned`、`last_message_date` |
| 读消息 `messages` | `get_messages` | `account`、`chat`（`list_chats` 的 `id` 或用户名）、`limit`（1–100，默认 20）、`before_id` | `chat_id`、`chat_name`、`messages`（从新到旧）：`id`、`date`、`from_id`、`from_name`、`out`、`text`、`attachment`、`reply_to`、`service`。不会把会话标记为已读 |
| 发消息 `messages_send` | `send_message` | `account`、`chat`、`text`（1–4096 字，纯文本）、`reply_to`（可选） | `queued`、`chat_id`、`chat_name`。每账号每小时 30 次、每天 200 次 |
| 管理会话 `chats_manage` | `mark_chat_read` | `account`、`chat` | `chat_id`、`chat_name`、`was_unread`。每账号每小时 60 次、每天 500 次 |
| 群组和频道 `groups` | `join_chat` | `account`、`chat`（公开用户名或 `list_chats` 的 `id`） | `joined`、`already_member`。每账号每小时 5 次、每天 20 次 |
| 群组和频道 `groups` | `leave_chat` | `account`、`chat` | `left`、`was_member`。只支持超级群和频道，自己创建的不能用它退出。每账号每小时 10 次、每天 30 次 |
| 资料 `profile` | `update_profile` | `account`，`first_name` / `last_name` / `bio` 至少一个 | `updated`、`name`、`bio`。每账号每小时 5 次、每天 10 次 |

- `send_message` 交给应用自己的发送队列，和手动输入发送一样：发出去，或者在会话里显示发送失败。一次只发给一个会话，没有群发工具。
- 「管理好友」「发消息」「管理会话」「群组和频道」「资料」是会改动账号的 Skill：开启时桌面版会弹确认框，确认一次后该账号不再逐次询问。
- 频率上限按账号、按工具计数，重启后仍然有效，失败的调用也计数。超限时工具返回错误并给出还要等多少秒。上限是为了避免账号被 Telegram 限制，不能关闭。
- Telegram 自己返回 `FLOOD_WAIT` 时，工具会把要等的秒数原样告诉调用方。
- `list_chats` 只返回主列表（不含归档），不返回任何消息内容。`complete` 为 false 表示应用还只加载了长列表的前一部分。
- `list_friends` 在本次启动第一次调用时要向服务器取通讯录，最多等 15 秒。

## 行为

- 应用被本地密码锁住时，三个工具都返回错误，不列出也不切换。
- 双重空间（第二密码）下，隐藏的账号不会出现，也切换不到。
- 切换时如果应用需要用户先确认（比如正在通话），工具返回错误而不是报告成功。
- 端口默认 28765；被占用时依次试到 28784（同一台电脑开多份 Luckgram 时各占一个），实际端口显示在设置页。

## 安全

- 只监听 `127.0.0.1`，局域网和外网连不上。
- 每个请求都要带访问令牌。令牌首次开启时随机生成，存在系统凭据库（Windows 凭据管理器 / macOS 钥匙串），设置页可以重新生成，旧令牌立即失效。
- 拒绝带外部 `Origin` 或非本机 `Host` 的请求，网页无法借浏览器调用这个接口。
- 拿到令牌的本机程序都能切换账号，不要把令牌发给别人。

## 从另一台机器调用

接口不对外开放。Agent 跑在别的机器上时，用 SSH 把端口转到本地再连：

```bash
ssh -N -L 28765:127.0.0.1:28765 <用户>@<装有 Luckgram 的电脑>
```

## 云端 MCP

本机接口只能在这台电脑上用。打开云端 MCP 后，这台电脑会主动连到 `mcp.luckgram.cc`，AI 工具从任何地方都能用，多台电脑共用一个地址。

1. 在第一台电脑上：设置页「本机 MCP 接口」→ 打开「通过云端连接」（会生成一把云端密钥）→「复制云端客户端配置」，粘贴到 AI 工具里。
2. 在其他电脑上：同一页 →「使用另一台电脑的云端密钥」，粘贴第一台的密钥，再打开「通过云端连接」。

```json
{
  "mcpServers": {
    "luckgram-cloud": {
      "type": "http",
      "url": "https://mcp.luckgram.cc/mcp",
      "headers": { "Authorization": "Bearer lgc_..." }
    }
  }
}
```

- 工具和本机接口相同，每个工具多一个 `device` 参数（电脑的 id 或名称）；只有一台电脑在线时可以不填。
- 多出两个工具：`list_devices`（接入的电脑及是否在线）、`list_recent_calls`（最近 200 次调用的时间、工具、电脑、账号、是否失败）。
- Skill 仍然按账号开启，在每台电脑各自的应用里设置；频率上限也在每台电脑上各自计数。
- 云端只转发请求：不保存参数和结果，不保存聊天内容，也不保存云端密钥本身。
- 云端密钥在你的电脑上生成，存在系统凭据库。拿到它的人可以使用所有接入电脑上已开启的功能，泄露后请立即「重新生成云端密钥」。
- 不需要公网地址和端口转发；电脑关机或 Luckgram 退出后显示为离线。

## 代码

- `Telegram/SourceFiles/luckgram/mcp/luckgram_mcp_logic.*`：HTTP 解析、访问校验、JSON-RPC 方法（纯逻辑，`luckgram_tests` 里有测试）。
- `Telegram/SourceFiles/luckgram/mcp/luckgram_mcp_server.*`：本机套接字。
- `Telegram/SourceFiles/luckgram/mcp/luckgram_mcp.*`：接到应用的账号、开关和令牌。
- `Telegram/SourceFiles/luckgram/mcp/luckgram_mcp_cloud_client.*`：连接云端的长轮询；`luckgram_mcp_cloud.*`：云端开关和密钥。服务端在 `luckgram-server/mcp-relay`。
- `Telegram/SourceFiles/settings/sections/settings_luckgram_mcp.*`：设置页。
