## 1. 审核与有效基线

- [x] 1.1 审核 design 与 actor-migration.md：保留已认可的公共接口方向，确认动画/样式、相册/媒体身份、输出 details、调用方最终路径及跨安装 bytes 路径；有变动先同步 proposal/design/specs。（用户已批准并要求 subagent 并行实施；最新确认撤销 SDK host-only/共享文件注册/路径授权门槛，不改变其余已批准边界。）
- [x] 1.2 核实 `decouple-bot-platform-contracts` 和 `auto-detect-chat-llm-addressing` 的实际完成状态，列出依赖和规格同步顺序；不得把前序未完成验收勾为完成或用旧主规格覆盖新实现。（独立 scout 核对当前代码/前序产物日志，剩余发布任务及已退休 bundle 机制记入 implementation-status.md；未改前序勾选。）
- [x] 1.3 按 actor-migration.md §1 在五个 actor owner 仓库承接迁移任务，纳入 ExHentai probe 和 registry 发布元数据；记录各自保留/替换/扩展/阻塞清单，不将 actor 业务规格搬回 core。（各独立 actor 工作树已有 docs/domain-sdk-migration.md；registry authority 核实为独立 local_actor/actor-registry，core 仅为 conformance mirror；业务实现/发布仍另行验收。）
- [x] 1.4 记录原 self cache 的生命周期、配置审批范围、QQ login 缓存投影和 remaining verification；确认不新增默认值、不复制缓存、不移动 actor 响应策略；新媒体预算须有明确配置/推导来源。（保留 packaging/actors/self-identity.md 的有效基线与唯一已批准默认项；新契约仍需重验。）
- [x] 1.5 审核 Bridge 多输出 ledger 的独立数据迁移范围、历史不完整证据和回滚方案；核实实施时 schema 版本，不将本次审核视为在线迁移/发布授权。（用户批准方案；当前基线 schema 3，在线迁移仍未授权。）

## 2. 统一身份与 codec

- [x] 2.1 定义 `ConversationId/UserId/MessageId` 强类型值及仅含 installation/id 的 `ConversationRef`，以及统一 `UserRef/MessageRef`；公共引用不暴露 thread/topic/native_group_id。（SDK 值/codec 增量已独立复核，所有已报 finding 关闭；25 个既有编译检查通过。native codec/installed SDK 仍另行验收。）
- [ ] 2.2 固定内部 ID 编码版本、规范化、长度边界及转义/无歧义规则，记录群/私聊/同群多话题/多 installation 的持久化样例。
- [ ] 2.3 在 QQ/TG 模块实现已注册的 native-to-domain/domain-to-native codec，拒绝非法子会话和错误 installation；通用层不增加平台 switch。
- [ ] 2.4 将现有配置目标和 command scope 通过集中解析边界映射成新引用，保持群范围与 exact-topic ACL 的区别，不要求用户手工拼接编码 ID。
- [ ] 2.5 实现集中 legacy source/reference 读取转换，保留原存储键、pair/session/destination/去重身份；缺失 topic/媒体证据明确不可用，不猜测、联网补全或覆盖在线记录。

## 3. 消息与有界媒体值

- [x] 3.1 实现 `RichText/Text/TextStyle/Mention` 和有序 Image/Animation/Video/Audio/File、显式 caption 与校验；静态/动画及样式不能静默混用，拒绝原生 JSON 发送路径。（新公共请求仅接收 typed body，值校验/有界 codec 复核通过；平台转换、能力拒绝与旧入口退休仍在 6.1、7.2–7.3、14.3。）
- [ ] 3.2 实现 `MediaRef/MediaIdentity/BytesSource` 所有权、稳定缓存身份与 binary gateway codec；禁止逐字节 JSON 数组、跨安装复用及用临时 URL 冒充稳定身份。
- [ ] 3.3 实现调用方最终路径 `LocalFile` 与所选 `HttpSource` 的原生适配及实际读取/缓冲边界；移除 SDK host-only、mapping、共享文件注册及统一路径授权门槛，保留协议/凭据/既有重定向限制，未知 provider 文件大小不当零或冒充已验证。
- [ ] 3.4 实现显式 `SendLimits`、准备内存/字节及输出数量限制；读取过程中执行限制，逻辑 deadline 不随拆分步骤重新开始。
- [ ] 3.5 实现 `ReadMediaRequest/MediaBlob` 两平台适配和可用 typed 元数据；逐项核对 actor 所需媒体，不以图片最低能力替代完整迁移，凭据/签名 URL/服务器路径不外泄。

