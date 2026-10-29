## Context

chat_llm 当前使用私有 sqlite3 历史库、每轮最近 N 条窗口、字符串-only prompt，用户 command 提问被后续查询过滤，工具轨迹也不跨轮保存。message-store 已保存原始消息，既有表有平台内 `id` 主键及 `(source_platform, source_bot, conversation_id, message_id)` 唯一键，但 upsert 会覆盖原行，没有修订日志。

用户确定的存储边界：message-store 不改；LLM 可以有自己的 DB，但读写必须走 core DbManager；源消息按主键/唯一键逻辑关联，不强制真实外键或共库；旧 history.db 直接弃用、不迁移。

用户进一步明确了图片行为：**历史只放 `<image>稳定标识</image>`；模型需要时调用 tool，执行器再调用 QQ 接口取得临时 URL，供本轮模型看图。不能把临时 URL 当作持久历史，也不需要本地图片库。** 本修订替代此前下载落盘、稳定 data URL、媒体 pin/GC 的方案。群级 toggle_think、显式 128k token 配置和摘要压缩继续有效。

## Goals / Non-Goals

**Goals:**

- 复用原始消息，使用 core 管理独立或同库的 LLM 状态；冻结必要的文字/图片标识投影而非二次完整归档。
- 文字历史、图片标识与 provider-native reasoning 按 exact platform/installation/conversation/topic 隔离追加，保留完整用户和工具轮次，并按 endpoint 协议跨轮恢复 reasoning。
- 模型通过 view_image 主动选择何时取图；QQ 临时 URL 不持久化，TG 图像只在本轮内存中处理。
- 显式 token 预算触发摘要压缩；群级 toggle_think 只控制 decline 理由输出。
- 恢复不重发副作用，错误不伪装成成功，缓存指标区分持久前缀与临时视觉尾部。

**Non-Goals:**

- 不修改 message-store 的源码、schema、索引、接口、事件、写入或清理行为，不回填命令/出站记录。
- 不提取共享存储库，不修改 core 数据库接口，不使用私有 sqlite3 连接。
- 不保存平台临时图片 URL，不建立本地图片目录、内容寻址缓存、QQ HTTP 下载器或媒体 GC。
- 不承诺旧图片永久可取或含图 wire request 跨轮完整前缀相同；平台失效时必须明确报告。
- 不使用 URL 文本冒充视觉输入，不新增视频/音频/动画格式转换，不绕过 typed gateway 调任意 QQ 方法。
- 不新增单元测试、不部署、不重启、不删除现场文件。用户已授权最小 OneBot 图片 SDK 扩展和 Bridge 取图迁到 core；源码/隔离构建授权不等于自动替换运行中的 SDK/core。

## Decisions

### 1. LLM DB 通过 core 接口，message-store 只读

chat_llm 内部区分 SourceMessageReader 与 SessionRepository，分别通过 `DbManager::run_read` / `IDbConnection::query` 和 core 写入/事务/migration API 工作。同步工作由 BlockingExecutor 执行。LLM schema 初始化只触及其 namespace，源 schema 只做 SELECT 兼容性检查。

LLM/source DB instance 和 namespace 均显式配置。共库可按键 JOIN；分库先读取本地引用，再通过 source instance 批量查询、内存合并，不依赖 ATTACH、跨库事务或强制外键。SourceMessageReader 失配/缺表不触发建表、加索引、修复或补写。

备选方案：强制共库/外键限制部署并可能影响基础服务删除；提取共享库或直接复用 actor 私有服务超出边界。逻辑引用足够满足关联查询。

### 2. 精确引用、有限分页与最小派生投影

MessageRef 包含 DB binding、namespace、platform、installation、conversation、native message ID，可附平台表 id 加速，但必须校验完整唯一身份。表名按既有 namespace/platform 编码验证，SQL 值参数绑定。topic 从既有 payload/raw 提取，null 仅匹配无 topic；保留 v4 `raw = ''` 时由 payload 重建的语义。

首次 session 从固定源高水位选择预算内近期完整消息，冻结 bootstrap 起点；后续按 existing id 有界分页，而非重新取最近 N 条。upsert 不增加 id，所以这只是新行游标，不是假定存在修订 feed。重复观察按 normalized fingerprint 去重；观察到的编辑追加为新事件，不回填旧前缀。

