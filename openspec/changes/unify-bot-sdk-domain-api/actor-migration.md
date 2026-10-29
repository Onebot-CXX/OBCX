# local_actor 迁移方案

先看 §3 的 Bridge 富媒体与回执方案，再看其余 actor。本文是 `design.md` 的实施附件，**已获用户批准开始并行实施，尚未完成真实平台验收**。实际进度见 [implementation-status.md](implementation-status.md)；schema 代码开发不等于授权在线迁移。

## 1. 范围与归属

本次按当前工作树盘点，不以 actor 名称推测调用方式：

| 独立仓库 | 主要迁移面 | 必须保留 |
| --- | --- | --- |
| `local_actor/obcx-message-bridge` | 双向事件、富媒体、引用、相册、回执/重试和持久映射 | pair 隔离、展示/转换策略、全部确认输出、未知提交不重放 |
| `local_actor/obcx-message-store` | 归档输入与 `MessageStored` 的 typed 投影 | 原始证据、source key、更新/重复判断、通知关联信息 |
| `local_actor/chat_llm` | source reader、addressing、图片工具、成员工具和发送 | session/sidecar/image_id/发送去重、授权、prompt 与响应策略 |
| `local_actor/obcx-exhentai-fetch` | 普通发送、topic、QQ 共享媒体及合并转发 | 审查/lease、批次状态、直接发送的逐条结果与继续策略 |
| `local_actor/obcx-actor-template` | 导出契约、package/SDK 依赖及示例 | 既有同步、协程、命令完成与 propagation 示范 |

ExHentai 仓库另有 `tools/qq-forward-probe-actor`，是第六个需重编的 actor，见 §6.4。`local_actor/actor-registry/entries/` 是发布元数据，不是另外两个待迁移实现；匹配版本的 entries 随发布更新。

依据 `openspec/ownership-migration-2026-09-30.md`，本附件记录 core SDK 的消费者迁移依赖和联合验收。actor 的业务规格、数据库迁移脚本和实现任务仍归各独立仓库；实施前在那里承接相应任务，不把 actor 业务规格搬回 core，也不添加 submodule。本轮不修改这些仓库。

## 2. 迁移前必须补齐的 SDK 契约

不能只把 `MessagingClient` 换成 `BotClient`。静态盘点发现原草案还需要以下五组契约补充；公共客户端仍是原来的七个方法，不增加公共 `thread` 或原生 JSON 入口。

### 2.1 运行时身份与持久身份分层

配置/legacy adapter 输出完整 `ConversationRef`；业务不再 `substr("chat:")`、`stoll(message_id)` 或拼 `topic_id`。旧 source/ACL/session/destination key 留在兼容存储边界，不用不透明 ID 替换既有唯一键。迁移 codec 必须离线工作，不能为恢复历史行逐条查询 provider。

### 2.2 动画、文本样式与接收媒体元数据

- 公共内容补充 `Animation`，不能把 GIF/WebM 动画静默当静态 `Image`。`Text` 的显式样式至少覆盖 Bridge 现有发送者斜体；adapter 处理 entity offsets，能力不支持时明确拒绝。actor 可按自己既有展示策略预先选择无样式文本，SDK 不自动剥离。
- 接收事件增加可选 `MediaGroupRef`，包含完整会话作用域及不透明组 ID。它只是聚合证据，不表示相册完整，不提供猜测的成员数；每条源 `MessageRef` 独立保留。
- 接收媒体提供可选不透明 `MediaIdentity` 作为稳定缓存键；与可用于读取/发送的 `MediaRef` 分开，按 installation 隔离。TG `file_unique_id` 由 adapter 转成该值，缺失时不拿下载 URL 或 `file_id` 冒充稳定身份。
- Sticker、video-note、QQ 合并转发、卡片等真正专有内容由 typed 平台扩展视图描述，引用共用 `MediaRef/MessageRef`。普通消费者看到明确的 `UnsupportedContent`；Bridge 只在这些专有转换插件导入平台 SDK，不在普通图片/音频路径解析原始 JSON。

### 2.3 回执不只有第一条 ID

在保留 `SendReceipt.messages` 和 `SendFailure.confirmed_messages` 的同时，为确认输出增加 `output_details`：每项关联一个确认 `MessageRef`、对应输入内容块/片段，以及 provider 确实返回的可复用 `MediaRef`。无可复用媒体时显式缺失，禁止用响应顺序猜造 `file_id`。结果/confirmation 通知的 codec 保留这些信息。

