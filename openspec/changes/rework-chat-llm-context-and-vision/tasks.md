验收与逐项证据见 [acceptance.md](acceptance.md)。完成表示源码、文档和隔离验收已交付；不表示真实 provider 验收、部署、重启、旧库删除、提交或归档。

## 1. 确认实现边界与现有能力

- [x] 1.1 核对初始四份 specs 与设计，记录 chat_llm、message-store、core 的工作区基线；初始范围证据保留，后续按第 11 节的用户追加授权扩展 core 图片接口和 Bridge 消费迁移，不改 message-store/DbManager、不新增共享存储库。
- [x] 1.2 核实现有 MessageStored payload、message-store v4 主键/唯一键、topic 提取和 `raw = ''` 语义，列出 SourceMessageReader 只读列与兼容性检查，不提出源 schema/index 改动。
- [x] 1.3 按用户新确定的 marker + view_image 方案核实 QQ 的实际临时 URL API、TG typed 获取和两个 provider 的外链视觉契约；最小平台扩展已获许可，仍须通过网关，并确定显式 token/image accounting profile。
- [x] 1.4 核实可用冻结 installed SDK、actor-only 构建目录及既有验收入口；实施使用 `scripts/actor_build.py`，不得在 live core 工作区构建 actor 或替换 core 二进制。
- [x] 1.5 核实现有适配器实际 endpoint 的 reasoning 返回/重放矩阵：明文、签名、redacted、opaque 形态，同轮工具续接、跨用户轮次、重启与压缩依赖；版本化契约，不按品牌猜能力，不新增 Claude transport，未知/不支持显式报告。

## 2. 显式配置与 core-managed 状态库

- [x] 2.1 定义并严格解析必填 DB/source namespace、token、摘要、分页、session 清理和按需 image tool 限制；支持正整数及 k/K token 缩写，不补默认值，不添加本地媒体目录/下载器配置。
- [x] 2.2 校验 `target < trigger <= context_window - response_reserve`、固定开销、accounting profile、图片 URL reuse/refresh/期限/数量、TG 字节与请求体限制；拒绝旧 history_db_path/history_limit/history_ttl_days。
- [x] 2.3 实现仅属于 chat_llm namespace 的 SessionRepository schema，包含 session/epoch、journal、provider-native reasoning 及完整块关联、MessageRef、文字/marker 投影、attachment locator、游标、group control、action intent/receipt 和摘要；不存可检查的临时 URL/图片 bytes，opaque reasoning 按协议原样保存但不能作为图片缓存。
- [x] 2.4 将 LLM 数据库读写/事务/初始化全部接到 core DbManager、IDbConnection 和 BlockingExecutor，移除私有 sqlite3 连接和 C API 使用。
- [x] 2.5 实现 startup/reload 的 actor-owned schema 只读探测、显式 startup 初始化和 candidate restart-required；ValidationOnly 仅校验配置/profile/静态能力，不获取 DB 服务或读 schema，不要求 live gateway/BlockingExecutor、不私建执行器、不修改 core。candidate/validate-only 不建表、不解析图片、不摘要、不清理、不读取/删除旧 history.db。

## 3. 只读源关联与增量消费

- [x] 3.1 实现完整源 DB/namespace、platform、installation、conversation、message ID 与可选表 id 的 MessageRef，关联校验完整身份，SQL 值绑定且表名安全验证。
- [x] 3.2 实现同库 key JOIN 与分库批量只读合并，保留 raw/payload 重建和精确 topic 语义，不要求 ATTACH、真实外键或跨库事务。
- [x] 3.3 实现固定高水位 bootstrap、existing-id 分页与本地提交游标，区分 I/O page 和上下文上限，处理同 timestamp、延迟行与多页积压。
- [x] 3.4 实现缺失引用、源行变更、重复观察与 fingerprint 去重；不修复/回填源表，不把 insertion id 宣称为 revision feed。
- [x] 3.5 只冻结实际保留会话的最小文字/图片标识投影及 native locator，以本地事务提交引用/游标；源编辑/删除不改旧前缀，不复制完整 raw/payload 或临时 URL。

## 4. 输入编排与持久 journal