## 4. 公共客户端与 self

- [ ] 4.1 添加共同 `BotClient` facade 和 actor/command 服务注入；内部复用 gateway/registry，普通客户端不依赖 QQ/TG DTO 或要求用户组装 envelope。
- [ ] 4.2 将 `GetSelfRequest` 接入 `BotClient::get_self`，保留 `bot.self.get`；把 SelfInfo 转为同型 UserRef 与 optional profile，统一 username 比较编码并记录旧字段迁移。
- [ ] 4.3 复用并迁移现有 SelfIdentityCache publication/read 边界，保证 cache miss/失效/关闭立即返回 CacheUnavailable，读取不触发或等待 provider I/O。
- [ ] 4.4 保留原刷新合并、连接/账号代次隔离、同代失败保留、成功 reload 后刷新、validation-only/失败候选无副作用及 TG is_bot 校验。
- [ ] 4.5 迁移 SelfClient 使用点和文档；若保留 QQ login-info 专属兼容入口，仅投影同一缓存，不回到 provider 查询，不新增 is_self/关系判定服务。

## 5. 成员、引用与能力查询

- [ ] 5.1 实现 GetMemberRequest/MemberInfo 两平台公共基础视图，adapter 负责显示名及可用角色映射；未知值保持未知，原生特有资料留扩展。
- [ ] 5.2 实现 ReplyRef/ReplyInfo 和显式 LocalOnly/AllowProviderLookup 请求；验证真实 source-target 引用证据，优先本地作者/回执，不接受任意文本指定 get_msg。
- [ ] 5.3 整合 QQ 严格 get_msg 和 TG 事件/缓存证据，有界合并同引用查询，缺失或跨作用域证据明确不可用；不递归或给历史页自动逐条联网补查。
- [ ] 5.4 从同一 installed descriptors/adapters 发布 typed 操作、样式/媒体来源/格式组合和已知限制，保持本地无 I/O、无凭据；动态权限未知，专有扩展不冒充公共支持。

## 6. 发送计划、回执与删除

- [ ] 6.1 实现 message.send binding 和完整内容预检；原生地址、CQ/entities、multipart、native batch 均在 adapter 内，actor 展示/占位/转码不得偷偷下沉为 SDK 默认降级。
- [ ] 6.2 实现显式 SingleMessageOnly/AllowSplit、有序计划与首条引用；TG 单图/2–10 图分组和 11 图尾批均合法，按输出消息数计预算，不丢图、不重排或暗改 caption。
- [ ] 6.3 实现 SendOutcome/SendFailure 的完整 gateway codec 与 output_details，保留已证实关联/媒体引用和未知提交；关联未知不丢确认 ID，遇首错停止，有任何输出或可能输出时禁止整批重试。
- [ ] 6.4 实现 unified MessageRef 删除的 QQ/TG 私聊/群聊/话题路径及完整结果作用域验证，旧私聊回执不再需要伪造 GroupTarget。
- [ ] 6.5 实现逻辑成功与含输出关联的物理 confirmation 独立通知，沿用 process executor admission/close/drain；通知失败不改变 provider 结果或重发。

## 7. 强类型接收与专有扩展边界

- [ ] 7.1 添加 MessageReceived/Sender/ReceivedBody/ReplyInfo 和有完整会话作用域的 MediaGroupRef；相册保留每个源 MessageRef，不伪造完整成员数，原始归档独立。
- [ ] 7.2 在 QQ/TG adapter 统一文本/样式、真实提及与 caption entities，处理 UTF-16 和 username 规范化；区分 known-user/unresolved/everyone，普通文字不产生证据。
- [ ] 7.3 接收附件提供 MediaRef/可选 MediaIdentity 与有界元数据，保留 UnsupportedContent/顺序和真实 sent_at/received_at；为 sticker/video-note/card/forward 提供受控 typed 扩展视图。
- [ ] 7.4 统一可获得的编辑/删除身份及 external reply 标记，不虚构缺失原生事件；普通路径不依赖 TG self_id=0 或原生 sub_type。
- [ ] 7.5 实现 OneBot typed forward nodes 与调用方最终文件路径的原生序列化，撤销共享文件预注册/SDK mapper 前置条件，保留 actor lease/owner 责任；actor 不拼 CQ JSON，不把专有操作塞进公共客户端。