输出与输入的对应关系由 adapter 的执行计划及已验证结果给出；不能让 actor 假设“一块内容总是一条消息”。畸形 batch 中已确认 ID 但来源关联不明时，保留 ID 并明确标记未知关联；禁止为了凑一一对应而丢回执或猜输入块。Bridge 记录输入块来自哪条源消息，再将这个关系与输出 details 合成多对多映射。普通只发文字的 actor 不必使用 details。

### 2.4 媒体读取与显式转换分层

公共跨 installation 路径固定为 `read_media → owned bytes → send`。QQ native image 的 `get_image`、TG `getFile` 和认证下载留在来源 adapter；限额、重定向策略和总 deadline 同时生效。旧消息附带 URL、远端绝对路径不是失败后的后门。

图片归一化、占位图、WebM 转 GIF、贴纸展示和图库审查继续由 actor 的显式策略决定；SDK 只做格式/能力校验与传输编码。actor 先完成所选转换，再提交对应 typed body；结果标明替换/调整，不能把有损处理藏进 SDK 的“成功”。下载过但未发送也不是提交成功。

### 2.5 最终路径由调用方提供

Bridge 和 ExHentai 保留既有 host/container 路径映射与配置，通过调用方使用的外置 `obcx.path-mapping` 库得到最终路径字符串，再交给 `LocalFile.path`。SDK/core 不映射、不推断文件系统、不要求 OBCX-host 可见，也不增加路径授权配置或共享文件预注册。adapter 负责原生 file URI 等协议编码，不要求 actor 拼 CQ JSON。

actor 保留文件 owner、lease、磁盘限额、访问/挂载、重启恢复和清理责任，覆盖确定及可能提交的完整生命周期。SDK 只对实际执行的读取、缓冲和已知大小施加显式边界；provider-only 路径不得因 OBCX 本机 stat 失败而被拒绝，未知大小不得当零或宣称已验证。

普通 typed 媒体和 QQ typed forward nodes 使用其原生能力支持的来源。原生不支持时明确拒绝，不偷偷省略图片或换上传路径；不能把缺失 SDK mapping/registration installer 当作迁移阻塞。此条按最新用户确认替代先前 host-only/注册方案。

## 3. obcx-message-bridge：按发送路径迁移

### 3.1 修改入口与责任划分

| 当前入口 | 迁移后的责任 |
| --- | --- |
| `actor/bridge_actor.cpp`、`src/bridge_forwarding_runtime.cpp`、`src/bridge_message_event_adapter.cpp` | `MessageStored` 的 typed 投影 → pair/目标引用 → 转发计划；去掉普通消息的 raw JSON 再解析 |
| `src/bridge_bot_operations.cpp` | 普通 send/delete/member/media 委托 `BotClient`；保留窄的 QQ forward、TG sticker/edit 等扩展 binding，不返回单个裸 message ID |
| `src/qq/handler.cpp`、`src/qq/message_formatter.cpp`、`src/qq/media_processor/` | 展示与源内容转换；不再构造 Telegram entities/multipart 或普通 CQ 段 |
| `src/telegram/handler.cpp`、`src/telegram/media_group_buffer.cpp`、`src/telegram/media_processor/` | typed 内容/组证据、转换/聚合/去重；不再读 `event.data.file_id/media_group_id` |
| `src/bridge_state_repository.cpp`、`src/retry_queue_manager.cpp`、`src/path_manager.cpp` | 完整多输出 ledger、版本化重试 payload、调用方最终路径与 lease；保留 exact scope 和 blocking executor 边界 |

目标数据流：

```text
源事件 + 原始归档证据
        ↓ Message Store 成功通知（一次业务准入）
MessageStored {旧存储身份, typed MessageReceived}
        ↓ exact pair / 完整目标 ConversationRef
Bridge 展示、聚合、媒体转换、引用映射
        ↓ MessageBody + 显式 policy/limits
BotClient / 窄的 typed 平台扩展
        ↓ SendOutcome + output_details
持久化所有确认输出 / 未知状态 → 逻辑成功或失败通知
```

RawMessage/heartbeat 和物理发送确认可继续刷新安装活跃度，但不得绕过 `MessageStored` 再转发同一来源。command processed header、correlation/causation、未匹配路由 no-op 及未知安装 fail-closed 保留。

