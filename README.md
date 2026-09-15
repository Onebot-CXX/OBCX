# OBCX

OBCX 是面向 OneBot 11 与 Telegram 的 C++26 机器人运行时。当前版本采用两层
运行模型：进程级 `BotInstallation` 由固定 recipe 组装协议、传输、事件入口与操作
组件；generation 级 ABI 2 actor 继承 `ReflectedActor<Derived>`，以类型化 `handle`
重载声明业务输入，并由原生 work-stealing scheduler 执行。异步 handler 可通过
`ActorContext::await_asio` 等待网络、计时器和其他 Boost.Asio 操作。

Actor 看不到 installation、transport、token 或进程 capability registry。Bot 出站
只能使用 data-only `BotOperationGateway`；旧 `IBot`/provider 接口、`QQBot`、
`TGBot`、`BotRegistry` 与基于 RTTI 的 wrapper 已删除。

## 构建

推荐使用仓库提供的 Nix 开发环境：

```bash
nix develop
cmake --preset actor-dev
cmake --build --preset actor-dev --parallel
ctest --preset actor-dev
```

也可以在系统环境中构建；需要 Linux x86_64/arm64、CMake 3.30、GCC 16.1+
和 `-std=c++26 -freflection`（`__cpp_impl_reflection >= 202506L`），以及
`vcpkg-base.json` 中列出的依赖。Linux 默认在检测到 liburing 时启用 Asio 的
io_uring reactor，可用 `-DOBCX_ENABLE_IO_URING=OFF` 关闭。

安装 SDK 与运行时：

```bash
cmake --install build/actor-dev --prefix "$HOME/.local/obcx"
```

## 选择 actor package

`actors.toml` 只负责选择参与构建的 package；package 自身的身份、ABI、依赖、
兼容范围与发布信息只来自 package 内唯一的 `actor.toml`。可复制
[actors-example.toml](actors-example.toml) 后按需选择本地 package：

```toml
schema_version = 1

[[actors]]
path = "local_actor/obcx-message-store"
enabled = true

[[actors]]
path = "local_actor/obcx-message-bridge"
enabled = true
```

远程 package 使用 `repository` 与 `revision`；`revision` 必须是不可变 tag 或完整
commit revision，不能依赖会移动的分支名。配置阶段会通过
`OBCXActorLoader.cmake` 加载所选 package。配置完成后，依照实际 preset 的 binary
directory 合并 vcpkg 依赖：

```bash
python3 cmake/gen_vcpkg_manifest.py actors.toml --binary-dir build/actor-dev
python3 cmake/gen_vcpkg_manifest.py actors.toml --binary-dir build/actor-dev --list
```

仓库内四个固定版本的 standalone actor 源码可在无网络环境恢复：

```bash
sh packaging/actors/restore-sources.sh
```

## 运行配置

运行时配置包括 bot installation、actor runtime、command route、数据库、actor 与
pipeline。Bot 配置使用关闭的 typed schema；每个表键就是 ingress 与 operation
routing 使用的精确
installation id。当前只接受 `onebot11.qq + websocket`、`onebot11.qq + http` 和
`telegram.bot_api + http`。缺失、未知或不支持的 `surface`/`transport` 组合直接
报错，不存在 provider 或 transport 回退。

