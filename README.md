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
# 首次创建显式 workspace；不要覆盖已有的本地选择。
cp packages-example.toml packages.toml
cmake --preset actor-dev
cmake --build --preset actor-dev --parallel "$(nproc)"
```

也可以在系统环境中构建；需要 Linux x86_64/arm64、CMake 3.30、GCC 16.1+
和 `-std=c++26 -freflection`（`__cpp_impl_reflection >= 202506L`），以及
`vcpkg-base.json` 中列出的依赖。Linux 默认在检测到 liburing 时启用 Asio 的
io_uring reactor，可用 `-DOBCX_ENABLE_IO_URING=OFF` 关闭。

以上是 **core + actor 的完整 workspace 构建**，不是热重载隔离构建。
即使指定单个 actor target，过期的 core 依赖仍会被重新编译。
只修改 actor 并准备热重载时，请使用
[固定 SDK 的隔离构建流程](packaging/actors/README.md)，不要覆盖运行中进程的
`libobcx_core.so`。现有 `actor-dev` / `actor-release` preset 名称保留兼容，
不代表它们只构建 actor。

安装 SDK 与运行时：

```bash
cmake --install build/actor-dev --prefix "$HOME/.local/obcx"
```

### 可选堆内存 profiling

可用默认关闭的 `-DOBCX_ENABLE_JEMALLOC_PROFILING=ON` 构建 jemalloc 诊断应用，
通过 `memory_stats` 查看分配器统计、`memory_dump=./obcx`
导出采样调用栈。需要独立构建和批准后的进程重启，不向在线进程注入 profiler。
构建、启动及 core/actor 归属分析见 [jemalloc 内存诊断](packaging/jemalloc-profiling.md)。

## 选择 actor package

根构建在 CMake configure 内离线解析 v2 `packages.toml`；包内
`package.toml` 是身份、artifact、依赖和兼容范围的唯一声明。
依赖图是构建目录内自动生成的中间产物，不需要锁文件或手动 Python 准备步骤；
删除构建目录后重新执行同一 configure 命令即可。示例
[packages-example.toml](packages-example.toml) 显式选择空 roots，只构建 core/SDK。
选择 actor/library 时必须同时提供 roots、来源以及 provider 的发现方式和 targets。
系统依赖仅由 Nix/vcpkg 的 lock/baseline 锁定；不再要求 `.package-state/providers/`、
安装路径白名单或额外版本 receipt。CMake 读取实际版本，检查每个消费者声明的版本范围、
SDK 兼容范围及所需 targets。本地 `packages.toml` 不纳入版本控制。

迁移旧 workspace 时，删除 `providers[].version`、`providers[].provenance` 和
`providers[].package_manager.baseline`；不要删除 actor 依赖中的版本范围。
`version_probe` 使用 CMake 版本变量、target property 或 pkg-config，不再接受 receipt。
Nix store 路径变化不需要更新 workspace；构建目录内的观测记录仅用于当次检查，
不是需要保留或手工恢复的锁文件。

路径映射库 `obcx.path-mapping` 在独立 Git 仓库
`local_library/obcx-path-mapping/` 中维护，不属于 core 源码，也不是 submodule。
`local_library/` 与 `local_actor/` 一样由 core 忽略；选择 Bridge 或 ExHentai
时，须在 workspace 显式绑定该库来源，不能依赖 core 自动附带或下载它。
此处是本地开发目录约定，尚未配置远程发行地址。

configure 每次检查当前声明的依赖关系、版本、来源及实际 CMake targets。
Git 来源必须绑定完整 commit；所需源码必须已在本地或已校验缓存中，configure
不下载、不重新选版本，也不读取旧 `actors.toml`。`package_tool.py resolve` 仍可
按需导出依赖图用于诊断，但不是构建前提。

CMake presets 明确选择 development 来源模式；`actor-release` 仅选择优化的
编译配置，不代表已完成正式发行验证。非 preset 构建必须显式传入
`OBCX_PACKAGES_WORKSPACE/CACHE/MODE/STATE_DIR` 和构建配置。
详见 [CMake 接入](docs/architecture/package-cmake.md) 与
[provider 发现声明](schemas/workspace.schema.json)。

> 旧格式 reader 已删除，registry 本地检查统一使用 core 工具；当前索引只登记源码
> 元数据，不宣称已有可下载的 v2 发行物。发布脚本/旧源码快照的完整迁移仍待完成，
> 不要将旧归档恢复流程当作 v2 workspace 的准备入口。没有 v1 fallback。

### Actor 的 Nix 开发依赖

在 Linux 的 OBCX checkout 根目录或其子目录运行普通 `nix develop` 即可：

- 没有 `packages.toml`，或 `workspace.roots` 为空：只提供 core 开发环境。
- 配置了 roots：自动合并所选 actor/library 及传递依赖的 `shell.nix`（如果有）。
- workspace 无效、依赖解析失败或 `shell.nix` 求值失败：明确报错并停止进入 shell，
  不会静默退回 core-only。

`nix develop --command ...`、`nix develop .#ci` 和 `.envrc` 使用同样的自动加载逻辑。
由于本地 workspace/actor 被 Git 忽略，加载在 shellHook 阶段执行，而不是纯 flake
求值阶段；无需手动传 `--impure`。自动入口显式采用 development 模式、离线源码
策略，并复用 `actor-dev` 的 `build/actor-dev/package-state/sources` 缓存路径。