### 3.2 QQ → Telegram：文字、提及与卡片

| 现有情况 | 迁移方式 | 不能丢掉的语义 |
| --- | --- | --- |
| 普通文本、发送者前缀、topic 目标 | `RichText/Text`；前缀斜体用显式样式；配置边界给出完整目标引用 | `show_qq_to_tg_sender` 等既有开关与 topic 覆盖规则，不在 actor 构造 UTF-16 entity |
| `at` 与 `at-all` | 来源为 typed mention；普通成员显示名用 `get_member` 和原有作用域缓存 | QQ `UserRef` 不能直接变成 TG ping；没有验证过的跨平台用户映射时，按 Bridge 原策略输出显示文本，全体提及同理；映射成立也须重建为目标安装的 UserRef |
| `face`、`mface` | QQ 扩展提供表情描述/媒体引用；普通 face 保留文本映射，mface 按实际静态/动画生成 `Image/Animation` | 表情缓存、文本替代和动画转换仍由 Bridge 决定，不假装所有表情都可无损跨平台 |
| `json/app/ark/miniapp` 与分享内容 | typed QQ 卡片视图 → actor 现有标题/描述/链接展示 | `enable_miniapp_parsing` 及文本 fallback 保留；JSON 解析移到专有 adapter，不暴露公共任意 payload |

### 3.3 QQ → Telegram：单图、多图与图片恢复

**明确行为差异：本方案建议把当前“QQ URL 先交 TG、明确拒绝后再下载 multipart”改为“QQ `read_media` 后上传拥有的字节”。** 这是贯彻显式跨安装复制的代价，不宣称只换 API 而网络路径完全不变。相应带宽、内存和 deadline 需要离线/授权环境验收；若必须保留 URL-first，需另行设计经授权的 locator-transfer 扩展，不能在公共 API 偷放签名 URL。

| 场景 | 迁移方式 | 边界 |
| --- | --- | --- |
| 单图 | 来源 `MediaRef` → 有界读取 → `Image{BytesSource, caption}`；目标 adapter 上传 | QQ native `file` 解析由 adapter 做；过期/仅远端路径明确失败，不回退原事件 URL |
| 多图与相册输出 | Bridge 先明确排列内容和 caption，再 `AllowSplit`；TG adapter 选择单图/兼容媒体组 | 1 图不调用 media group；2–10 图一组；11 图保留 9+2 等合法分组，绝不丢尾图；预算按实际输出消息数算 |
| 图片下载/尺寸异常 | 保留逐项占位、尺寸检查、缩放/补边及“已调整/已替换”统计；准备后再发送 | 好图保留，坏图位置保留；不裁剪、不无界解码，blocking executor、当前显式配置/既有安全边界保留 |
| 已有目标贴纸缓存 | 命中且属于目标安装时，typed sticker 扩展复用目标 `MediaRef`；未命中走读取/转换 | 不把 TG file ID 当字符串或跨账号复用；失效仅在确定未提交后有界回源 |

Bridge 现有 formatter 会把其他文字组成相册 caption，并按批次添加发送者/恢复说明。这属于 **Bridge 显式构造目标 body 的展示策略**，迁移时保留其配置与已选布局；普通 SDK 不得把任意前后文本自动搬到 caption。交错媒体不能在 adapter 中为了凑相册而重排。长 caption 必须在发送前明确规划，超限拒绝或按 actor 选定策略产生独立文字，不能截断。

现有单图、多图、forward-node 图片共用恢复路径，迁移后也只保留一套 actor 图片准备器；core 负责下载和上传，不复制 Bridge 的占位/归一化逻辑。URL/multipart 的 provider 判错不再散在 actor 中。任何替代发送只允许发生在有明确未提交证据时；超时、取消或可能提交不得换一种表示再发。

### 3.4 QQ → Telegram：音视频、文件与合并转发

| 内容 | 迁移方式 | 保留/限制 |
| --- | --- | --- |
| `record` | typed `Audio{Voice}`，来源读取后由目标 adapter 发 voice | 编码不兼容时按 Bridge 明确策略转码/文本替代；禁止 SDK 悄悄当普通音频或文件 |
| `video` | `Video` + 显式 caption + 完整目标引用 | 有界读取、准备和 native 上传；未实现 recipe 不能冒充支持 |
| `file` | `File`；QQ 直接 URL/群文件/私聊文件定位由来源 adapter/受控扩展统一验证 | 保留文件名/大小展示和既有失败文本；actor 不自行选择原生 get-file API 或用服务器路径作本机路径 |
| 合并转发 | typed QQ forward 查询返回有序节点、发送者、文本和媒体引用；Bridge 渲染节点与图片批次 | 查询仅限受支持的扩展与已取得引用，不把 `resolve_reply` 变成任意 forward/history 查询 |

