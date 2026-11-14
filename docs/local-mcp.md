# 本机 MCP 接口（切换账号）

让这台电脑上的 AI 工具（Claude Code、Cursor 等）查看 Luckgram Desktop 里已登录的账号，并切换当前账号。只做这两件事：读不到聊天和消息，也不能发消息。

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

## 代码

- `Telegram/SourceFiles/luckgram/mcp/luckgram_mcp_logic.*`：HTTP 解析、访问校验、JSON-RPC 方法（纯逻辑，`luckgram_tests` 里有测试）。
- `Telegram/SourceFiles/luckgram/mcp/luckgram_mcp_server.*`：本机套接字。
- `Telegram/SourceFiles/luckgram/mcp/luckgram_mcp.*`：接到应用的账号、开关和令牌。
- `Telegram/SourceFiles/settings/sections/settings_luckgram_mcp.*`：设置页。