仅对实际进入保留会话的输入冻结文字 envelope、图片标识、source reference 和渲染 fingerprint，不复制原始 raw/payload。源更新/删除不破坏已冻结文字；未物化引用缺失则明确失败。读到的投影、游标和引用在 LLM DB 单事务提交，失败不前进，分库不要求源锁或原子分布式快照。

### 3. 图片引用是持久身份，不是 URL

actor-owned `attachment_refs` 将不透明 image ID 关联到 exact session/source scope、native file locator、segment index，以及平台可信的可选 MIME/尺寸/大小元数据。ID 由 actor 从消息附件建立，不能从用户文本中的标签自行注册；不使用 URL 或签名查询参数充当稳定身份。

例如模型看到：

```text
这张图是什么意思？ <image>img_7f...</image>
```

LLM DB 保存该标识及可再次调用平台 API 的原生 locator。message-store 原 payload 即使包含过期 URL 也保持不动；chat_llm 不把它作为持久模型字段或重新解析的唯一依据。命令图片没有源表行时同样保存受 scope 校验的本地 attachment record。

“以后仍可按标识请求图片”不等于承诺平台永久保留文件。缺 locator、平台不支持或历史文件被回收时返回明确 unavailable，不读取任意本地路径、不猜 URL。

### 4. 普通事件、command 和出站不改变基础服务

```text
普通消息 → 现有 message_store → 现有 MessageStored → chat_llm 只读关联
已授权 ChatCommand ─────────────────────────────→ 本地 session
模型/工具输出 + confirmed bot receipt ──────────→ 本地 session
```

chat_llm 消费已持久化事件，不再第二次完整采集 RawMessageEvent。typed command 保持 ACL/availability/collect scope 和 Consume；没有源行的 `/chat` 仍作为本地用户输入保留，不再过滤上轮 command 提问。toggle_think 属于控制事件。

出站 receipt 及 tool result 只写本地，后来 self-echo 按 exact provider identity 关联，不能重复追加或触发回复。没有 source row 是允许状态，不向基础服务补写，不制造给 Bridge 转发的假入站事件。busy 会话先可靠保存 direct input 或保留未消费 source 游标，再合并/排队唤醒，不丢提问。

### 5. 持久 journal 与临时视觉上下文分离

持久层保存 epoch、源游标、ordinal、文字/marker 投影、模式控制、assistant/tool 轨迹、provider-native reasoning、action intent/receipt、摘要和可信附件引用。provider/endpoint/model/system/tool/renderer 及 reasoning 协议版本指纹限定重放兼容性。

```text
固定 system + 固定 tools + 当前 epoch 冻结摘要
  + 持久有序 journal（文字、image marker、sanitized tool result、协议允许的 reasoning）
  + 本轮新增 journal
  + 当前 view_image 产生的临时多模态输入（仅内存）
```

固定工具集合包含 get_user_info、view_image、send_message、decline_reply。direct/proactive 差异作为当轮控制追加，执行器独立校验允许动作和 visible user/message/image IDs，不由摘要或用户文字赋权。

view_image 的持久结果只记录稳定 ID、状态及“仅本轮提供图像”的说明，不含 URL 或像素。成功后 renderer 将 URL/bytes 转为 provider 支持的图像 parts；若 tool role 不接受图片，使用运行时生成的、关联 call ID 的临时 user 多模态消息。不能只把 URL 当文本返回然后宣称模型已看图。

当前工具循环维护 request-local wire transcript，附件插入后在当前有效期内保持位置，后续 assistant/tool 继续追加；同时单独写不含附件 URL/bytes 的 canonical journal。下一轮从 canonical journal 开始，不恢复上轮临时图像；需要像素就再次调用 view_image。URL 过期只替换当前视觉层，不替换持久 marker。

因此保证的是**同 epoch 的 canonical journal 追加稳定，包括持久文字/标识与 reasoning 记录**，不是所有 wire request 跨轮都是严格前缀。视觉输入的出现/移除，以及 endpoint 明确要求的 reasoning 重放投影变化，都可能降低其后缓存复用；必须单独观测，不能改写 journal 或用 durable-prefix 指标冒充完整请求缓存命中。不存在这两类变化时仍须保持持久输入前缀，不允许借协议投影任意丢弃历史。

完整保存 call IDs 和所有 tool results，terminal 后未执行调用写 not-executed。reasoning 不是 Codex 专用功能，也不是日志附件，而是所属 assistant/tool 轮次的协议状态：