合并转发必须完成以下四步：

1. 保留节点顺序、作者、节点文本及图片所在节点；未建模段明确描述，不当作纯文字成功。
2. 图片使用 §3.3 的同一准备器，保留“节点文本 + 图片批次”的现有展示意图；禁止只拼节点文本后丢图。
3. 文本、各批图片的 **所有** 物理回执进入同一逻辑 delivery ledger，不再只存主 ID。
4. 某步失败立即终止这个逻辑 delivery；保留已发部分及失败阶段。不能沿用当前部分 catch 后继续发送的路径再报告整批成功，也不能整批入重试队列。

### 3.5 Telegram → QQ：普通媒体与相册

| 内容 | 迁移方式 | 必须保留 |
| --- | --- | --- |
| 文本、caption、reply | typed RichText/ReplyInfo → Bridge 展示 → 目标 body/reply_to | caption 不被当成第二条重复文本；跨平台引用只用已确认映射，不复制来源原生 ID |
| photo / image | `read_media` 后用 `BytesSource`，或按调用方现有挂载/映射逻辑准备最终 `LocalFile.path` | actor 不调用 getFile/拼 token URL；既有文件路径授权、临时文件清理保留 |
| video / video-note | 普通 video 用 `Video`；圆形 video-note 由 TG 扩展视图识别并按 Bridge 既有策略转普通视频 | 圆形展示丢失是 actor 的显式转换，不冒充公共无损能力 |
| audio / voice | `Audio{Audio/Voice}`；保留两者区别 | 不支持的编码或 caption 组合显式处理，不把所有媒体都塞成 image |
| document | `File`，保留可用文件名/MIME/大小及文字说明 | 缺失、超限和读取失败保留明确结果；文件不得越过受管目录 |

### 3.6 Telegram → QQ：动画与贴纸

| 内容 | 迁移方式 | 缓存/失败边界 |
| --- | --- | --- |
| animation（GIF/MP4/WebM） | typed `Animation`；保留当前 WebM → GIF 的 actor 转换与显式 `gif_*` 设置 | 缓存使用来源 installation + `MediaIdentity`；缺稳定身份不持久化，不能哈希临时 URL 顶替 |
| 静态 sticker | TG typed sticker 视图 → 读取/现有静态处理 → QQ `Image` | 保留 emoji/描述及失败文本；不把 sticker 原生 JSON 放进公共 body |
| 视频/动画 sticker | WebM 保留现有 GIF 转换；TGS 由扩展明确标识 | 当前代码能识别 TGS/在失败时保留原文件，不等于 QQ 能播放；没有已验证解码器/目标能力时明确替代或不支持，不宣称已完成 |

转换失败后原格式是否能发，必须由目标 capability/格式校验决定，不能保留“原 WebM/TGS 塞进 QQ image 即成功”的假设。新增 TGS 解码器或转换依赖不借迁移自动引入；仍未验证的格式记为验收缺口。格式转换、占位和文本 fallback 都保留来源/原因，禁止 SDK 静默降级。

### 3.7 相册、引用、编辑与删除

1. **相册聚合**：`media_group_buffer.cpp` 的 key 改为 source installation + 完整 source conversation + `MediaGroupRef`。保留超时、顺序、去重和 flush 边界；聚合后仍保存每个源成员，不能把不同 topic 或账号同名 group 混在一起。
2. **延迟成员**：flush 后到达的新成员按既有策略处理并扩展成员/输出关系，重发同一成员不产生新 delivery。旧相册记录缺 topic 时禁止猜成主群；只把已证实的成员关系投影到新 ledger。
3. **引用**：回复目标从目标安装的已确认映射取出 `MessageRef`。保存一个明确的引用锚点与完整输出集合；不能因有多个输出就任取首条且丢弃其余。Missing/Unique/Ambiguous/Corrupt 继续区分，Ambiguous/Corrupt 不作为重发理由。
4. **编辑**：保留既有 TG typed edit 扩展及 QQ 端既有替换策略。按 output provenance 找到相关输出；若一个输出包含多个源成员，必须重新构造该共享输出或明确拒绝，不能删除其他成员内容。先删除后重发的失败也要保留状态，不声称有事务回滚。
5. **删除**：对已观察且支持的撤回/删除通知，用 `delete_message` 处理所有相关且有权删除的确认输出；逐项保留结果。TG 删除事件入口目前未完整实现，本提案不虚构平台会提供该通知，也不把“双向自动撤回已完成”写成迁移结论。

