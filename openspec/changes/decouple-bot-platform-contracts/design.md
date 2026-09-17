## Context

当前代码已经使用 `BotInstallation` / `BotComponent` / `CapabilityRegistry` 隔离运行时所有权，但平台知识仍集中在几个公共入口：

- `include/core/bot/bot_operation_types.hpp` 定义全部 surface、13 个 action、平台兼容矩阵及 `TelegramTopicTarget`。
- `bot_operations.hpp` 混有通用群消息、Telegram topic 和 Telegram edit；`bot_operation_client.hpp` 经 umbrella header 包含所有平台 DTO，并为所有 action 声明虚函数。
- `include/common/config_loader.hpp` 导出跨平台连接 variant；`src/common/config_loader.cpp` 负责所有平台连接解析。
- `src/core/bot/bot_installation_assembler.cpp` 同时定义两个平台的 recipe、manifest 与具体组件构造；operation、transport、protocol、event 组件也混在公共文件中。
- 通用 command coordinator / runtime generation 识别 Telegram 配置类型及 `TelegramCommandCatalog`，仍有 surface 与 ingress platform 的硬编码 switch。

本设计承接三个已完成但尚未归档的变更：`support-bridge-multi-installation-pairs`、`fix-bridge-conversation-scoped-message-mappings`、`complete-bot-component-migration`。主 specs 的 live-bot wrapper、单 pair、旧 mapping 内容不是实现基线；前序配置 delta 中“遗漏字段使用默认值”的场景也已被后续显式配置实现取代。实施时先同步前序规格并纠正这处冲突，不借本次重构恢复旧行为。

读者/消费者包括 core runtime、OneBot/Telegram 模块、Bridge、Chat LLM、Message Store、actor template、安装 SDK 用户及离线打包流程。这里只生成规划，不执行实现或同步主 specs。

## Goals / Non-Goals

**Goals:**

- OneBot-only actor 无需包含 Telegram DTO；公共 SDK 不枚举具体平台/action。
- 新平台注册或既有平台增加经过审查的 action，不需修改通用 dispatcher、config loader、installation 生命周期或无关平台契约。
- 保留当前 action/surface 字符串、payload/result JSON 语义、exact installation 和 conversation identity。
- 强类型调用仍有编译期 request/result 配对，运行时仍有不可信输入验证；不把操作权限等同于一个任意 JSON 方法名。
- 配置 schema 与 typed config、recipe、operation handler、平台命令/事件适配由各平台拥有。
- 保留 current recipe、显式配置、媒体限制、错误脱敏、提交安全性、reload 及 shutdown 行为。
- 安装/编译隔离、假模块扩展、旧 DSO 拒绝和离线部署都有可执行验收。

**Non-Goals:**

- 不实现第三个平台，不增加第 14 个生产 action，不删除或更名当前 action。
- 不支持从 TOML 指定任意 component 类名、provider 方法、动态插件路径或 action 列表。
- 不建设不可信插件沙箱、动态平台 DSO 协议、网络 RPC、通用消息模型、outbox、blob gateway 或自动补偿。
- 不迁移 Bridge schema 3 或 Message Store 存储/事件，不改变 pair/会话映射、重试和命令传播规则。
- 不新增配置默认值；本次不重新设计既有 timeout/limit 策略或 worker 预算。
- 不要求把每个平台发布为独立共享库；要求源码/头文件/target 依赖可隔离，并允许组合到现有 runtime 产物。

## Decisions

### 1. 契约按所有权拆分，不再依赖全平台 umbrella

目标布局（名称可在同职责范围内微调）：

```text
include/core/bot/                       # 安装的公共 Bot SDK
  ids.hpp                              # SurfaceId / ActionId
  references.hpp                       # installation / group / message
  operation_error.hpp
  operation_result.hpp
  operation_gateway.hpp                # 不包含平台 DTO
  typed_operation.hpp                  # request/result traits + invoke
  messaging.hpp                        # 当前 common send/delete

include/onebot11/bot/                   # 安装的 OneBot SDK
  actions.hpp  types.hpp  operations.hpp  client.hpp
include/telegram/bot/                   # 安装的 Telegram SDK
  actions.hpp  types.hpp  operations.hpp  client.hpp

src/core/bot/                          # 通用运行时，不安装给 actor
  platform_catalog.*  operation_dispatcher.*
  installation_directory.*  component_runtime.*
src/onebot11/bot/                      # 平台私有实现/配置/recipe
  configuration.*  recipe.*  operations.*  components.*
src/telegram/bot/
  configuration.*  recipe.*  operations.*  components.*
src/app/builtin_bot_platforms.*        # 唯一内置模块组合入口
```

