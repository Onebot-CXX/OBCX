## Why

当前 SDK 统一了调用网关，但普通业务仍需理解 QQ 的 CQ 段和 Telegram 的 entities、file_id、topic 等原生字段；同一个发送接口还可能产生丢图、丢提及的不同结果。需要把公共业务语义收敛为直接可调用的强类型结构体，将平台适配留在底层，并把正在实现的统一 self 查询作为同一公共 API 的组成部分。

## What Changes

- **BREAKING**：以 `BotClient`、强类型消息/事件和公共查询替代普通业务对 `common::MessageSegment{type, json}`、原始事件 JSON 和平台客户端的依赖；保留独立平台扩展，不建立全平台方法虚表或任意 provider API 透传。
- **BREAKING**：统一群聊、私聊和话题的 `ConversationRef{installation, id}` 与 `MessageRef`。话题等完整寻址信息编码进不透明 `ConversationId`，公共结构体不暴露 `thread/topic_id/native_group_id`；业务只传递引用，内部 codec 负责版本、规范化和无歧义组合。
- 定义有序文本/显式样式、提及、图片/动画/视频/音频/文件、显式 caption 和统一媒体来源。发送显式选择单消息或允许拆分；禁止静默降级，完整返回回执、输入/输出关联和可用媒体引用，保留部分成功/不确定提交证据。接收侧提供 typed 相册证据与稳定媒体缓存身份，统一群聊与私聊删除。发送可显式携带 attempt correlation，保证物理 confirmation 能匹配 actor 的预先落库准入，而不是返回后猜测归属；会产生消息的 typed 专有扩展复用相同 outcome-aware binding/通知，不把包装结果当作无条件成功。
- 整合 `GetSelfRequest` / `SelfInfo` / `bot.self.get` 到 `BotClient::get_self`，保留当前 installation 所有的缓存、cache-only 读取、刷新合并和账号代次隔离；返回身份与消息中的 `UserRef` 同型。增加公共成员基础视图、受限引用解析、媒体读取和强类型能力查询。引用证据 store 的容量/TTL 为每 installation 的必填共同配置、无默认值；配置/历史引用通过纯读取 ReferenceResolver 转换，不能借恢复对象获得 provider lookup 权限。
- **BREAKING**：迁移接收事件和 SDK 消费者，发布新的 actor contract schema 门禁；以 [actor-migration.md](actor-migration.md) 逐 actor 规定代码入口、富媒体路径、持久化与验收，不只替换 facade。保留 command ACL、Bridge pair、存储原始证据和 Chat LLM 去重/关系标记；Bridge 完整多输出映射需显式数据库升级，self 不承担本人判定或回复策略。

- **范围追加（用户明确确认“全部移植”）**：完整 OneBot11 标准纳入公开 typed QQ SDK，不仅限当前 actor 使用的消息能力；覆盖全部标准消息段、38 个公开 API、hidden quick operation、异步/限速调用形式，以及全部标准事件。包括群管理、审批、凭据和服务维护；仅实现/隔离验证，不实际执行管理操作或读取真实凭据。先完成标准实现，所有非标准扩展新增/改造暂停，已有扩展保留；标准完成后才询问非标准处理。

## Capabilities

### New Capabilities

- `bot-portable-messaging`: 强类型消息内容、媒体来源、显式拆分、统一删除及保留部分回执的结果。
- `bot-portable-events`: 平台归一化的消息、发送者、提及、附件和真实引用证据，以及历史不可回查时的明确未知状态。
- `bot-portable-queries`: 同一客户端上的缓存 self 查询、基础成员查询、受限引用补全、媒体读取和数据型能力描述。

- `onebot11-standard-sdk`: 完整 OneBot11 标准的 typed QQ 接口、消息内容、事件、查询/管理/服务能力及真实的完成/待执行/不确定语义；普通共同客户端的既有边界保持不变。

### Modified Capabilities

- `qq-telegram-bot-contract`: 不透明完整会话身份、统一用户/消息引用和新的公共操作/DTO；替代旧的分平台目标、原生消息段和单条结果约束。
- `bot-operation-dispatch`: 公共类型化 facade、平台拥有的 codec/执行计划、精确作用域检查、能力验证和拆分发送的安全完成语义。
- `actor-abi-v2`: 新 SDK 契约门禁及安装包配套迁移，禁止旧 DSO 因调度 ABI 仍为 V2 而被误接纳。

## Impact

- 核心与平台：`include/core/bot/`、`src/core/bot/`、`include/common/message_type.hpp`、`src/core/runtime/message_event_ingress.cpp`、QQ/TG adapter、operations、recipes，以及 actor service 和 SDK 导出。
- 消费者：command runtime、五个顶层 `local_actor` 和 ExHentai 的 `tools/qq-forward-probe-actor`；具体方案见附件 §3–7。Bridge 的双向富媒体/相册/forward、Chat LLM 图片工具、ExHentai direct/forward 及共享文件 lease 都有独立迁移项；真正平台专有调用保留 typed 扩展，actor 业务规格仍属独立仓库。
- 媒体与数据：Bridge QQ→TG URL-first 和 Chat LLM QQ URL-only 建议改为显式读字节，新增预算必须明填；共享挂载由调用方保留现有映射/配置/lease，SDK 接收最终 `LocalFile.path` 字符串并做原生适配；不限定 OBCX-host、不映射、不要求预注册或额外路径授权配置，`obcx.path-mapping` 留在 core/SDK 外。旧 source/session/去重身份保留；Bridge v3 的单输出映射建议升级至多输出 ledger（v4），不猜历史缺失回执。全部变化只在副本验收。
- SDK 打包：完整 SDK 包含配套 core，公共/平台 target 自动传递其链接依赖；不要求平台 SDK 脱离 core 独立使用，不新增平台实现库或运行时平台插件。保留公共与平台接口头的边界和私有实现头不安装的约束。
- 发布：匹配 core/SDK/六个 actor 构建、当前 packages.toml/registry 元数据和回滚资料配套更新；不恢复已退休的旧 offline bundles。用户已批准实现与隔离验证，actor 用冻结 SDK 和 `scripts/actor_build.py`；不部署、不改在线数据/用户 prompt、不写新单元测试或新增配置默认值，保留已批准的 self 刷新政策。core schema 3 与 actor 数据库版本分开，在线迁移及 core 替换/重启须另行授权。
- 前序依赖：以 `decouple-bot-platform-contracts` 的模块化实现和 `auto-detect-chat-llm-addressing` 的 self cache 为基线，不视其未勾选任务为已完成。本变更不复制另一套 cache，不修改前序文件；实施时明确先后关系并同步已批准的有效规格，避免旧规格恢复原生段/平台分支。