## 8. Command 与 Message Store 迁移

- [ ] 8.1 迁移 generation-owned command/help 收发到共同客户端与 ConversationRef，保留 ACL/availability 和无副作用 preparation。
- [ ] 8.2 按附件 §4 改造 Message Store extraction/admission 的归档输入，保留 raw/payload、旧唯一键、namespace 和重复/编辑判断；不新增无关 BotClient/self 调用。
- [ ] 8.3 升级 actor_messages.hpp 的 MessageStored 为旧 source identity + typed MessageReceived 的一次通知，保留失败/headers/command processed/correlation/causation；同步 Bridge/Chat LLM reader 及其 message-store actor 依赖版本。
- [ ] 8.4 在副本验证 legacy decoder 与新通知不会重建存储身份或重放历史；运行时投影不隐式重建消息表，需要持久列变化时在 owner 仓库另列迁移。

## 9. Bridge：入口、文字与图片

- [ ] 9.1 按附件 §3.1/3.9 迁移 bridge_actor、event adapter、runtime 与 bot operations；完整目标引用保留 pair/群/topic/command scope，RawMessage/confirmation 只观察不重复转发。
- [ ] 9.2 迁移 QQ/TG 文字、发送者样式、caption、at/everyone 的 Bridge 展示，普通显示名用 get_member；跨平台用户无验证映射时不伪造 ping。
- [ ] 9.3 按 §3.3 共用单图/多图/forward-node 图片准备器，改为 read_media→bytes；保留逐项占位、尺寸安全、归一化、配置及调整/替换统计，不回退原事件 URL/远端路径。
- [ ] 9.4 迁移多图输出至显式 body/caption/AllowSplit，核对 1/2/10/11/21 图、混合内容、长 caption 和全部 output_details；移除只取 messages.front 的映射行为。
- [ ] 9.5 迁移 TG photo/普通附件读取和调用方目标最终路径传递，保留 PathManager 路径安全、blocking executor 与覆盖完整提交的临时资源生命周期。

## 10. Bridge：音视频、专有内容与相册

- [ ] 10.1 按附件 §3.4/3.5 迁移 record/audio/voice/video/document/file，保留 kind、filename/caption、来源文件解析和明确失败；video-note 仅经 typed 扩展显式转换。
- [ ] 10.2 按 §3.2/3.6 迁移 face/mface、GIF/MP4/WebM animation 与各类 sticker；保留转换/缓存策略，TGS 和不可播放原格式逐项标未验证/不支持，不新增隐式解码依赖。
- [ ] 10.3 迁移 QQ card/miniapp/forward typed 扩展视图和节点渲染；forward 图片使用同一准备器，文本及各批图片进入同一 logical delivery，首错停止，不吞异常报成功。
- [ ] 10.4 迁移 media_group_buffer 的 scoped MediaGroupRef、成员顺序/超时/去重/延迟 flush；保留每个成员身份与完整输出关联，不混安装/话题。
- [ ] 10.5 迁移 reply、typed edit/可获得的删除通知和管理命令；按 provenance/完整映射操作，保留专有强制刷新/头像/贴纸/topic 能力，明确 TG 删除事件入口未完整实现。

## 11. Bridge：多输出 ledger、重试与数据迁移

- [x] 11.1 按附件 §3.8 在 owner 仓库定义下一版 schema（当前基线 3→4）的 delivery/source/output/provenance/retry 模型与约束，分开引用锚点和反向 primary owner，不循环覆盖 v3 单输出槽。（隔离 Bridge storage seam 已实现；独立复核问题修复后全部关闭，20-worker 前序 SDK 编译通过。SDK binding/forwarding/retry 整合与 DB 运行验证仍在 11.2–11.5，不代表可启用 v4 forwarder。）
- [ ] 11.2 实现完整成功/部分/未知结果及 confirmation 的幂等持久化，记录全部确认 MessageRef/媒体引用，提交前持久化 attempt；只在全成功且落库后发逻辑成功。
- [ ] 11.3 迁移 retry payload 为版本化 typed body/引用/预算/媒体 lease；仅零确认且零未知、retryable+DefinitelyNotSubmitted 才整批重试，不隐式新增断点续发。
- [ ] 11.4 在脱敏副本执行 v3 导入与恢复演练；保留旧 keys/成员/primary，标记历史输出集合不完整，缺 topic/歧义 retry 隔离；DB 写失败/崩溃 submitting 不自动再发。
- [ ] 11.5 验收共享文件及缓存的 lease/清理覆盖所有输出、取消和可能提交；补齐必要显式配置和匹配备份/回滚文档，不触碰在线数据库。