- [x] 4.1 将普通 handler 改为消费既有 MessageStored 并校验 exact scope，保留 typed command handlers，移除 RawMessageEvent 第二次完整历史采集。
- [x] 4.2 将已授权 ChatCommand 保存为允许无源引用的本地用户输入，保留 Consume 和参数投影；不再过滤上一轮 command 提问。
- [x] 4.3 为 conversation/activation/in-flight/decline/cursor 补齐 installation/topic 隔离，可靠保存 busy command 与未消费普通输入，不丢提问或提前推进游标。
- [x] 4.4 实现确定的持久追加序列、固定 system/tools（含 view_image）和末尾当轮模式策略，移除消息数裁剪及按文字误去重；区分 durable-prefix 与临时 visual suffix。
- [x] 4.5 保留 trusted visible user/message/image ID 校验，拒绝 direct decline、跨 scope lookup/quote/view_image 及由文本/摘要伪造的附件。

## 5. 工具轨迹、恢复与群级控制

- [x] 5.1 持久化 assistant、call IDs/arguments、lookup 和 terminal send/decline 结果，补齐 not-executed 调用；view_image 结果只存 image ID/状态/当轮可用说明。
- [x] 5.2 实现通用 reasoning 状态模型：明文 reasoning_content、签名 thinking/redacted blocks、opaque/encrypted continuation 均按 exact session/epoch/turn/ordinal 与 assistant/tool 状态事务性保存，保留内容、签名、phase、item/call IDs 与顺序；异步回调/重试不得跨会话串用。
- [x] 5.3 实现 intent/confirmed/ambiguous 恢复，禁止自动重发可能已执行操作，图像重试不能重复发送，不恢复悬空工具序列。
- [x] 5.4 出站回执仅存 LLM DB，按 exact provider identity 关联 self-echo，避免重复 assistant/触发；不补写 message-store，不生成假入站事件。
- [x] 5.5 将 toggle_think 拆为 `(platform, installation_id, group_id)` 群级持久控制，同群 topic 共享，其他群/bot 独立，只控制 decline 理由输出。
- [x] 5.6 保持控制确认/decline 理由的各自 topic 目标、turn 一致控制快照和兼容 restart/reload 恢复；不改变 reasoning/tools/topic 历史。
- [x] 5.7 每次工具续接、后续用户轮次、重试、兼容重启构建请求时按 1.5 的 endpoint 契约附带允许/必需的 reasoning；协议禁止字段只保留本地并报告限制，必需状态缺失/未知能力明确失败，不转普通文本、不静默丢弃。
- [x] 5.8 增加 provider/endpoint/model/prompt/tool/renderer/reasoning-protocol 指纹及 epoch 边界；不兼容切换不重放旧 reasoning，兼容恢复保持完整块，toggle_think 不改变 reasoning 生命周期。

## 6. 图片标识与按需查看工具

- [x] 6.1 提取 QQ/TG text/image segments，生成有序 `<image>稳定ID</image>` 和本地 attachment_refs，记录 native locator 与 exact scope，不持久化图片 URL，不对入站图片自动取图。
- [x] 6.2 实现固定 view_image tool schema 与执行器，校验 trusted image ID、scope、次数/数量/期限，并让纯图/多图遵守既有触发和权限规则。
- [x] 6.3 在 1.3 能力/范围确认后，通过 exact-installation typed QQ resolver 取得当前临时 URL；校验返回值与合法公网外链，不复用源 payload 旧 URL，不引入通用下载器或落盘 fallback。
- [x] 6.4 为 TG 实现同一 lazy tool 语义，使用既有 FetchTelegramFileRequest 和显式 maximum_bytes，仅在当前 turn 内存持有 bytes 并形成临时 data URL，不泄露 bot token。
- [x] 6.5 实现 request-local 视觉资源状态、明确 expiry/reuse deadline、有界刷新与失败状态；turn 完成/取消后释放，后续必须重新调用 tool，不建立本地图片缓存或媒体 GC。

## 7. Provider 视觉输入、预算与 usage