每个平台 SDK 仅依赖公共 SDK；公共 SDK/通用 runtime 不依赖任一平台 SDK 或实现。`TelegramTopicTarget`、entity/media、topic/edit 请求与结果移至 Telegram namespace；OneBot lookup/poke 归 OneBot namespace。通用 `message.send_group` / `message.delete` 保留当前 message segment 与引用语义，不扩大为新的可移植消息模型。

`BotOperationErrorCode`、`SubmissionSafety` 和 `BotOperationResult<T>` 是跨平台失败语义，可以继续是公共封闭类型；平台错误通过已脱敏的 provider code 扩展，而不是添加 provider-specific 公共枚举。平台凭据识别/脱敏规则放在相应 parser，公共错误校验只处理通用安全规则；保留现有防泄漏测试。

选择此方案而非只移动文件：只将原类型改放子目录、同时保留一个客户端包含所有平台，不会消除传递编译依赖。也不采用重新引入 `IQQBot` / `ITelegramBot` live-object 接口的方式。

### 2. 标识开放，生产支持集合仍显式封闭

`SurfaceId` / `ActionId` 为无隐式默认值、拥有自身字符串的轻量值类型；只能从显式稳定 ID 构造。语法验证复用稳定 ID 的小写字母、数字、点、下划线和连字符规则及 128 字节上限，不做大小写转换、别名推导或平台注册查询。

公共层只判断 ID 是否具有合法格式；是否受支持由不可变平台 catalog、recipe 与当前 endpoint 判断。平台 SDK 自己声明 surface/action 常量和每个请求的 operation traits。保留所有现有 wire ID，尤其以源码中的 `message.send_group`、`telegram.message.send_topic`、`telegram.media.send_photo` 等为准，不采用此前解释中误写的字符串。

区别如下：

```text
SurfaceId("test.echo") 语法有效
  != 生产配置支持 test.echo

ActionId("telegram.media.send_photo") 语法有效
  != 任意 installation 可执行此操作
```

删除全局 `action_ids::all`、依赖 enum 序号的反序列化和 `action_supports_surface()`。每个平台提供自己的 manifest；集成测试对当前两个生产模块求并集，仍断言恰好 13 个 action，但这个断言不成为通用 runtime 的注册上限。

### 3. 一个通用网关 + 平台强类型适配，不再扩充虚函数表

公共服务替换为 `BotOperationGateway`。其固定接口只包含 exact-installation support query 和一次通用 `invoke`；不会随着平台 action 增长增加虚函数。

概念数据结构：

```text
OperationEnvelope
  installation: BotInstallationRef
  action: ActionId
  payload: SDK request value JSON

OperationReply = BotOperationResult<Json>
```

这里的 JSON 是 SDK 自己的 DTO 编码，不是 provider 原始 response，也不是任意 URL/method。平台客户端（函数或薄 facade）使用 `OperationTraits<Request>` 指定 action、result 类型和请求编码，通过公共 `invoke<Request>()` 适配，向 Actor 返回 `BotOperationResult<Result>`。没有平台虚函数、全平台 variant 或必须包含所有请求的 traits 特化总表。

公共 DTO 的持久化/交换 JSON 及 golden fixtures 保持不变。网关 `payload` 的非字节字段沿用对应 DTO JSON；媒体 `bytes` 字段必须由独立的 gateway codec 使用同进程 `Json::binary` 表示，不使用逐字节 JSON 数值数组。encode/decode 直接转移拥有的字节缓冲，先验证请求/SDK 字节边界，再复制或交付字节；不能先生成完整数值数组再转成 binary。绕过 typed facade 的网关媒体 payload 若提供非 binary 字节字段必须在 provider I/O 前被拒绝。该内部格式不承诺 dump/parse 持久化或网络传输，不引入 blob service、缓存、旁路 provider API 或配置默认值。

如果 payload 含有 `action`、installation、group、message、reply 引用，平台解码适配器必须校验它们与 envelope 及实际 endpoint 一致，不能以 payload 的另一个路由覆盖 envelope。请求 DTO 不保存 gateway、回调、executor、live bot 或 provider 对象。

此决定已在实施时获用户批准：4 MiB 合法上传按原 DTO JSON 编码实测产生 64 MiB 字节数组；128 MiB SDK 上限对应仅数组就需 2 GiB。binary codec 消除此表示膨胀，同时保留原媒体上限。

运行路径：

