## Why

当前组件化只隔离了 Actor 与 Bot 运行时，却仍由核心 SDK 的 `BotSurface`、`BotAction`、全平台 `BotOperationClient` 和连接配置 `std::variant` 汇总 Telegram/OneBot 知识。新增一个平台操作仍需修改公共枚举、dispatcher 和无关平台的编译依赖；需要把平台契约和组装规则归还平台模块，同时保留严格验证和精确 installation 路由。

## What Changes

- 分离平台无关的标识、引用、错误、结果、调用网关与公共消息契约；Telegram topic/entity/media 和 OneBot member/forward/file/poke 的类型、序列化、验证及客户端由各平台模块拥有。
- **BREAKING**：移除全局 `BotSurface` / `BotAction`、`action_ids::all` 和 `action_supports_surface()`，以无平台枚举的 `SurfaceId` / `ActionId` 值类型及平台拥有的操作描述替代。保留现有 surface/action 字符串及当前 13 个生产 action，不新增业务能力。
- **BREAKING**：以平台无关的 `BotOperationGateway` 和按需包含的强类型调用适配器替代枚举所有平台方法的 `BotOperationClient` / endpoint。网关只路由已注册的 SDK 操作，不提供任意 provider method、URL 或原始响应透传。
- 将连接 schema、平台 typed config、组件、recipe、事件平台映射及命令适配逻辑移至 OneBot/Telegram 模块；应用 composition root 显式注册内置模块，通用 config loader / assembler 不再维护跨平台连接 variant 或平台 switch。
- 保持当前三个受支持 recipe 和显式 TOML 配置格式；未注册平台/传输、缺失字段、重复注册和不匹配请求继续在 provider I/O 前失败。不引入任何配置默认值、动态插件加载或任意组件列表配置。
- **BREAKING**：迁移所有 Actor 消费者和安装 SDK，使用生成的 actor contract schema 版本门禁拒绝旧 SDK 构建的 DSO，要求配套重建和部署，避免服务接口/值布局变化被 ABI 2 名称掩盖。
- 增加公共 SDK、单平台 SDK 和通用 runtime 的独立编译测试，以及仅通过注册测试模块即可扩展的测试；保留错误脱敏、上传可选能力、媒体限制、reload/shutdown 和 Bridge 多 installation/会话隔离行为。

## Capabilities

### New Capabilities

- `bot-platform-modularity`: 公共与平台契约的单向依赖、开放标识、平台模块所有权、独立编译及不修改通用核心的注册扩展。

### Modified Capabilities

- `qq-telegram-bot-contract`: 将全局封闭类型集合改为平台拥有的强类型契约；保留当前生产操作、值语义及 wire ID。
- `bot-operation-dispatch`: 通用网关、endpoint-local 操作注册、强类型适配、精确路由和受控 data-only 分发。
- `bot-component-runtime`: 平台模块提供 recipe 和能力依赖，通用安装目录和生命周期不认识具体平台。
- `bot-installation-configuration`: 模块拥有连接解析和 typed configuration，替代中央 variant，保持显式配置及无 I/O 校验。
- `actor-command-routing`: 命令检测/发布适配器从已注册模块取得，消除通用协调器对 Telegram 类型和配置字段的依赖。
- `actor-abi-v2`: 用 actor contract schema 版本区分新的 SDK 服务契约，在 actor 构造前拒绝不兼容 DSO。
- `bridge-capability-forwarding`: Bridge 改用公共/平台强类型适配器和通用网关，保持精确 pair、conversation 路由。
- `bridge-actor-message-retry`: 重试回调迁移到新网关，不改变持久化目标、有限重试及可能已提交的处理规则。
- `chat-llm-capability-egress`: Chat LLM 按需引用公共群消息和 Telegram topic 契约，不依赖所有平台的集中客户端。

`bot-component-runtime`、`bot-installation-configuration` 已由完成但尚未归档的 `complete-bot-component-migration` 定义。本提案以前序已实现变更及当前源码为基线；实施时先同步相关已完成 delta，不能恢复旧 live-bot wrapper、默认连接字段、单 pair 或旧会话映射行为。

## Impact

- 核心 SDK/runtime：`include/core/bot/`、`src/core/bot/`、`include/common/config_loader.hpp`、`src/common/config_loader.cpp`、`src/core/command/`、`src/core/runtime/`、`src/app/` 及 actor contract 生成/加载。
- 平台模块：`include/onebot11/`、`include/telegram/`、`src/onebot11/`、`src/telegram/`；复用现有协议和传输实现，不增加第三个平台或 provider API。
- 消费者：`local_actor/obcx-message-bridge`、`chat_llm`、Message Store、template、独立 SDK fixtures、mock gateway、架构测试和 benchmarks。
- 发布链路：CMake 安装/export、SDK 元数据、actor registry、offline bundles/patches/pins、CI 和 migration 文档必须配套更新；原 actor 调度 ABI 2 和业务事件格式保持不变，但旧二进制不兼容。
- 不新增数据库表、不迁移当前 Bridge schema 3、不改变 Message Store 事件/存储、命令路由配置、provider token、网络行为或超时/重试策略。