- [x] 7.1 将 view_image 结果关联为 OpenAI image_url/Codex input_image 真实视觉输入；tool role 不支持图像时生成临时 user 多模态条目，不能只返回 URL 文本或 silent text-only fallback。
- [x] 7.2 实现显式 tokenizer 与视觉 accounting profile：未打开 marker 仅按文字计费，实际 transient image 采用可信元数据或明确保守上界，不为计费预先下载 QQ 图片。
- [x] 7.3 每次 normal/tool/retry/summary 调用检查完整输入、重放 reasoning/opaque 协议开销、临时视觉尾部、输出 reserve 与请求体大小，canonical journal 不含可检查的临时 URL/bytes；opaque 开销按 endpoint/accounting 契约处理，不当普通文本计数、不把未知值当零，单条或完整轮次超预算明确失败。
- [x] 7.4 只发送核实支持的 output-limit/image-input 字段；外链不可访问、视觉不支持或 context-limit 错误明确且有界处理，不擅自加本地缓存或无限重试。
- [x] 7.5 解析 input/output/cached-token usage，区分 unknown 与 0；分别记录 durable-prefix 和视觉尾部变化。日志整体去除所有 reasoning，不泄露原始错误响应；持久化保留协议有效 reasoning，仅清理可编辑字段中的临时 URL/bytes/凭据。签名块的可检查回显冲突明确失败，opaque 块不改写且不宣称内部脱敏，不重放已执行发送。

## 8. 摘要压缩与原子 epoch 切换

- [x] 8.1 实现 token trigger/target 驱动的完整旧轮次选择，保留近期轮次/current input/未终结动作，低于 trigger 不裁剪或摘要。
- [x] 8.2 实现受限无工具摘要及完整轮次分块，使用持久文字、既有图像观察与 markers，不自动展开历史图片；校验摘要携带的 image IDs 来自可信附件集合。
- [x] 8.3 重新计费候选 summary + retained tail + pending input，在本地事务提交冻结摘要、epoch、边界和允许保留的 attachment_refs，失败不提前释放旧引用。
- [x] 8.4 实现失败/取消/过大摘要的原状态保留及明确错误；不滚动删头、不偷偷刷新持久图片内容、不降低保留要求，只有仍符合硬预算才允许未压缩请求。
- [x] 8.5 压缩按 reasoning + assistant/tool 完整依赖单元划界，近期尾部原样保留，旧前缀 reasoning 与新 epoch 活跃请求显式分离并原子退役；摘要不把原始 reasoning 转普通文本、不处理密文、不冒充续接块，依赖不能满足则失败并保留旧状态。

## 9. 配置、依赖与文档

- [x] 9.1 删除旧 MessageRepository/messages_v2 使用及私有 SQLite 依赖，声明实际 tokenizer 与既有 message-store runtime 依赖；不添加 QQ HTTP 下载器、媒体缓存或共享存储库依赖。
- [x] 9.2 更新配置示例，展示 core-managed 同/分库、只读源绑定、MessageStored 链路和必填 token/image-tool 限制，128k 和 URL 有效期均不得成为隐式默认值。
- [x] 9.3 更新功能/provider 文档，说明 marker → view_image → QQ 短期 URL / TG 内存图像、当轮视觉输入、稳定持久前缀的边界、群级 toggle_think 与恢复不重发；补充 reasoning 重放能力矩阵、会话隔离、签名/opaque 原样保存、密文不可检查边界及压缩/切 provider 的生命周期。
- [x] 9.4 编写旧 history.db 直接弃用、人工删除和回滚步骤；不要求源 schema 迁移、message-store 重启或自动清库，SDK 变更仅按另外批准的范围处理。
- [x] 9.5 复核 diff 和依赖闭包，保持 message-store/DbManager 无改动；core/Bridge 改动仅限第 11 节已授权图片接口及消费迁移，单列 SDK 发布要求，不混入其他 core 改动，不自动归档/提交。

## 10. 隔离验收与证据