```text
Actor 的 typed request
  → 平台/公共 typed invoke：编码 SDK DTO
  → BotOperationGateway：exact installation + surface 查找
  → endpoint-local action registry：找到已安装 action
  → 平台 typed adapter：解码 + validate + route binding
  → 现有 protocol/transport：执行 provider call
  → 平台 parser：验证 provider result + 脱敏 + submission safety
  → SDK result JSON
  → typed invoke：解码并验证 Result
  → Actor
```

Actor 的模板适配和 completion 仍受 generation lease / `ActorTask` / `await_asio` 管理。没有新线程或 detached gateway 工作。返回 DTO 解码失败也必须转换为 typed error；已执行的副作用不能因网关序列化失败而变成可安全重试。已知只读操作可沿用只读安全分类，缺少可信执行证据时保守返回非自动重试的 `PossiblySubmitted`。

选择类型擦除的 SDK DTO 边界而不是每平台一个必须挂到 `ActorServices` 的接口：前者保持单一服务及路由生命周期，又不让公共虚表获知全部平台类型。普通字段的 JSON 成本通过 value move 和避免多余 dump/parse 控制；媒体通过 owning binary JSON value 避免逐字节数值对象膨胀。本次不引入跨进程二进制协议或流式 ABI。

### 4. 注册式分发不等于任意 provider API

每个 process endpoint 使用平台拥有的 operation descriptor/handler 表。一个 descriptor 绑定一个稳定 action ID、request/result codec、scope validator、只读或副作用分类及组件依赖；descriptor 与可执行 handler 必须同时存在，不能只向 `supported_actions` 添加字符串。类型擦除和模板注册 helper 在通用层，但注册条目归平台。

公共 dispatcher 执行以下顺序：

1. 验证 envelope 标识及结构。
2. 查找 exact installation，比较 surface，检查 admission。
3. 查询该 endpoint 已 seal 的 action 表；不存在返回 `UnsupportedAction`。
4. 平台 adapter 解码/验证 payload 和完整目标，验证通过才进入 provider handler。
5. 按现有错误/结果政策完成恰好一次。

未知 action、错 surface、冲突引用、无效 payload 不触发 provider I/O。重复 action 注册直接失败。平台 handler 不得把未识别的 JSON `method` / `url` 交给 transport，且不得仅从 action 前缀推导权限。现有媒体 source URL 字段继续合法，它们不等于选择任意 API endpoint 的权限。

`supported_actions()` 读取实际 seal 的可执行条目，并排序去重。validation-only 使用相同 operation descriptor 定义的静态 manifest，无需构造 transport；启动测试核对实际发布结果与 manifest 一致。

### 5. 平台拥有 schema、typed plan 和 recipe

应用入口显式调用内置模块注册函数，创建不可变 `BotPlatformCatalog`，再把它注入 config loading、generation validation 和 assembler。禁止静态初始化自注册和新的全局 service locator。

catalog 按 exact `(surface, transport)` 定位模块 parser/recipe factory，并提供无副作用的 public metadata：surface、ingress platform、command detection target、声明的组件与操作能力。重复 recipe key 在读取配置前失败。一个 platform 可以注册多个 transport recipe。

通用 loader 只读公共 bot table 字段与 `connection` 的存在/表类型，随后将 connection table 交给 owning module。模块解析一次，返回 immutable、process-only 的 typed plan；其内部可以使用平台私有 variant，但不能重新建立中央跨平台 variant，也不能在后续 transport 中再次解释 raw TOML。

runtime 通过统一的 plan 接口取得 metadata、manifest、fingerprint 和构造步骤。只有 owning module 访问具体 connection config；public SDK 的 config metadata 不再导出 token-bearing provider config。Actor 可见配置视图不得通过新的 metadata/API 或 bots connection 透传取到这些私有值。私有快照/plan 可以保留 credential 做启动及指纹比较，但日志和 public metadata 不得包含它们。

实施时用户明确选择方案 A：完整配置 loader 和平台 catalog 留在进程私有层，不新增 process-config/host SDK。Actor SDK 保留配置视图，并提供显式的 actor-only 上下文构造入口，接受 Actor 配置和非秘密 installation metadata；该入口不解析 Bot connection、不能启动 provider，也不宣称完成生产连接 schema 校验。独立 Actor 测试迁移到此入口，需要完整连接校验的测试留在 core/platform 集成层。Actor 自己拥有的配置（例如 Chat LLM 的 API key）不因 Bot 连接隔离而被删除。

连接 schema 和所有现有必填字段保持不变。可选 proxy 整组可省略；启用 proxy 后仍显式给出它要求的字段，不能恢复隐式 username/password、timeout、TLS 或 transport 值。disabled bot 也解析验证其 schema，并参与 process-owned fingerprint。