- 存储以 exact session key、epoch、turn、ordinal 绑定 provider-native blocks/items，保留原生类型、内容、签名、phase、item/call IDs 与顺序；与对应 assistant/tool 状态事务性提交。不使用 actor 全局 reasoning 缓冲或跨会话缓存，异步回调和重试也必须校验会话/epoch 所有权。
- 明文 `reasoning_content`（如支持该字段的 DeepSeek endpoint）、带签名 thinking/redacted blocks（如 Claude 的协议形式）、Codex encrypted reasoning 都遵循同一生命周期。字段示例不等于宣称所有模型/端点均支持，也不新增 Claude transport 的实现范围；现有适配器必须按实际 endpoint 契约处理，未来适配器遵守同一约束。
- 每次构建工具续接、下一用户轮次、重试或重启恢复后的请求时，从该会话 journal 读取 reasoning，按明确能力矩阵决定允许回传的形态和范围：区分同轮工具续接、跨用户轮次、重启重放及完整 block 依赖。支持重放时必须附带，不允许静默仅保留最终回答；未返回的内部 CoT 不虚构，不支持/未知能力显式报告。若缺失状态使当前工具续接不合法则失败，不发送残缺请求。协议不允许某类历史 reasoning 时可保留本地记录，但不得发送禁止字段；这种已声明的协议投影不改写 canonical journal。
- 原生 reasoning 不转换成普通 assistant/user 文本，不把其内容当系统指令、授权依据或群消息。明文、签名、密文均不进入普通 trace/日志；toggle_think 不影响其收集、持久化和重放。
- 签名或 opaque/encrypted 块必须原样保存，不解析密文、不截断、不脱敏改写其受保护内容。普通可编辑响应字段若回显本轮临时图片 URL/bytes，在持久化前清理；可检查但不可编辑的签名块若含此类值，应报告明确的持久化/续接冲突，不提交受污染或签名失效的轮次、不重复已执行发送。opaque 块允许作为 provider 的不透明续接状态落库，但无法验证其内部是否编码过 URL/像素；禁止声称已做密文内部脱敏，也禁止用它恢复图片链接或附件。图片再次查看仍必须调用 view_image。这是“无临时图片数据持久化”对不透明协议状态的明确边界，不是本地媒体缓存例外。

### 6. view_image 执行路径与短期 URL

1. 校验请求 image ID 属于当前 session 可见、可信 attachment registry；拒绝任意 URL/file path 参数及跨 installation/topic 引用。
2. QQ：使用 exact-installation typed image resolver，以 native locator 请求当前 URL。校验协议、平台返回字段、公共目的地和凭据边界，然后让已确认支持外链图片的 LLM provider 获取图片；actor 不自行下载 QQ 图像。
3. TG：按相同 lazy tool 语义调用现有 FetchTelegramFileRequest，传入显式 maximum_bytes；结果只保留当前 turn 内存并转为临时 data URL。不得发送含 bot token 的 Telegram 下载 URL。
4. 对临时图像进行请求预算检查，再发视觉 continuation；失败只记录稳定 ID + failure，不虚构图像内容。
5. 当前 turn 内根据可靠 returned expiry 和显式保守 reuse policy 决定 URL 是否仍可用；必要时有限刷新。用户提到的约五分钟不是代码默认值，也不是所有 OneBot 的保证。
6. turn 完成/取消立即丢弃 URL/bytes；下次 view_image 重新解析，不从 DB 加载链接。

只读 view_image 可在有界策略下重新解析，但任何 image failure retry 都不得重放已执行 send_message。provider 不支持外链、QQ 只返回本地 file path、locator 不存在或 URL 刷新失败时明确报告，不私自加下载落盘 fallback。

**已纠正的平台实现**：用户明确使用 LLOneBot / OneBot 11，之前以 Lagrange 推导 get_rkey 的分支撤销，不是当前实现或验收前提。标准 `get_image(file)` 的入参来自图片段 file，标准返回 file 是服务端路径，不保证公网 URL。

已核实 LLOneBot upstream commit `dc2b2b90092ef0f571b5546878288b83fff1e436`（仓库已重命名为 LuckyLilliaBot）：GetImage 继承 GetFileBase，支持显式 `download: false`，按 file UUID/文件名查平台缓存并返回 `url/file_name/file_size`，此模式不执行本地下载。入站 image.data.file 是 picElement.fileName；core 保存 exact installation + 原生 file 标识，不从临时 URL 推导身份、不自行管理 rkey。typed get_image 保留标准路径结果与可选 URL 的区别；URL-only 消费端缺少受支持的公网 URL 就明确失败，不读任意服务端路径、不发送 base64 回显。