### 3.8 持久映射与重试：必须显式升级，不是换 ID

**当前 Bridge schema 为 3，普通 mapping 唯一键每个源/目标组合只能容纳一个目标消息；`ON CONFLICT` 会更新那个 ID。不能在旧表循环写入 N 条回执来假装支持一对多。** 现有 `is_primary` 主要描述反向来源所有权，也不能直接改成“发送顺序第一条”的同义词。

建议由 Bridge 自己提供 **schema 3 → 4** 的受控迁移。实施前核实该仓库届时版本；若版本已前进，使用下一个明确版本，不强写 4。数据模型分为：

| 记录 | 内容与约束 |
| --- | --- |
| logical delivery | pair、完整 source/target scope、准备/提交中/完整/部分/未知状态、attempt 身份、显式 policy/limits；提交前持久化 admission |
| source members | 一个或多个来源 MessageRef、旧存储 key、相册成员/节点与输入块对应；不改旧 source identity |
| physical outputs | 每个确认 MessageRef、顺序、可用媒体引用与状态；未知提交记录独立，不捏造消息 ID |
| output provenance | 来源成员/输入片段到输出的多对多关系、引用锚点及反向 primary owner；分开建模，未知关联不得用于成员级更新/删除 |
| retry payload | 版本化 typed body/完整目标引用、所需媒体与 lease、总预算、attempt/result 证据；不再序列化 CQ JSON + group/topic 裸字段 |

旧 v3 记录可确证的单输出导入为单输出关系，保留原键、反向所有权及相册关系。历史上只保存主 ID 的记录 **不能推断隐藏输出数量**；标记历史输出集合不完整，禁止宣称能完整撤回。迁移不得联网搜索补图、重新转发或重排 retry 时间来触发补发。

重试与恢复遵循五条规则：

1. 整个逻辑 delivery 没有确认输出、没有未知提交，且错误为 retryable + DefinitelyNotSubmitted，才允许现有有界自动重试；转换/来源读取失败不自动等于可重试发送。
2. 部分成功保存全部确认输出；未知结果或无法归类异常不自动重试整个 delivery。此版本不新增“从失败位置恢复发送”的隐式策略。
3. confirmation 通知按 attempt + 输出引用幂等归并；完整 `SendOutcome` 与通知可能交错，不能据此发两次 `MessageForwarded`。逻辑成功只在全部输出确认并持久化后发布。
4. provider 已返回但数据库写失败、进程崩溃留有 submitting attempt，恢复时视为待核实/未知，而不是 Missing mapping → 再发。引用可确定的已发部分继续可查；不得靠删重试记录抹掉未知状态。
5. 旧 retry 只在有足够 installation/conversation/topic/内容证据时转换；歧义或缺失记录隔离等待处理，不路由到默认安装/主群。

Bridge 共享文件、转换缓存和准备缓冲必须覆盖完整调用/所有输出的生命周期。未发出可按原清理规则释放；可能已提交不能当作“没用过临时文件”立即删除。若当前清理机制不足以表达该状态，应在 Bridge 自有配置/lease 方案中显式补齐，不新增静默 retention 默认值。

### 3.9 配置与管理命令

`installation_pairs`、每条 mapping 的显式 `pair`、group/topic 配置、sender/media 开关与执行分区保留。仅配置解析边界生成不透明目标；普通运行路径不继续传 `target_topic_id`。现在没有的 `SendLimits`/媒体读取预算必须显式提供，或由已配置的更严格预算确定推导并记录来源，不能提供未批准的默认值。

普通群成员显示名查询转 `get_member`。Bridge 的 `info`/管理命令若依赖 QQ 强制刷新、头像或专有用户资料，继续走受控 QQ 扩展；头像发送使用公共媒体接口，确定未提交时保留现有文字 fallback。贴纸缓存发送、topic 创建/管理、原生编辑与 QQ forward 查询继续是明确平台扩展，不把这些入口伪装成公共查询。