## 12. Chat LLM 迁移

- [ ] 12.1 按附件 §5.1 迁移 session_types/source_message_reader/actor_session 与 MessageStored 准入，删除业务 native segment/locator 解析，保留 session/observation/sidecar/image_id 和历史未知证据。
- [ ] 12.2 迁移 actor_addressing 到 BotClient::get_self 与 typed mention/reply，保留本地证据优先、lookup policy、稀疏标签及 direct/declinable 策略，不改 prompt。
- [ ] 12.3 迁移 view_image/resolve_visual 到 read_media；用必填 image_read_max_bytes 替代 TG-only 读取预算，保留 MIME/静态格式、刷新/复用/总请求边界，bytes/data URL 只在本 turn 易失状态。
- [ ] 12.4 迁移 get_user_info 到 get_member，send_message/send_tracked_text 到 scoped reply + SingleMessageOnly；保持工具授权/名称、文本上限、intent/claim/去重及失败/未知恢复。
- [ ] 12.5 在历史副本及现有回放核对相同 session/image_id/sidecar/已完成发送，self miss 零查询、无原生媒体 URL 外泄、无跨 turn 自动图片授权或重复发送。

## 13. ExHentai 与 QQ forward probe 迁移

- [ ] 13.1 按附件 §6.1 迁移 gallery_presenter 的普通文字/TG 封面/topic 到 BotClient，保留 DestinationId 持久身份、trusted hosts/caption 限制及仅确定未提交的允许文字 fallback。
- [ ] 13.2 迁移已审查 QQ 图片到调用方最终路径、qq_gallery_forward 到 typed nodes；保留 explicit mode 开关、审查/受限内容政策、节点配对及 99-node/包络限额。
- [ ] 13.3 按 §6.3 用独立 SingleMessageOnly 调用保留 direct 的逐条继续/取消策略，分别累计 confirmed/failed/unknown/unsent；不改自动 Delivered/Uncertain/全拒绝重试或手工无重试规则。
- [ ] 13.4 保留 SharedMediaStore owner/lease/磁盘预算/retention 与批次准备/claim 时点，覆盖全部调用 settle；不随 SDK 升级改 namespace schema、订阅/evidence/去重键。
- [ ] 13.5 更新 tools/qq-forward-probe-actor 的 ConversationRef、typed forward、反馈发送及独立 package/schema/SDK 构建；只构建与离线检查，不运行真实 QQ 探针。

## 14. Actor template 与消费者清理

- [x] 14.1 按附件 §7 更新 template 的 SDK/package/input schema 与说明，保留 V2 导出、run_blocking/emit/命令完成/Consume 示范，不新增无意义网络行为。（隔离模板 package 0.2.0 / SDK 2.x / input schema 3 已通过现有 metadata validator，src/ 相对 snapshot 不变；冻结 SDK actor build 仍在 16.2。）
- [x] 14.2 更新最小 typed 原会话回复文档示例；不展示 CQ/file_id/topic 拼接，不写新的单元测试。（README 示例保留原 MessageRef/ConversationRef，显式 caller limits、SingleMessageOnly 并返回完整 SendOutcome；既有 template-reply 编译输入已对最终 SDK 头复编译通过。）
- [ ] 14.3 扫描所有 actor 的普通路径，确认原生段 JSON、SelfClient、旧 group/private/topic 发送入口已退休；真正专有扩展与集中 legacy codec 留在明确允许的位置。

## 15. 安装 SDK 与版本门禁

- [ ] 15.1 更新公共与单平台 SDK 的安装/export/include 闭包；普通客户端/事件/self/媒体 API 不传递依赖平台 DTO，专有扩展只依赖所属平台。
- [ ] 15.2 将 generated actor contract 和 loader 门禁升级为 schema 3，保留调度 ABI V2；旧 schema 在 factory/prepare/注册前拒绝，保留命令及配置约束。
- [ ] 15.3 更新 SDK identity/version、全部五个顶层 actor 与 probe 的 package 要求和配套构建；不以 JSON 可序列化或 ABI V2 标签冒充二进制兼容。
- [ ] 15.4 更新 public migration/专有扩展文档，明确 self DTO、cache-only、opaque ID、完整结果、媒体行为变化和旧入口退休；记录 actor-owned 数据迁移的独立版本。