LLOneBot 自己负责 getImageUrl：无 rkey 的 NT 源地址会使用平台当前密钥，但已带 rkey 的 originImageUrl 可能原样返回。故重新调用不等于保证换 URL 或永久可用；遇到平台缓存缺失/旧链接失败时有界失败，不在 OBCX 偷加 get_rkey fallback。接口未返回可靠 expiry，使用调用方显式配置的短期 reuse 上限，不能猜五分钟。

用户同时授权 Bridge 清理重复图片获取实现。新增 core 有界 HTTP 图片获取/探测接口，显式接收 URL、期限、响应字节限和 Range 意图；Bridge 的批处理、重试/占位、MIME 业务识别、归一化和发送行为仍留在 actor，仅将 URL 解析、临时客户端构建和 GET/Range 执行收敛到 core。TG 已走 typed FetchTelegramFileRequest 的路径继续保留，删除未使用的旧 connection-manager 下载模板。此扩展不改 message-store、DbManager 或通用网关结构，也不把 Bridge 必需的图片下载强加给 lazy LLM。

### 7. 会话恢复与群级 toggle_think

journal、锁、激活与 decline 计数按 `(platform, installation_id, conversation_id, topic-or-null)` 隔离。provider/endpoint/model/prompt/tool/renderer 或 reasoning 协议不兼容改变时显式切 epoch，不将旧 provider 的 reasoning 发给另一 provider，不跨 epoch 偷带续接缓存。兼容重启恢复同会话完整 reasoning；不兼容切换保留旧 epoch 的本地记录直到正常清理，但新请求仅使用兼容的新 epoch 状态，并明确记录续接边界。旧 generation drain 后再恢复 writer，candidate 验证不修改当前状态。

发送前写 action intent，确认后记录 receipt/result。跨崩溃存在“可能已发送”的窗口，恢复标为 ambiguous，不自动重发、不伪造成功。恢复持久 journal 不等于重新执行历史工具；图片只有模型再次请求时才解析。

toggle_think 单独按 `(platform, installation_id, group_id)` 群级原子切换并保存，同群所有 topic 共享，其他群/bot 独立。它只控制 proactive decline 理由是否发出，不改 reasoning_effort、tools、epoch 或 topic 历史。未切换群保留既有未开启行为，不新增配置默认值。确认发送到命令 topic，理由发送到其决策 topic，不做全群 topic 广播。

### 8. 显式 token 与图片工具配置

所有选项必须显式给出，不添加默认值。128k 为十进制 128000 tokens，不是字节或条数。

| 字段 | 含义 |
| --- | --- |
| `context_window_tokens`、`response_reserve_tokens` | 上下文总量与生成/reasoning 预留 |
| `compaction_trigger_tokens`、`compaction_target_tokens`、`recent_turns_to_keep` | 压缩触发、目标与完整近期轮次保留 |
| `summary_max_output_tokens`、`summary_timeout_ms` | 摘要预算与期限 |
| `token_accounting_profile`、`tokenizer_vocab_path` | 显式版本化 tokenizer、图像和协议计费策略，以及 SHA-256 固定的本地词表；含未知尺寸时的保守上界 |
| `reasoning_protocol`、`output_limit_parameter` | 显式 endpoint 重放契约和已支持的输出限字段；Codex 只能选 `none`，不宣称服务端输出 cap |
| actor `db`、`db_namespace` | LLM 状态库 instance/namespace |
| `message_store_db`、`message_store_namespace`、`source_fetch_page_size` | 只读源绑定及有限 I/O page |
| `session_idle_ttl_days`、`cleanup_interval_ms` | 本地 session/attachment locator 保留及清理周期 |
| `image_resolve_timeout_ms`、`image_url_reuse_ms`、`image_refresh_limit`、`max_images_per_turn` | 按需解析期限、显式临时复用上限、刷新次数与当前 turn 图像数上限 |
| `telegram_image_max_bytes`、`multimodal_request_max_bytes` | TG 内存获取上限和 provider 请求体上限 |

token 字段接受正整数或整数+k/K，拒绝浮点、小数缩写、未知单位、负数和溢出；满足 `target < trigger <= window - response_reserve`。URL reuse 的显式 0 表示不跨 provider 请求复用临时链接，不是缺省值；计数/TTL/期限按各字段明确合法范围验证。