### 3.10 Bridge 迁移完成的验收门槛

这些是复用现有回放/手工验收的场景，不要求新增单元测试；无现有工具或真实平台证据的项目如实记为未验证。

| 场景组 | 需要记录的证据 |
| --- | --- |
| QQ → TG 图片 | 1/2/10/11/21 图，坏图夹在好图中，超限/过期/仅路径、极端尺寸、forward-node 图片；数量、顺序、caption 与替换/调整统计 |
| TG → QQ 媒体 | photo/album、video/video-note、audio/voice/document、GIF/MP4/WebM、静态/WebM/TGS sticker；支持/转换/明确不支持逐项列出 |
| 寻址与缓存 | 双 pair、同名相册跨账号/话题、主群/多 topic、回复、缓存失效、不同 bot 相同媒体 ID；无串群/串账号 |
| 副作用与恢复 | 第三步失败、超时/取消、部分 batch 回执、映射写失败/崩溃恢复、共享文件 lease；不整批重发，不丢确认 ID |
| 数据与更新 | v3 副本迁移、历史输出不完整、缺 topic 的 retry、延迟相册/重复通知、编辑/撤回全部可证实输出；TG 缺失删除来源明确标记 |

不能以“图片最低能力已实现”宣布 Bridge 完成；当前实际使用的媒体路径必须有对应能力或逐项记录明确阻塞/经审核的替代行为。

## 4. obcx-message-store：存原始证据，发 typed 投影

### 4.1 入口改造

`src/message_store_actor.cpp` 的 extraction/admission 不再从业务 payload 猜 user/group/message 类型。使用 SDK 归档输入提供的 source identity 与 typed view；仍允许通过独立 archive 接口接收 raw payload。这个 actor 不发送消息，**无需为了统一而新增 BotClient/self 查询**。

`include/core/actor/actor_messages.hpp` 中 `MessageStored` 的配套公共事件契约升级为“旧持久 source identity + typed MessageReceived + 原关联信息”。Bridge/Chat LLM 消费它，不各自重新解析 `payload/raw`。这是现有通知的契约升级，不另外广播一条同义业务事件造成二次准入。Bridge/Chat LLM 的 `onebot-cxx.message-store` actor 依赖版本随此事件边界收紧，不能只更新 SDK pin 而允许装入旧 Message Store。

### 4.2 数据与通知不变项

1. 保留 `source_platform/source_bot/conversation_id/message_id` 唯一身份、namespace/table 规则、原始 payload/raw 和已有更新判断；不把新编码塞回旧 conversation 列。
2. 保留 message/source 时间的既有存储含义；新增 typed `sent_at/received_at` 不通过伪造时间触发旧记录更新。
3. 持久写成功后才发 `MessageStored`；失败仍发 `MessageStoreFailed`，headers、command processed、correlation/causation 和 retryability 按旧约束传播。
4. 历史读取调用集中 legacy decoder；无法恢复媒体/话题时返回明确缺失，不能把新 typed JSON 当成原始证据覆盖过去。
5. 单为增加运行时投影不要求重建消息表或修改当前 schema。若实现需要持久化新的投影/版本列，先在 Message Store 仓库单列迁移与回滚，不夹在 core SDK 发布中自动执行。

验收：同一原始样本在迁移前后具有相同 source key、重复/编辑结论及通知次数；Bridge/Chat LLM 不因表示升级重新处理历史消息。

## 5. chat_llm：换事实输入，不改决策与 prompt

### 5.1 历史与当前消息

`src/session_types.cpp`、`src/source_message_reader.cpp`、`src/actor_session.cpp` 与 `chat_llm_actor.cpp` 接收 typed source view。删除 image 段、TG photo 数组、QQ native image locator 的业务解析；历史 raw 只由集中 legacy adapter 解释。

现有 session/message observation、稳定 `image_id`、sidecar 和 completed-send key 不改。图库定位值内部可升级成版本化 `MediaRef`，但不能按新编码重新计算用户/模型看见的 `image_id`。同一旧记录重读不成为 source edit，未知旧 locator 保留不可用状态而不是联网猜测。

### 5.2 Self、提及和引用

`src/actor_addressing.cpp` 从 `BotClient::get_self` 读取 cache，直接比较同型 `UserRef`/规范化 username。来源提及/引用使用 typed 证据；保留现有严格本地证据优先和 `LocalOnly/AllowProviderLookup` 限制。self 缺失保持未知，不等待 getMe/get_login_info。