```toml
[bots.qq_bot]
enabled = true
surface = "onebot11.qq"
transport = "websocket"

[bots.qq_bot.connection]
host = "127.0.0.1"
port = 3001
access_token = ""
connect_timeout_ms = 5000
action_timeout_ms = 30000

[bots.telegram_bot]
enabled = true
surface = "telegram.bot_api"
transport = "http"

[bots.telegram_bot.connection]
host = "api.telegram.org"
port = 443
access_token = "YOUR_TELEGRAM_BOT_TOKEN"
bot_username = "your_bot_username"
use_tls = true
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_timeout_ms = 25000
poll_force_close_ms = 30000
poll_retry_interval_ms = 3000

[actor_runtime.scheduler]
policy = "stealing"
workers = 0
blocking_workers = 0
slow_resume_warning_ms = 10

[actor_runtime.routing]
hop_limit = 32

[actor_runtime.reload]
drain_timeout_ms = 5000

[db.instances.main]
type = "sqlite"
path = "data/obcx.sqlite3"

[actors.message_store]
library = "message_store"
enabled = true
partition = "source_platform:conversation_id"
db = "main"
db_namespace = "message_store"

[actors.bridge]
library = "bridge"
enabled = true
requires = ["message_store"]
partition = "source_bot:conversation_id"
db = "main"
db_namespace = "bridge"

[actors.bridge.config]
bridge_files_dir = "/tmp/bridge_files"
legacy_state_pair = "primary"
legacy_unresolved_mapping_policy = "fail"
enable_retry_queue = true
message_retry_max_attempts = 5
message_retry_base_interval_sec = 2
retry_queue_check_interval_sec = 10
max_retry_interval_sec = 300

[[actors.bridge.config.installation_pairs]]
id = "primary"
telegram_installation = "telegram_bot"
onebot11_installation = "qq_bot"

[pipelines.message]
source = "obcx::core::events::RawMessageEvent"

[[pipelines.message.stages]]
name = "persist"
actor = "message_store"
input = "obcx::core::events::RawMessageEvent"
output = "obcx::message_store::events::MessageStored"
mode = "await"

[[pipelines.message.stages]]
name = "forward"
actor = "bridge"
input = "obcx::message_store::events::MessageStored"
output = ["bridge::events::MessageForwarded", "bridge::events::MessageForwardFailed"]
after = ["persist"]
mode = "await"
```

Bot 表与 connection 表拒绝 legacy `type`、bot-level `plugins`、无 `_ms` 单位的旧
超时键、未知键和 provider-misplaced 键。完整 recipe、生命周期和迁移表见
[Bot installation component runtime](docs/architecture/bot-component-runtime.md)。
Scheduler 没有引擎选择项；`policy` 只控制原生 scheduler 的 work
stealing/sharing 行为，`workers = 0` 与 `blocking_workers = 0` 由统一线程预算解析。

`library = "message_store"` 这类按名称发现会搜索当前 binary tree 的 `actors/`
目录，也会搜索已安装可执行文件对应的 `<prefix>/lib/obcx/actors`（以及 `lib64`
变体），因此 preset 构建与安装后的运行时不需要改写为绝对路径。

启动：

```bash
./build/actor-dev/src/app/obcx --no-tui config.toml
```

运行中可在 TUI 命令框或 `--no-tui` 的标准输入中输入 `reload`。命令接收后立即
返回；运行时会在后台构建完整候选 actor generation，关闭根 ingress、等待旧代已
接收的路由完成、原子切换并重新开放 ingress。切换不会重连 bot。仅 actor 条目、
actor 自有配置、pipeline 与路由策略可热更新；bot 定义、数据库实例或解析后的线程
预算变化会返回 `reload_restart_required`，必须重启进程。`--no-tui` 模式下按
Ctrl-C 会立即开始关闭，不需要再按 Enter 来结束标准输入行。

```toml
[actor_runtime.reload]
drain_timeout_ms = 5000 # 100..300000
```

若返回 `reload_drain_timeout`，运行时已回到旧 generation，候选已丢弃。若切换
成功但业务行为错误，恢复上一组不可变 actor 产物及其匹配配置后再次执行
`reload`。完整错误码与回滚流程见
[Actor operations](docs/architecture/actor-runtime-v2-operations.md)。

执行配置和 actor contract 校验：

```bash
./build/actor-dev/src/app/obcx --validate-config config.toml
```

`--validate-config` 会解析 typed bot variant、验证固定 component recipe、数据库
provider、actor contract 与依赖，以及 pipeline source、stage 依赖和
`await`/`async` mode。它不会 assemble/start `BotInstallation`、打开 provider
连接、发布命令目录或建立 bot ingress；actor 侧仍通过生产
`RuntimeGenerationBuilder` 完成 DSO contract、配置和 generation preparation 校验。

