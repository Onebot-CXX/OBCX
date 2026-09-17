# OneBot 本地联调环境

这里保存不属于自动化测试的 QQ/LLOneBot 本地运行环境。运行数据和包含凭据的
TOML 配置由 `.gitignore` 排除，不能提交到仓库。

这里的 OBCX 配置必须使用当前 typed installation schema：

```toml
[bots.qq_bot]
enabled = true
surface = "onebot11.qq"
transport = "websocket"

[bots.qq_bot.connection]
host = "llonebot"
port = 3001
access_token = ""
connect_timeout_ms = 5000
action_timeout_ms = 30000
```

未知或不支持的 surface/transport 组合直接报错，不会回退到其他 provider 或
transport。Telegram Bot API 只能使用 `telegram.bot_api + http`。

目录约定：

- `QQ/`：NTQQ 运行数据。
- `llonebot/`：LLOneBot 程序与数据。
- `config/`：本地 OBCX 配置。
- `compose.yaml`：LLOneBot 以及可选 OBCX 容器。

actor 配置中的媒体路径必须与这里的挂载保持一致：

```toml
bridge_files_dir = "/absolute/path/to/OBCX/dev/onebot/llonebot/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
```

移动目录或修改这两个值后，需要 reload actor runtime 或重启 OBCX；已经运行的
bridge generation 不会自动重新读取磁盘上的 TOML。

只启动 LLOneBot：

```bash
docker compose up -d llonebot
```

同时启动预构建的 OBCX 镜像：

```bash
OBCX_LLOB_WEBUI_BIND=0.0.0.0:11451 \
  docker compose --profile server up -d
```

默认所有端口仅监听回环地址。需要远程访问时应显式覆盖对应的绑定变量。