fingerprint 覆盖 installation identity、surface、transport、enabled、全部规范化连接配置（含仅以 digest 参与的 credentials）及原有线程预算；不能只对 public metadata 做比较。配置或模块 recipe/manifest 变化需要 restart；actor-only reload 复用相同 catalog/gateway/installation。

### 6. OneBot 和 Telegram 的实际组合

```text
OneBot module
  websocket recipe ─┐
                    ├─ protocol + selected transport + events + operations
  http recipe ──────┘
  common: send_group/delete
  onebot: member/forward/group_file/private_file/poke

Telegram module
  http recipe
    protocol + transport + events
    media uploader + operations + command catalog
  common: send_group/delete
  telegram: topic/edit/photo/url group/upload group/file fetch
```

组件运行时继续验证 `provides` / `required`，按拓扑 prepare/start、反向 stop，executor 最后销毁。production recipe 固定包含原有组件。

Telegram uploader 是 operation 层的可选依赖：测试 recipe 可以不安装它；平台根据选定组合构造 descriptor。有 uploader 的组合为 operations 显式加入其依赖边，只有 prepare 成功后才注册 upload action；无 uploader 的组合不宣告、不注册 upload action。不能借另一个 installation 的 uploader 满足依赖，也不能只依赖组件加入顺序来保证可选能力的生命周期。

### 7. 去掉 command / ingress 中隐藏的平台 switch

把 Telegram/OneBot 的 command detection 实现放入对应模块，由 catalog 根据配置提供绑定后的、无凭据 `ICommandPlatformAdapter`。通用 command coordinator 不 `get_if<TelegramHttpConnectionConfig>`，也不解析 bot username；模块给出已验证的 command target metadata。

用通用 process-only `CommandCatalogPublisher` 接口替换 `ICommandPlatformAdapter::publish_catalog(TelegramCommandCatalog*)` 的具体平台参数。Telegram 模块通过原有 command catalog component 实现发布，OneBot 明确不提供 publisher；通用 installation directory 只保存 generic endpoint、event/command binding，不出现 `telegram_command_catalog()` 特例。

`qq` / `telegram` 仍作为现有 ingress 和 command route platform 字符串保留，由模块 metadata 明确映射到 surface，不能再把“不是 Telegram”作为 OneBot 分支。actor contract 的 expected bot types 改用 exact surface ID；schema 2 不接受旧 `qq` / `onebot` / `telegram` 作为 surface 别名，但现有 TOML command route 的 platform 字段不变。

命令 aggregate、exact bot scope、target username、RE2 normalization、continue/consume、catalog 的 activation-only publication、重试与 shutdown 语义均不变化。

### 8. SDK 兼容性采用明确版本门禁

新的 `SurfaceId` 值布局、config metadata、gateway 服务类型与旧 C++ SDK 不兼容。保持 actor scheduler 的 numeric ABI generation 2 和 `IActorV2` 调用模型，但将生成的 actor input contract `schema_version` 从 1 提升到 2，作为本轮 SDK 契约硬切换门禁：

- 新 loader 在 factory / generation preparation / scheduler registration 前只接受 schema 2。
- schema 1 或未知版本被拒绝，诊断明确要求使用配套 SDK 重建。
- 旧 loader 会拒绝 schema 2；不保留“旧 client 类名 + 新虚表”的伪兼容路径。
- 保留全部现有 accepted inputs、命令、scalar/collection/identity constraints 及 preparation hook 行为；新增门禁不能丢弃前序 actor contract 字段。
- schema 2 的语法解析只验证 exact surface ID 格式；支持性通过注入 catalog 和实际安装 metadata 在 generation validation 中验证，不在 actor loader 内再建平台 enum。
- 所有 actor，包括不执行 Bot 操作的 Message Store/template/fixtures，都配套重建。可选 preparation 符号的兼容性只适用于通过 schema 2 门禁的库，不承诺继续加载 schema 1 二进制。

不声称 JSON 的格式稳定就代表 C++ ABI 稳定，也不把 schema 2 宣称为跨任意编译器的二进制保证。继续遵循当前 C++26/reflection/toolchain 和构建元数据要求。

### 9. 验收要证明独立性，而不是只扫目录

CMake 导出公共、OneBot、Telegram 三个可独立选择的 SDK interface target，平台 target 只依赖公共 target；runtime dependency 不反向暴露到 public include closure。生产 application 明确链接两个模块，测试可以只链接其中之一或独立假模块，不新增带默认值的 feature toggle。

验收分层：