稀疏关系标记、direct/declinable、工具授权、历史 sidecar、召回/压缩和回复决策继续属于 actor；不改用户 prompt、不新增 core `is_self`，不因 refresh 改写历史或重新准入消息。

### 5.3 工具逐项替换

| 工具/入口 | 当前耦合 | 迁移 |
| --- | --- | --- |
| `view_image` / `resolve_visual` | QQ `get_image` 后 URL-only；TG getFile 下载后 data URL | `MediaRef → read_media → 受支持的模型图片输入`；统一 actor 内 MIME/像素/预算校验，不将 bot 媒体 URL 交给模型 |
| `get_user_info` | 两套原生 member request/字段 | `get_member` 后渲染同一基础视图；未知字段保持未知，不发明 TG 任意用户查询 |
| `send_message` / `send_tracked_text` | CQ reply 段、GroupTarget 与 TG topic request | 验证可见 reply ID 对应的 scoped MessageRef，`BotClient::send`；保留意图记录/claim/去重 |
| `decline_reply` 与上下文工具 | actor 自有策略/只读历史 | 不因 SDK 改造调整工具名称、授权集合、模型 prompt 或 decline 限制 |

QQ URL-only → 读取字节是明确的传输行为变化，不假称原链路完全不变。建议以必填 `image_read_max_bytes` 取代仅 TG 生效的 `telegram_image_max_bytes`，为两平台都提供显式读取预算；迁移文档要求操作者填写，不自动给 QQ 套一个默认值。继续约束 multimodal_request_max_bytes、刷新次数、复用时间和总 deadline，包含 base64 后的请求大小。现有 PNG/JPEG/静态 WebP 校验不能因统一读取而放宽；动画明确拒绝或走另获批准的转换。

图片 bytes/data URL 只存在于本 turn 的易失请求状态，不能写入 journal、summary 或原始消息库，也不把“曾看过 image_id”当成下一 turn 自动授权。模型 provider 不支持所需输入格式时明确失败，不退回原生 bot 下载 URL。

### 5.4 发送失败与恢复

普通文本选 `SingleMessageOnly`，保留现有文本长度边界，避免迁移时突然拆成多条。将 `SendOutcome` 映射到既有 intent/已发送/失败/未知记录；任何已确认或可能提交结果阻止自动重复发送。来源 echo/物理确认不能代替逻辑完成或造成第二次准入。

验收：相同历史副本产生相同 session/image_id/sidecar/完成记录；QQ/TG 图片均不外泄凭据；self miss 不产生网络调用；过期回复授权仍被拒绝；未修改用户 prompt 文件。

## 6. obcx-exhentai-fetch：普通发送统一，QQ 专有发布保留

### 6.1 普通消息与目标

`src/gallery_presenter.cpp/.hpp` 中普通文字、健康/空结果、TG 封面和 topic 回复转为公共 `BotClient`。配置/命令边界提供 `ConversationRef`，运行时不再构造 `GroupTarget` 或 `SendTelegramTopicMessageRequest`；原 `DestinationId` 的持久订阅/去重 key 通过兼容边界保留，不能因为 topic 内编码而重新订阅或重发。

TG 缩略图使用 `Image{BytesSource, caption}`，trusted thumbnail hosts、签名检查、caption/URL 边界保留。只有确定未提交且属于现有允许降级的图片失败，才可发原有文字 fallback；可能提交/超时不能再发一次纯文字。多帧格式按 `Animation`/目标能力或显式既有替代策略处理，不把动画盲塞静态图片。

### 6.2 QQ 共享图片与合并转发

| 现有模式 | 迁移方式 | 不得改变 |
| --- | --- | --- |
| `qq_send_images=false` | 普通 `RichText` 发送；forward 模式仍用专有 forward | 不启动图片处理，不隐式启用审查/上传 |
| 普通分条 QQ 发布 | 已审查 PNG 经调用方现有映射得到最终 `LocalFile.path`；逐条公共 send | 图片/文字顺序、受限内容阻断、可选图失败保留允许文字 |
| `qq_use_group_forward=true` | `src/qq_gallery_forward.cpp` 使用 typed QQ forward nodes 和调用方准备的最终 `LocalFile.path` | 一个 direct group-forward、节点顺序/配对、99-node 与序列化字节边界；不退化成普通消息串 |
| `SharedMediaStore` | 保留 actor owner/namespace、lease、磁盘/图片预算与恢复规则 | 不把 container file URI 当公共 LocalFile，不把租约责任交给 mapper/SDK |