未打开的 `<image>` 按文字计费，不为计费预先取图；view_image 返回后才计入实际本轮视觉成本。QQ 不下载图片，不能声称校验过其实际像素/字节：使用可信平台元数据或 accounting profile 的显式保守图像上界；实际取图限制由 provider 支持情况决定。所有 normal/tool/retry/summary 调用均检查总输入、输出预留及请求体大小。

只发送 provider 已支持的 output-limit 字段；Codex 私有端点不盲加参数。unknown profile/能力不静默猜测，估算与实际 usage 分开。移除 history_db_path/history_limit/history_ttl_days；不再增加 media_store_path、QQ proxy/downloader、媒体保留/GC 等已撤销方案字段。

### 9. 摘要压缩不自动展开历史图片

低于 trigger 只追加；达到阈值选择完整旧轮次，保留近期轮次、current input 和未终结动作。摘要是无执行工具、受输入/输出/超时预算约束的独立请求；来源太大按完整轮次分块。摘要基于持久文字、已有图像观察和 markers，不遍历全部历史调用 view_image。

压缩以完整轮次及 provider 声明的 reasoning 依赖单元为边界。近期保留尾部的 reasoning 与关联 messages/tool results 原样保留；被压缩前缀的 reasoning 从新 epoch 活跃请求中一并退役，旧记录只按 actor-owned 清理规则释放，失败不提前删除。摘要请求使用对话文字/已公开的回答和工具观察，不把原始 reasoning 拼成普通文本，也不把 opaque 块交给摘要器解密或改写。摘要不是新的 reasoning block；若 endpoint 依赖无法保留的旧块，必须明确拒绝该压缩方案，不能截断签名、伪造续接或静默降级。summary、retained reasoning、边界和 epoch 的切换同事务提交。

摘要可携带仍有意义的 image IDs，必须与压缩前可信 attachment metadata 校验并在本地结构化记录其允许集合；自由文本不能注册新图片。未来模型看到这些保留标识仍可按需请求图片，但不能声称摘要请求直接看过原图。

summary + retained tail + pending input 计费符合 target 后，单事务提交冻结摘要、epoch、边界和引用集合。失败保留原 epoch，不滚动删头、不把图像 marker 偷换成 URL、不降低近期保留要求；只有仍符合硬预算时才允许未压缩请求，否则明确 capacity failure。不可分割超大轮次不无限重试。

session TTL 只释放本地 journal 和不用的 attachment locator；活动 turn 保有必要引用直到结束。没有本地图片文件/媒体 GC，也不修改源表保留期、pin 或删除行为。

### 10. 生命周期、观测与所有权

ValidationOnly 仅检查显式配置、声明的 DB binding、静态 SDK/provider 协议、词表/profile、prompt 和固定请求预算（Codex 凭据文件仅本地只读校验，不刷新）。chat_llm 在此阶段不获取 DB 服务、不构造 repository、不读 schema/消息/会话、不依赖 live gateway，也不创建私有 BlockingExecutor；通过配置校验不代表数据库可用或 schema 已兼容。用户明确选择保持 core 不变，不为配置校验补 runtime 执行器。

源 schema 与 actor-owned schema 的实际只读探测留到 startup/reload，并使用进程提供的 core BlockingExecutor。reload candidate 不初始化表、不改活动状态，需初始化新状态 schema 或更换 DB/namespace 时返回 restart-required；只有显式 startup 可初始化 actor-owned schema。startup 时平台尚未注册，live gateway 能力只在 reload 或实际 view_image 时检查。所有准备/校验路径均不调 image resolver、不获取像素、不摘要、不清理或读取旧 history.db。

上述无 DB 访问承诺限定为 chat_llm 的 ValidationOnly 路径：现有 core CLI 在 actor preparation 前仍按既有逻辑打开配置的 DB instances，可能创建空文件/WAL；本 change 不修改该行为，不宣称整个进程零数据库文件操作。平台 SDK 的新二进制若经批准实现，必须与独立 core/SDK 发布和进程重启协调，不能在 live core 工作区偷偷重建。

指标分别记录 durable-prefix fingerprint、临时视觉请求、view_image 成功/刷新/失败、估算 token、provider 实测 input/output/cached tokens、epoch 和压缩边界。缺 usage 是 unknown，不是零；本地 prefix 检查不能证明真实 provider 命中。临时链接、图像 bytes、token 及所有形式的 reasoning（明文、签名、redacted、encrypted）均不进入普通 trace/日志。只允许不含内容的 reasoning 能力、兼容性和失败状态诊断，不打印原始 provider 响应作为错误信息。