- 公共 SDK 编译 fixture：include 路径中不存在任一平台契约/实现。
- OneBot-only / Telegram-only SDK fixture：不安装另一平台契约，能编码、调用 fake gateway 和解码结果。
- 通用 runtime fixture：不链接生产平台，只显式注册独立 translation unit 中的 `test.echo` 模块；验证 parse、describe、assemble、dispatch、stop 全流程，无核心源码修改。
- 生产 catalog 单独断言三个 recipe、两个 exact surface、13 个 action，测试模块不得进入生产 catalog 或 actor 业务测试。
- 每个生产 action 在平台测试中覆盖 codec、路由绑定、成功/错误 response 和 submission safety；上传开/关、fake malformed gateway result、media limits 均覆盖。
- 继续运行 current lifecycle/concurrency/reload、command tests 与独立 Bridge/Chat LLM actor suite。
- 用全新 install prefix、离线恢复的 actor sources 和旧 SDK fixture 测试，而不是从开发机旧安装目录误取头文件/DSO。

## Risks / Trade-offs

- [开放 ID 使未知操作不再由 enum 编译失败] → request/result traits 保留编译期契约，endpoint-local allowlist 和双端验证在 I/O 前拒绝未知调用；生产矩阵保持 13 个 action。
- [类型擦除增加 DTO 编解码/媒体内存开销] → 独立 gateway codec 以 `Json::binary` 转移媒体字节，禁止先构造数值数组或 dump/parse；测试缓冲所有权、边界及代表性媒体路径，保持公共 JSON/golden 不变且不引入无界缓存。
- [网关承载 JSON 被误用为 provider passthrough] → 只注册 SDK request/result codec，拒绝 routing/action 冲突；provider parser 必须留在 owning module，不记录 payload。
- [拆分后仍由公共 config/command 反向包含平台类型] → include closure、独立 target 和假模块测试共同约束，不能靠改目录通过验收。
- [manifest 与实际 handler 注册漂移] → 从同一 operation descriptors 生成静态描述及 live handler 表，启动/conformance 核对一致性。
- [可选 uploader 缺少依赖边或生命周期租约] → 平台 recipe 为实际安装组合显式声明依赖，保留 capability/endpoint/executor 到完成或取消。
- [旧 actor DSO 被 ABI 2 名称误判为兼容] → schema 2 双向加载门禁、旧 fixture factory 未调用断言及配套 SDK/actor 发布。
- [前序 specs 未同步导致覆盖多 pair/schema 3 行为] → 实施第一阶段同步并检查有效基线；仅更新本提案触及的 requirement，保持 conversation/persistence 规格不变。
- [离线 bundles/pins 与本地 actor HEAD 不一致] → 更新完整可重放 bundle/patch/pin 链并从临时空目录验证，不将本地成功当作发布验证。
- [移除默认值的规格与旧 delta 冲突] → 明确以当前显式字段实现为基线，补足必填字段测试；不得补文档默认值或悄悄改变选项。

## Migration Plan

1. 同步前序已完成 specs，记录当前三个 recipe、13 个 wire ID、payload fixtures、schema 3 / multi-pair 行为以及显式配置要求。
2. 建立新的公共 ID/result/gateway/typed helper 和平台 SDK 契约；先用独立编译与 fake gateway 测试固定边界。
3. 移动平台 endpoint/parser/components，完成 action registry 和模块 catalog；连接 parser 返回平台 typed plan，替换中央 variant。
4. 切换 application、command、ingress、reload、fingerprint 到统一 metadata/catalog，保持生产 TOML 无变更。
5. 切换所有 actor 和 fixtures 到新 gateway，启用 schema 2 门禁；删除旧 umbrella、平台枚举、客户端重载、兼容 aliases 和全局矩阵。
6. 更新 installed SDK targets、actor metadata/version constraints、registry、离线 bundle/patch/pins 及文档；执行全套测试和干净安装/离线恢复验收。
7. 部署前用新 binary 和配套 actor 执行 `--validate-config`；停止进程后整体替换 core/SDK-derived actors 并重启，不通过 actor reload 混用两代 SDK。
8. 回滚必须整体恢复之前 core 和全部 actor 产物；当前 TOML、wire ID 和 schema 3 数据库不因本次重构改变，无本次专属数据库逆迁移。

## Open Questions

无阻塞架构决策：本提案选择同进程通用 DTO gateway、平台 typed facade、显式静态模块注册及 schema 2 硬切换。未来独立平台 DSO、跨版本协议协商、细粒度权限和新增 action 需另行提案；实现阶段仅需按现有发布政策确定具体版本号/离线 pin，不得弱化兼容性门禁或引入配置默认值。