需要自定义 workspace、缓存或源码联网策略时，仍可使用 Python 3.11+ 运行显式
入口（以下选项都必须传入）：

```bash
python3 scripts/nix_develop.py \
  --workspace packages.toml \
  --cache .cache/obcx/packages \
  --mode development \
  --network deny \
  --shell workspace
```

非交互执行可在末尾添加 `-- <命令> <参数>`；CI 使用 `--shell workspace-ci`
省略根环境的 clang tooling。入口每次复用 CMake 的包解析器，从当前
`workspace.roots` 沿 actor/library 依赖解析；`profile = "tests"` 时也包含测试依赖。
仅加载选中包根目录的 `shell.nix`，不扫描未选中包，不加载 `flake.nix`。
包没有 `shell.nix` 时不增加 Nix 依赖。远程源码必须已在缓存中；如需准备源码，
显式选择 `--network allow`（该选项只控制包源码获取，不限制 Nix 下载）。

Actor 或 library 可提供以下 `shell.nix`：

```nix
{ pkgs }:
pkgs.mkShell {
  buildInputs = [ pkgs.openssl ];
  nativeBuildInputs = [ pkgs.pkg-config ];
}
```

文件必须是接收 `pkgs` 并返回 `mkShell` derivation 的函数。根 flake 统一传入其
锁定的 nixpkgs 包集，通过 `inputsFrom` 合并 `packages`、`buildInputs`、
`nativeBuildInputs` 及 propagated inputs；不继承包的 `shellHook`、环境变量或
stdenv，Linux 根环境仍使用 GCC 16。不要在包的输入列表中另加冲突的编译器。

内部启动器显式提供 `OBCX_NIX_SHELL_INPUTS` 并使用 `--impure` 求值
`.#workspace` / `.#workspace-ci`，自动入口通过 `nix print-dev-env` 将结果加载到
当前 shell，不重新启动交互 shell 或丢失 `--command` 参数。不要手动复用该内部
环境变量。变更 roots、依赖或 `shell.nix` 后重新进入 shell；direnv 用户可执行
`direnv reload`（`.envrc` 也会监视根 `packages.toml` 的变化）。只应加载可信包，
`shell.nix` 是可执行的 Nix 表达式，依赖自身的 setup hooks 仍可执行。
workspace shell 仅支持 Linux，且 Nix system 必须匹配 workspace 声明的平台；
macOS 仍为 tooling-only。

Nix 负责提供并锁定开发依赖，不会自动放宽 actor 的版本要求。例如 `chat_llm`
要求 ICU 76.x，其 `shell.nix` 显式使用 `pkgs.icu76`，不能直接以 `pkgs.icu`（78.x）
替代；CMake 会拒绝实际版本与声明范围不兼容的依赖。无需另外维护 provider 路径证据。
热重载继续使用固定 SDK 的隔离构建流程，不在 live core workspace 构建 actor。

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

### 交互 TUI：选区复制与退出

- **Ctrl+C**：有输出选区时复制，保留正在编辑的命令；无选区时只清空命令和
  光标位置，**不会退出**。在任一面板聚焦时行为相同。
- **Ctrl+D**：仅命令框完全为空时，向本进程提交 SIGINT，由原有关闭流程排空
  工作并显示最终日志。非空（包括纯空白）时不删除、不执行、不退出。显式
  `exit` 命令和外部 SIGINT/SIGTERM 的关闭行为保持不变。
- 鼠标在日志或 CLI 输出中拖动可选择连续文本；跨面板拖动会限制在起始面板。
  高亮按源条目及完整 UTF-8 字形定位，复制保留空格和显式换行，不附加软折行、
  边框或滚动条。拖动及松开鼠标**不会自动复制**。