四项 chat-llm 主规格归属 `local_actor/chat_llm/openspec/specs/`，新增 core-image-access 归属 core，bridge-core-image-access 归属 Bridge。根 change 保留联合规划与用户追加范围授权；不扩大到 message-store 或通用数据库改造。所有完成 checkbox 保留原任务证据，旧 SDK 可用性的已完成检查仅证明初始基线，新增 SDK 仍须隔离重建、安装冻结和验证。

## Risks / Trade-offs

- [平台图片或原生 locator 后来失效] → view_image 明确失败，不承诺永久视觉记忆，也不悄悄恢复本地缓存方案。
- [临时视觉层影响完整请求缓存] → 不改稳定文字/marker 前缀，单独观测视觉尾部，不承诺所有 wire requests 严格追加。
- [QQ API 可能只返回本地路径/旧链接] → 核实实际 provider 契约；能力不支持则报告，不能凭 get_image 名称猜成功。
- [外链图片尺寸未知] → 显式保守计费上界及 provider 能力检查，不为计费偷偷下载。
- [临时链接通过 trace 或模型回显进入持久化] → 普通可编辑字段按本轮 URL/bytes 值清理；签名保护的可检查回显明确失败，密文不能声称可内部检查；reasoning 块在普通日志中整体移除。
- [跨会话 reasoning 串用或适配器丢弃字段] → exact session/epoch 所有权、完整块持久化、版本化 endpoint 重放矩阵与跨轮/重启隔离验收。
- [压缩破坏 reasoning 依赖或签名] → 完整单元保留/退役、原子 epoch 切换；无法形成合法请求时明确失败，不伪造 CoT。
- [源可变且分库无分布式事务] → 最小文字/marker快照与本地幂等事务；不承诺源编辑全历史。
- [摘要有损且切 epoch 失缓存] → 仅阈值触发、保留完整近期轮次与可信图片引用，失败原子回退。
- [外部发送与 DB 无原子提交] → intent/receipt/ambiguous，不在恢复或图像重试时重复发送。

## Migration Plan

1. 按已批准范围实现 core OneBot 图片解析/有界 HTTP 获取接口、Bridge 消费迁移和 chat_llm；先隔离构建匹配 SDK，再冻结供 actor-only 构建，保持 message-store/DbManager 不变。
2. 显式配置 LLM/source DB、namespace、预算、profile 和按需图片工具限制；移除旧 history 配置，不添加本地媒体缓存目录。
3. chat_llm 普通入口消费既有 MessageStored；command Consume 仍不走基础服务补写。新状态库只初始化自己的 namespace。
4. 旧 history.db 不导入、不读取、不再创建。用户允许直接弃用删除，但实际删除必须在旧 writer 停止、路径核实且无其他使用者后独立执行，禁止启动时自动清理或触碰源库。
5. 回滚只涉及匹配的 chat_llm 状态/代码/配置及另行批准的 SDK 发布步骤，不能用旧 actor 打开新状态 schema。message-store 无迁移无需回滚。
6. actor 编译使用冻结 installed SDK 与 actor_build.py，至少 6 workers，低负载全部 CPU；不在 live core 工作区构建，不自动部署/重启。无需新增单元测试，使用既有测试与隔离集成演练。

## Open Questions

- 最小 SDK 和 Bridge 取图迁移已获批准；采用 OneBot 11 get_image 和已核实的 LLOneBot download:false/URL 扩展。仍需现场验收部署版本、历史文件缓存与 URL 可达性，不把 upstream 源码检查当作现场成功。
- core/SDK 产物只能先做隔离构建和冻结，实际进程重启/部署须另行操作，不自动替换 live 二进制。
- 已按公有协议/固定源码和真实客户端 fixture 核对 OpenAI `image_url`、Codex `input_image` 与显式 `o200k_base-low-4096-v1` profile；具体线上 endpoint/model 的外链接受、实际费用/授权仍须现场验收，不支持时明确失败而不降级。
- 上线 DB/namespace、预算、临时 URL reuse/refresh 策略和真实 provider 验收凭据仍由明确环境参数提供，无默认值。
- 已实现四个显式版本化 reasoning 契约，矩阵、来源、同轮/跨轮/无工具投影与限制见 `local_actor/chat_llm/docs/provider-contracts.md`；受控验收与真实端点能力分开。直接 Anthropic transport 或未知表示不受支持，不按品牌猜测，也不把通用状态限定为 Codex 密文。
