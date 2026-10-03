# OBCX 当前 Bot 抽象差距与迁移建议

> 本文依据 2026-08-17 的只读源码审计；未运行 runtime/API integration 或 adapter conformance tests。完整 file/range register 见 [`agents/obcx-current.md`](agents/obcx-current.md#sourcefile-register)。本任务不修改代码，也不冻结 API。

## 1. 结论摘要

当前 transport 生命周期事实上已经采用正确方向：Bot、连接、`io_context`、registry 由进程持有并跨 actor generation reload 存活。但业务抽象仍由 OneBot 11/QQ 形状主导：巨型 `IBot`、opaque JSON 结果、Telegram 空 stub、RTTI capability、Bridge 直接取 live bot 指针和按平台歧义路由。

推荐迁移 seam 不是再加一个更大的 Bot 基类，而是：

```text
process adapter → typed ingress → business actors
business actors → typed operation → process EgressDispatcher
→ explicit installation route → discovered small capability → adapter
```

## 2. 当前实现与证据

### 2.1 巨型且 OneBot-shaped 的 `IBot`

`include/interfaces/bot.hpp:24-379` 同时包含：

- event subscription、`run/stop/error_notify/is_connected`；
- private/group send、delete/get；
- friend/stranger/group/member 查询；
- kick/ban/whole-ban/card/name/admin/anonymous/portrait/honor；
- login/status/version/image/record；
- cookies、CSRF、credentials；
- friend/group request approval。

大量操作返回 `asio::awaitable<std::string>`，把 OneBot 的 `status/retcode/data/echo` JSON 结果推入通用层。`set_group_anonymous_ban`、`get_group_honor_info`、`get_cookies`、`no_cache` 等名称直接泄露 QQ/OneBot 语义。[本地证据 F1、F8](agents/obcx-current.md#sourcefile-register)。

平台扩展已经存在但没有统一 discovery：

- `include/interfaces/qq_bot.hpp:8-35` 的 `IQQBot`；
- `include/interfaces/telegram_bot.hpp:14-170` 的 topic/photo/media-group/edit/commands/download 能力；
- 调用方使用 `dynamic_cast`，而不是 capability descriptor。[F2–F5](agents/obcx-current.md#sourcefile-register)。

### 2.2 Telegram 被迫伪装支持

`src/core/tg_bot.cpp:286-309,367-414,425-430,475-505` 中，`get_message`、friend/group list、group card/anonymous/honor、cookies/CSRF/credentials、friend/group request 等方法返回 `{}`。`get_status`/`get_version_info` 记录“not implemented”却返回合成成功；`can_send_image/record` 无条件返回 yes（`src/core/tg_bot.cpp:439-473`）。

这会把 unsupported 误报为 success，且违反“能力缺席而非空方法”的设计。Telegram delete 还要求 `message_id` 编码成 `chat_id:message_id`（`src/core/tg_bot.cpp:260-278`），说明通用消息引用缺少 conversation scope。[F3、F6](agents/obcx-current.md#adapter-asymmetry-and-unsupported-behavior)。

`ITelegramBot` 默认 `set_commands` 也可返回 `{}`，entity-aware overload 默认丢弃 entity 后转发（`include/interfaces/telegram_bot.hpp:134-169`）。即使当前 `TGBot` 覆盖了它们，该默认行为也不适合作为 SPI 合同。

### 2.3 Bridge 直接获取 process object

`local_actor/obcx-actor-bridge/actor/bridge_actor.cpp:104-250` 从 `ActorContext` 解析 `BotRegistry`、`DbManager`、Asio executor，并缓存 `BridgeForwardingRuntime`。`local_actor/obcx-actor-bridge/dependency/bridge_forwarding_runtime.cpp:13-112`：

1. 只接受 `qq` 与 `telegram`；
2. 使用 `registry.find_bot(platform)` 找 source/target；
3. 把 `MessageStored` 转回 `MessageEvent`；
4. 将 live `IBot&` 传给 handler；
5. 通过 `ActorContext::await_asio` 直接等待网络调用；
6. 依赖持久化 target-message mapping 执行删除/映射。

`include/core/bot_registry.hpp:15-107` 以 `(platform, bot_id)` 保存 `weak_ptr<IBot>`；仅按 platform 查找在多账号时会故意返回歧义失败，测试位于 `tests/cpp/bot_registry_test.cpp:9-58`。虽然 envelope 已有 `source_bot`，Bridge 当前路由没有把 account ID 作为强制键。[F14、F15、F20、F21](agents/obcx-current.md#bridge-acquisitioncalls)。

Bridge handler 搜索到的调用包括：

- base：`send_group_message`、`delete_message`、`get_group_member_info`；
- Telegram extension：`send_topic_message`、`send_group_photo`、`send_media_group`、`get_media_download_url(s)`；
- QQ：`get_forward_msg` 甚至 `dynamic_cast<obcx::core::QQBot&>`，未只依赖 `IQQBot`。

### 2.4 Ingress 与 envelope：可复用的迁移基础

现有链路：

```text
OneBot/Telegram connection
→ protocol adapter JSON
→ common::Event variant
→ per-bot EventDispatcher
→ app message/notice callback
→ MessageEnvelope
→ actor generation/store/Bridge
```

- `include/common/message_type.hpp:44-178` 仍使用 `post_type`、`message_type`、`MessageSegment{type,data}` 等 OneBot 词汇。
- `src/onebot11/adapter/event_converter.cpp:10-67` 将 OneBot message/notice/request/meta/heartbeat 转为 common variant。
- `src/telegram/adapter/protocol_adapter.cpp:51-173` 只识别 message、edited message、channel post、edited channel post、callback query；保留 raw Telegram JSON，但 `self_id = "0"`。
- `include/core/event_dispatcher.hpp:22-108` 做 variant handler dispatch。
- `src/app/main.cpp:384-412` 当前只把 `MessageEvent`、`NoticeEvent` 注册进 actor runtime；request/meta/heartbeat/error 留在 dispatcher 侧。
- `src/core/runtime/message_event_ingress.cpp:21-174` 已构造含 source platform/account、conversation、correlation/causation、normalized payload、raw JSON 的 envelope。
- `include/core/actor/actor.hpp:81-110,174-284` 提供 serializable envelope/result/service 与 Asio/blocking crossing。

因此应保留 `MessageEnvelope` 的 routing/correlation/causation 思路，但把主要 payload 从 OneBot 形状换成 typed `MessageCreated` 等事件；原始协议作为 `onebot11.*`/`telegram.*` extension 或受控 `raw_ref`。

### 2.5 Process ownership/reload 约束必须保留

- `src/interfaces/bot.cpp:7-57`：每个 Bot 创建 `io_context`、adapter、dispatcher；析构顺序要求先停止/排空 I/O，在 context service 存活时销毁 connection manager。
- `src/app/main.cpp:284-418,420-510`：应用强持有 bot，registry 仅弱引用；每 bot 一个 thread；actor reload 替换 generation，但 bot/registry 保持；shutdown 先停 actor/blocking executor，再停 bot。
- `src/common/component_manager.cpp:21-61,68-170,173-190`：工厂字符串硬编码 `qq`、`telegram`；配置可映射 Telegram websocket，但 `TGBot::connect` 只接受 HTTP（`src/core/tg_bot.cpp:20-39`）。

结论：actor 不应成为 connection owner。新的 egress 必须在 process 层处理 admission、取消、outbox、token、rate 和 shutdown drain；业务 actor 只持有可序列化 request/result。[F16–F18](agents/obcx-current.md#ownership-reload-and-network-lifecycle)。

## 3. 差距表

| 差距 | 当前状态 | 风险 | 目标组件 |
|---|---|---|---|
| Capability discovery | 无；靠虚函数存在/RTTI/空 stub | unsupported 像 success | D13 + R04 |
| Typed result/error | opaque JSON string、`{}` | 无法区分拒绝、限流、超时、unknown outcome | D11/D12 |
| 账号路由 | `find_bot(platform)` | 多账号歧义；source/target 错配 | D02 + R05 |
| Message identity | 裸 string；TG 使用冒号拼接 | conversation scope 丢失 | D03/D04 |
| Portable content/media | `MessageSegment{type,data}` 与 bytes/URL 泄露 | OneBot 偏置、媒体 TTL/权限丢失 | D05/D07 + K09/R11 |
| Event identity/idempotency | TG update ID 未 canonical 保留；notice ID 可为进程本地 | 重放/去重不可靠 | D10 + R07 |
| Egress actor boundary | Bridge 直接拿 `IBot&` | actor package 耦合 live transport 与 shutdown | R03 + typed actor protocol |
| Retry/outcome | 无统一模型 | timeout 后盲重试可能重复 | D12 + R08/R09 |
| Secrets | cookies/CSRF/credentials 属业务 API | secret 泄露、平台偏置 | 移出 actor；R06 |
| Ingress coverage | 仅 message/notice 进入 actor | interaction/request 等丢失或降格 | K01 + typed event inventory |
| Transport config | Telegram websocket 配置可选但实现拒绝 | 配置晚失败 | R01 capability/config validation |

## 4. 当前调用到新 capability 的映射

| 当前调用/模式 | 新 request/capability | 迁移约束 |
|---|---|---|
| `registry.find_bot(platform)` | `ResolveRoute{InstallationRef,target}` / R05 | source 取 envelope `source_bot`；target 必须配置 account；默认无歧义 fallback |
| `send_private_message` / `send_group_message` | `SendMessage{installation,ConversationRef,Content}` / K02 | destination 不再叫 group；返回 typed `MessageRef` |
| TG `send_topic_message` | K02 + `ThreadRef`，`telegram.topic.*` extension | topic 是可选 capability，不污染每个 adapter |
| `delete_message(string)` | `RemoveMessage{MessageRef}` / K04 | 消除 `chat_id:message_id` 拼接；返回 Recall/Redaction/Delete 语义 |
| TG `edit_message_text` | `EditMessage{MessageRef,Content}` / K04 | 显式权限/时间窗错误 |
| `get_message` | `GetMessage{MessageRef}` / K03 | optional；Telegram Bot API 不广告 |
| `get_group_member_info` | `GetMember{ConversationRef,PrincipalRef}` / K11 | `no_cache` 不进入领域语义；可成为 request metadata |
| kick/ban/admin/name | K10/K11/K12 的独立操作 | 不把所有 moderation 合成一个布尔 capability |
| anonymous/honor/poke/forward/file URL | `onebot11.*` / `qq.*` extension | 不作为腾讯官方或 common 证据 |
| TG photo/media group/upload/entities | K02/K07/K09 + `telegram.*` | 保留 file ID、UTF-16 entity、album 等扩展约束 |
| TG media download | `DownloadMedia{MediaHandle}` / K09，经 R11 | actor 不拿 connection manager/任意 URL |
| `set_commands` | `PublishCommands` / K16 | 无默认 `{}`；未支持则 capability 缺席 |
| `get_status/is_connected` | process observability / `EndpointStateSnapshot` | 不作为普通业务 actor 方法 |
| cookies/CSRF/credentials | 无业务 capability；R06 | 删除 actor-visible surface |
| handler `dynamic_cast` | R04 discovery + typed capability client | cast failure 替换为明确 capability state |
| `on_event<T>` callback | K01 → typed `IngressEvent<T>` | 保留 provider event/update ID 与 delivery metadata |
| Bridge `await_asio` 调 live bot | `OperationRequest<T>` → R03 → `OperationResult<T>` | Bridge 不再缓存 bot pointer；由 process egress 执行 |

## 5. 迁移阶段

### Phase 0：基线与 conformance（不改语义）

- 为现有 QQ/TG 实际调用建立 success/permission/error/timeout/reconnect 测试；确认哪些 Bridge legacy 路径实际激活。
- 固化当前 message mapping、source bot/account、Telegram callback/update ID 与 shutdown 行为的观察结果。
- 配置验证时拒绝未实现的 Telegram websocket，而不是 setup 时抛错。

### Phase 1：并行引入领域模型与 discovery

- 加入 Bridge MVP 的 D01–D07、D10、D11、D13；不把网络方法放进 DTO。
- R04 从 concrete adapter 的真实实现生成临时 descriptor；`{}` 不能视为 capability。
- `qq.official` 与 `onebot11.qq` 使用不同 installation/pack；当前 QQ adapter 先标成 `onebot11.qq`。

### Phase 2：建立 process-owned ingress/egress seam

- 由 R02/K01 把现有 callback 转为 typed `MessageCreated`/`MessageRemoved`，继续保留受控 raw。
- 建立 R03 `EgressDispatcher`/`BotEgressActor`：actor 发送 serializable request，进程服务执行 K02/K04/K09。
- R05 按 `InstallationRef` 路由；source 从 envelope，target 从 Bridge 配置/映射；歧义直接失败。
- R07/R08 保存 ingress checkpoint、`operation_id`、provider message mapping 与 unknown outcome。

### Phase 3：迁移 OneBot 11 与 Telegram

- P10 `onebot11.*` 把 `status/retcode/data/echo` 解码成 D11；原 envelope 仅作诊断。
- P09 `telegram.*` 使用复合 `MessageRef`，停止冒号编码；仅注册 Bot API 真实支持的方法。
- Bridge 当前 `send_group_message`/delete/media 路径切换到 K02/K04/K09；先保留旧 façade 供未迁移 caller 使用。
- QQ forward/node/face/poke/honor/anonymous/cookies 等留在 P10；Telegram topic/entity/file ID 留 P09。

### Phase 4：隔离并移除 legacy `IBot`

- 禁止新 actor 通过 `ActorContext::get_service<BotRegistry>()` 获得 live bot。
- 逐调用迁移后，让旧 `IBot` 成为 process 内兼容 façade；删除 Telegram 空/合成实现。
- 将 cookies/CSRF/credentials 完全移入 R06；移除 concrete `QQBot` cast。
- 为每个 advertised Kxx 增加 conformance：成功、权限失败、discovery 缺席、timeout/unknown、multi-account route、shutdown cancellation。

## 6. Bridge MVP 的具体范围

Bridge 首期只需要[组件建议 §11](component-proposal.md#11-bridge-mvp精确-26-个组件)的 26 个组件：10 core、K01/K02/K04/K09、R01–R09/R11、`telegram.*` 与 `onebot11.*`。不做 history fallback、好友/联系人、群 honor/anonymous、command/poll/reaction/card/payment 或官方 QQ 主动发送扩展。

## 7. 未验证项

- `RawMessageEvent` 到 `MessageStored` 的完整 pipeline/type 命名未穷尽追踪。
- Telegram callback query 到 actor payload 的确切映射未完整枚举。
- Bridge legacy 文件可能包含不再活跃的调用路径。
- 未找到覆盖所有 QQ/TG 操作、错误 schema、timeout、reconnect 的完整 conformance suite。
- 未确认仓库其他位置是否已有 target account selection；所审计 runtime 使用 platform-only lookup。
- Telegram canonical `self_id` 的正确 provider identity 仍未知。
- shutdown admission/cancellation、rate、retry-after 与 idempotency 尚未运行验证。

这些未知项要求在 Phase 0/实施时解决，不影响“业务 actor 不持有 transport、OneBot 退回 adapter”的方向。