- 选区存在时暂停该面板的自动跟随，但继续收集输出；窗口重排保持源锚点。
  日志被原有保留策略淘汰后，选区失效并提示重新选择，不会复制过期缓存。
  **Esc** 取消选区且不自动跳至末尾；滚轮、PageUp/PageDown、Home/End、
  分隔条调整和滚动条拖动先清除选区，再执行原导航。**End** 恢复日志尾随。

复制使用写入型 **OSC 52**（剪贴板 `c`，Base64 编码的纯 UTF-8 文本），目标是
运行终端客户端机器的剪贴板。通过 SSH 使用时不需要服务器上的图形剪贴板，
也不会运行 `xclip`、`wl-copy`、SSH 或 tmux 子进程。每次复制最多 **64 KiB
原始 UTF-8 字节**；这是实现的资源上限，不是配置默认值，也不保证终端接受
同样大小。超限整次拒绝，保留选区，不截断或分块覆盖剪贴板。

终端客户端必须允许 OSC 52 剪贴板写入。非空 `TMUX` 环境变量使应用增加一层
正确转义的 tmux DCS passthrough；否则发送普通 OSC 52。tmux 的
`allow-passthrough`、适用的 `set-clipboard`/终端剪贴板能力，以及每个外层
复用器的转发策略均由操作者管理，应用不更改它们。嵌套 SSH/tmux 不会被自动
完整探测或自动多层封装，所有相关层都须允许该转发路径；某层过滤或拒绝时，
请求可能无法到达客户端。

TUI 反馈只确认“**已发出复制请求**”，不证明远端剪贴板已经改变；它不读取
剪贴板，也不记录选中文本/Base64。非交互/重定向输出禁止剪贴板转义序列。
请用非敏感文本分别在实际直连、SSH、tmux 和嵌套环境中手动粘贴验收；构建
成功或出现“请求已发出”不能替代客户端验收。

### 配置校验

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
`package.toml` 是唯一 metadata 来源，必须声明 identity、ABI 2、artifact、依赖、
兼容范围、发布信息，以及确实完成构建和验证的 `artifact.platforms`。registry 不会
为未声明的平台虚构下载。最小 CMake 入口如下：

```cmake
cmake_minimum_required(VERSION 3.30)
if(NOT DEFINED OBCX_CURRENT_PACKAGE)
  project(example_actor LANGUAGES CXX)
  find_package(obcx-sdk CONFIG REQUIRED GLOBAL)
  include(OBCXPackages)
  obcx_load_configured_workspace() # 六个 OBCX_PACKAGES_* 参数全部显式提供
  return()
endif()

obcx_add_actor(SOURCES src/example_actor.cpp) # 名称、输出和外部链接来自 TOML
```

Actor 名称和版本由 `package.toml` 的 `actor.name`、`package.version` 在构建时
自动生成并继承，不需在 C++ 类中重复声明。actor 主目标和已注册内部实现库的
日志宏自动使用 `[actor_name]`，core 自身使用 `[core]`；异步任务不改变来源标签。

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

根构建只加载 workspace 明确选中的包，不遍历全部 `local_actor/`。
当前工作区已移除源码测试目录内容及对应构建入口，使用 `production` profile。

## Package registry

`actor-registry/` 是独立 registry 的开发期检查快照，actor/library 均读取同一份
v2 元数据规则。当前只登记元数据，不根据版本号猜测下载地址：

```bash
python3 cmake/package_tool.py registry-validate --entries actor-registry/entries
python3 cmake/package_tool.py registry-index --entries actor-registry/entries \
  --output actor-registry/index/packages.json --check
```

经验证的发行物、发布工具的正式版本固定和远程发布仍后置。

## 文档

```bash
python3 scripts/generate_api_docs.py
```

当前架构与运维说明：

- [Bot installation component runtime](docs/architecture/bot-component-runtime.md)
- [QQ/Telegram operation boundary](docs/architecture/qq-telegram-bot-operations.md)
- [Actor runtime ADR](docs/architecture/actor-runtime-adr.md)
- [Actor operations](docs/architecture/actor-runtime-v2-operations.md)
- [Current-only runtime and schemas](docs/architecture/current-schema-only.md)
- [Actor author guide](docs/architecture/actor-v2-migration.md)
- [Actor package registry](actor-registry/README.md)

本次不兼容边界与升级后的唯一受支持配置见
[actor-only breaking change](docs/actor-only-breaking-change.md)。历史基准与路线图
不代表当前运行时选项。

## License

项目按仓库中的 [LICENSE](LICENSE) 发布。