## 17. OneBot11完整标准SDK（用户追加授权；非标准扩展暂停）

- [x] 17.1 固定官方规范和完整标准消息段/API/事件清单，区分native/runtime/public typed支持与协议限制；明确全量SDK范围及标准优先顺序。（官方commit d4456ee7、20种segment、38个公开API、hidden/suffix及全部事件已核查；原始资料/URL/hash由父会话校验，用户明确确认全部移植。）
- [ ] 17.2 实现完整标准消息段及变体、类型化组合与三个send入口，保留顺序、引用、递归/展开边界和准确effects；provider-delegated URL/reference意图显式区别于core获取，不冒充远端字节保证，不扩展非标准forward发送。
- [ ] 17.3 实现完整标准查询/目录/能力API；新增明确的标准login查询使用同一identity coordinator，旧self/QQ兼容入口仍cache-only。get_msg/get_forward_msg独立selector与detached结果不得伪造会话或放大共同reply/历史恢复权限。
- [ ] 17.4 实现send_like、群管理、匿名禁言两种输入、好友/群请求审批及hidden quick operation；具名typed操作、真实上下文与权限/代次检查，不开放任意action/params JSON。
- [ ] 17.5 实现凭据、状态/版本、重启/清缓存标准接口；具备敏感结果、错误和全部日志路径的脱敏边界，区分读与mutation。仅实现与隔离验证，不读取真实凭据、不执行管理/重启操作。
- [ ] 17.6 实现全部标准message/notice/request/lifecycle/heartbeat子类型与字段；保留真实来源/时间/未知状态，核实兼容事件carrier和ABI/schema门禁，不破坏旧actor布局/持久身份。标准group_upload不等于批准文件消息/文件URL扩展。
- [ ] 17.7 实现全部标准API的_async/_rate_limited语义：受理不等于完成，消息和非消息副作用均保留提交不确定性，无伪造回执、盲目重试或状态补全。
- [ ] 17.8 实现标准媒体查询/显式record转换、真实接收URL证据的私有有界保留及读取；不猜provider路径本机可读，不泄露签名URL、不用失败后的原事件URL逃生，不改变调用方路径/lease责任或未经审查扩宽旧opaque codec。
- [ ] 17.9 汇总完整标准参数/变体/源码/运行接线矩阵，在新的完整core/SDK代次编译、链接、冻结和构建消费者；单列未执行的provider/runtime验收。不得以serializer存在、部分来源支持或compile成功冒充完整标准完成；标准实现完成前不询问/实施非标准新增改造。

## 16. 验收与发布准备（不新增单元测试、不自动部署）

- [x] 16.1 在隔离目录构建 core/SDK 并复用现有检查/测试；至少 6 workers，低负载全核。新写/改写单元测试另获授权，未覆盖项如实记录。
- [ ] 16.2 冻结目标 installed SDK，用 scripts/actor_build.py 隔离构建五个 actor 和 probe；核实公共/平台接口头闭包及完整SDK自动链接配套imported core、schema 3 接纳和旧 schema factory 未调用，不从在线 core workspace 构建 actor targets。
- [ ] 16.3 逐项执行附件 §3.10 的 Bridge 富媒体/寻址/失败/恢复矩阵及其他 actor 退出条件；复用现有样本/工具，保存实际差异、缺失能力和未验证格式，不以“编译通过”代替媒体验收。
- [ ] 16.4 在脱敏副本核对 legacy ID/ACL/session/去重、Bridge ledger/相册/引用恢复、ExHentai 终态及 MessageStored 次数；复验 self 生命周期/并发刷新/账号代次，不改在线 DB。
- [ ] 16.5 按当前 packages.toml/maintained repositories 更新依赖版本与 registry entries，准备匹配 core/SDK/actor/config/完整 DB 的备份回滚清单；不恢复已退休的旧 offline bundles/patch/replay。记录阻塞项，core 替换/重启、Bridge 在线迁移和真实探测另行授权，不以 actor reload 激活不兼容变更。