Bridge 的 bot、媒体与群组映射选项可参考
[actor-config.example.toml](local_actor/obcx-message-bridge/actor-config.example.toml)；
单账号部署可继续显式设置一组 `telegram_installation` 与
`onebot11_installation`；多账号部署使用具名 `installation_pairs`，并让每个群组或
Topic mapping 指向唯一 pair。Bridge schema v3 使用 installation、platform、
conversation 与 native message id 的完整消息身份；相同 Telegram message id 在不同
chat 中不会交叉回复、编辑或撤回。v2→v3 会严格预检当前/历史 route，无法确定的旧
mapping 默认阻止迁移，也可由管理员明确归档为生产不可读状态。首次迁移前必须停止
进程并进行包含 WAL 的 SQLite 一致性备份。当前仅 QQ/Telegram 的有限
typed operation 面、13 项 allow-list、retry safety 与明确延后范围见
[QQ/Telegram Bot Operation Boundary](docs/architecture/qq-telegram-bot-operations.md)。
actor 依赖与数据库选择以本页的当前运行配置为准。

## 编写 standalone actor

推荐从 [obcx-actor-template](local_actor/obcx-actor-template) 开始。Package 的
`actor.toml` 是唯一 metadata 来源，必须声明 identity、ABI 2、artifact、依赖、
兼容范围、发布信息，以及确实完成构建和验证的 `artifact.platforms`。registry 不会
为未声明的平台虚构下载。最小 CMake 入口如下：

```cmake
cmake_minimum_required(VERSION 3.30)
project(example_actor LANGUAGES CXX)

find_package(obcx-sdk CONFIG REQUIRED)
include(OBCXActor)

obcx_add_actor(example
  SOURCES src/example_actor.cpp
  OUTPUT_NAME example)
```

Actor library 继承 `ReflectedActor<Derived>`，公开精确的同步或异步 `handle`
重载，并使用 `OBCX_ACTOR_EXPORT_V2` 导出工厂、析构、名称、版本、数值 ABI 和
强制的 schema-2 输入 contract。需要在 ingress 发布前准备 actor-owned state 的
actor 可额外实现同步 `prepare_generation(ActorContext&)`，返回 typed `Ready`、
`Failed` 或 `RestartRequired`；运行时在配置及 generation service 就绪后、scheduler
注册前调用它。该导出是 ABI 2 的可选附加面，既有未提供该 symbol 的 actor 按
`Ready` 处理。`ActorManager` 在构造 actor 前校验 contract；旧二进制、短消息名和
缺少 contract 的 library 会被拒绝。

Actor 命令同样使用 typed message：actor contract 只声明命令名、说明和 request
message type，配置再按 platform/bot scope 激活路由；平台适配器不调用 actor
函数。完整的 `CommandCompleted`、`Continue`/`Consume`、fallback、聚合 catalog
和迁移约束见
[Actor command routing](docs/architecture/actor-command-routing.md)。

根仓库使用自有的通用 fixture 验证干净安装 SDK：

```bash
ctest --preset actor-dev -R '^actor_sdk_v2_smoke$'
```

独立 actor 仓库各自负责其 standalone build、安装、业务测试和跨 actor 集成；
根测试不会遍历或构建 `local_actor/` 下的外部仓库。

## Actor registry

`actor-registry/` 保存 actor-only entry schema、确定性索引生成器与 bridge、
message-store 发布项：

```bash
python3 actor-registry/generate_actor_index.py validate
python3 actor-registry/generate_actor_index.py generate --check
python3 actor-registry/generate_actor_index.py resolve \
  --id vollate.bridge --version 0.1.0 --platform linux-x86_64
```

## 验证与文档

```bash
ctest --preset actor-dev
python3 scripts/generate_api_docs.py
```

当前架构与运维说明：

- [Bot installation component runtime](docs/architecture/bot-component-runtime.md)
- [QQ/Telegram operation boundary](docs/architecture/qq-telegram-bot-operations.md)
- [Actor runtime ADR](docs/architecture/actor-runtime-adr.md)
- [Actor operations](docs/architecture/actor-runtime-v2-operations.md)
- [Actor author guide](docs/architecture/actor-v2-migration.md)
- [Actor package registry](actor-registry/README.md)

本次不兼容边界与升级后的唯一受支持配置见
[actor-only breaking change](docs/actor-only-breaking-change.md)。历史基准与路线图
不代表当前运行时选项。

## License

项目按仓库中的 [LICENSE](LICENSE) 发布。