审查、重编码、可信来源校验、允许/受限文字判断和“准备完成不等于提交”继续属于 ExHentai。专有 forward 扩展负责 native node 编码，actor 提供结构化节点，不手工拼任意 CQ JSON。已有共享挂载可读性和真实 QQ 可见性尚待用户环境验收，迁移不能写成已验证。

### 6.3 独立逐条发送与 SDK stop-on-first-failure 不冲突

当前 QQ direct 模式会在单条拒绝/不确定后继续后续图片/文字，取消才停止；这不同于单个 SDK `AllowSplit` 请求的首次失败停止。迁移时 **每条预备输出发起独立 `SingleMessageOnly` 调用**，由 actor 保留既有继续策略；不能把整批包装成一次 `AllowSplit` 后悄悄改变行为，也不能因下一条成功覆盖前一条未知。

1. 全批先完成有界准备，必要文字/包络失败不提交、不消耗自动尝试；保持原 claim 时点。
2. 手工搜索分别统计 confirmed/failed/unknown/unsent 的消息数，不自动重试；成功不新增完成摘要。
3. 自动批次仅全成功为 Delivered；有任何确认或未知的混合结果为 Uncertain，禁止整批重放；全部权威拒绝才适用原有有界重试。
4. lease 覆盖全部独立调用及 settle；确定未提交、确认成功和可能提交按原 retention 政策区分，不能在第一次返回后释放剩余输出所需文件。
5. 只升级引用/SDK 不触碰现有 ExHentai namespace schema、subscription/evidence/delivery key，不运行数据库“顺手升级”；如确需格式变化，由 actor 单列审批。

### 6.4 QQ forward probe

`tools/qq-forward-probe-actor/src/qq_forward_probe.cpp` 的目标改为 `ConversationRef`，使用同一 typed forward extension，反馈文字用公共 send。更新它自己的 `package.toml`/schema/SDK 构建，保留探针的显式命令触发及字节上限。构建 probe 不等于运行它；本次不向真实 QQ 发探测消息。

## 7. obcx-actor-template：最小示例也必须匹配新 SDK

`src/example_actor.cpp` 当前主要展示同步处理、`run_blocking`、emit、命令完成与 `Propagation::Consume`，不是完整 bot 客户端示例。保留这些示范，不为了迁移加入无意义 self/network 调用。

1. 更新 `package.toml` 的 input contract schema、SDK version/pins 和导出/构建说明；`OBCX_ACTOR_EXPORT_V2` 调度 ABI 名称保留。
2. 任何命令输入/引用示例改用新 SDK 类型；文档新增最小“typed 事件原会话回复”示例时只用 `BotClient/ConversationRef`，不展示 CQ/file_id/topic 拼接。
3. 使用冻结安装 SDK 构建并核实 include/link 闭包；旧 schema 在 factory 前被拒绝。无需新建单元测试或运行实际 bot 消息。

## 8. 实施顺序与联合退出条件

1. **先冻结 core 契约**：确认本附件 §2 的补充、跨安装 bytes 路径及调用方最终路径边界；实现 codecs、完整结果、类型化归档/通知与 schema 3 门禁。没有 actor 所需 capability 的列明确阻塞。
2. **先迁移数据入口**：Message Store + 集中 legacy decoder，再迁移 Chat LLM/Bridge 的 source reader；固定旧身份/通知次数后才接发送。
3. **逐 actor 完成**：Bridge 按 §3 分路径与 ledger 迁移；Chat LLM、ExHentai、template/probe 各自验收。先在各 owner 仓库登记任务，不把 core 的一项勾选当作独立仓库已发布。
4. **隔离构建与副本验收**：core/SDK 在隔离目录；所有 actor 用冻结 SDK 和 `scripts/actor_build.py`，至少 6 workers，低负载全核。复用现有测试/回放；新写或改写单元测试另获授权。数据库只在脱敏副本验证，不改在线库。
5. **只准备发布**：更新所有 package/SDK 要求、registry entries 和离线包，列出配套 core/actor/config/完整共享 DB 备份。Bridge schema 迁移与 core 替换/重启另行授权；不能 actor-only reload 激活新 SDK。回滚恢复匹配整套产物/数据，并人工核对切换后确认/未知发送，禁止自动重放。