- [x] 10.1 使用匹配的冻结 SDK 和 actor-only helper 编译，至少 6 workers、低负载全部 CPU；运行可用既有测试，不新增单元测试、不部署或替换 live core。
- [x] 10.2 在隔离 DB 验证同/分库关联、主键冲突、topic/installation 隔离、分页重试及源编辑/删除，确认没有源 DDL/DML/pin/级联约束或第二套 raw/payload 归档。
- [x] 10.3 使用受控 LLM 验证超过旧 100 条仍为 durable 追加前缀、command 跨轮保留、模式切换、完整工具轨迹、自回显幂等、busy 输入与恢复不重发；不把视觉尾部变化误报成持久历史重写。
- [x] 10.4 验证群级 toggle_think 同群跨 topic、跨群/bot 隔离、重启恢复与发送目标，确认 reasoning/tools/topic 历史不变。
- [x] 10.5 验证未调用 view_image 零取图、调用后平台解析/真实多模态输入、历史 locator 重新获取、过期有界刷新、跨 scope/伪造标识拒绝、TG 字节限、provider 外链失败及无临时 URL/bytes 落库或落盘。
- [x] 10.6 验证整数/128k、缺字段/非法值、marker 与实际 image 计费差异、未知尺寸保守预算、摘要阈值/失败回退、摘要保留可信 image IDs 与新 epoch 的 durable-prefix 稳定。
- [x] 10.7 验证 ValidationOnly 无 actor DB 调用、schema 缺失/不兼容不影响纯配置校验且静态非法配置仍拒绝；startup/reload 才探测 schema，candidate 不初始化。区分 core CLI 既有 DB 打开行为，不新增 core 改动。验证准备/校验不解析图片/摘要/删除旧库，并检查 turn 取消释放临时视觉状态和日志/模型回显脱敏；真实 cache usage 未实测则明确标注。
- [x] 10.8 整理实现、验收、未验证能力和上线显式参数；确认 message-store/DbManager 不变、core/Bridge 改动仅限已批准范围，源码完成不等于部署或旧库已删除。
- [x] 10.9 使用既有验收入口/受控集成验证明文、签名和 opaque reasoning 的存储与请求序列：跨群/bot/topic 并发隔离、工具续接、后续用户轮次、兼容重启、不兼容切换、协议禁止/未知/缺块失败、完整块压缩及原子回退、toggle_think 独立、日志无内容与签名回显冲突；fixture 证据与真实 endpoint 能力验证分开记录，不新增单元测试。

## 11. 已批准的 core 图片接口与 Bridge 迁移（先于 6.3 交付）

- [x] 11.1 核实 OneBot 11 get_image 标准及 LLOneBot upstream 具体 commit 的 file 标识、download:false、URL 返回及过期边界；撤销 Lagrange 前提，同步 scope/跨仓规格所有权，现场兼容性不冒充已验收。
- [x] 11.2 在 core 定义无鉴权的稳定 QQ 图片 locator、提取/校验逻辑与 typed request/result；拒绝任意 URL/本地路径和凭据进入可持久化 locator。
- [x] 11.3 复用 OneBot 11 get_image serializer，增加显式 download 参数及 typed action/endpoint/registry/client，严格区分服务端路径与 LLOneBot URL/元数据扩展并校验请求关联；不管理 rkey、不猜 TTL、不绕过网关。
- [x] 11.4 将 Bridge 重复的 URL 解析、直连 GET/Range 和有界 body 接收到 core 公共图片 HTTP 接口，期限/字节上限显式传入；不转移 actor 批处理重试/占位/归一化策略。
- [x] 11.5 将 Bridge 图片探测/下载/GIF 探测改用 core 接口、native QQ 图片按 exact installation 调用 typed resolver，删除未使用的旧下载模板，保留 TG typed 下载和既有发送/占位语义。
- [x] 11.6 导出新公共头及 SDK 依赖，更新 core/Bridge 接口文档和依赖声明；配置不新增隐式默认值。
- [x] 11.7 在非 live 独立目录编译 core/SDK、运行已有相关验证，再安装冻结到新路径并校验完整性，不覆盖 sdk-user-info 或 live core。
- [x] 11.8 使用新冻结 SDK 和 actor_build.py 至少六并行编译 Bridge/chat_llm，验证取图失败、字节限、安装隔离与缺失新操作的明确错误；不新增单元测试、不部署或重启。
