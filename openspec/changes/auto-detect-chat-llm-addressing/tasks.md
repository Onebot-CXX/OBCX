> Revised by user: 共同自身身份能力使用统一 GetSelfRequest/SelfInfo；core 维护缓存，查询只读缓存，默认每 5 分钟刷新且可配置覆盖。是否本人、@/回复关系与标记仍全部在 chat_llm。此前平台专属自身 API 改动已撤回。

## 1. Platform API 契约

- [x] 1.1 核实现有 installation 生命周期、gateway deadline/cancellation 和 self 信息来源，记录具体接入点及可复用预算；不增加隐含默认值。
- [x] 1.2 新增统一 bot.self.get / GetSelfRequest / SelfInfo 与公共 client；两平台注册相同操作，返回缓存，后台分别调用现有 get_login_info/getMe，不改已有 QQ login DTO，其旧操作也改为同一缓存的只读投影。
- [x] 1.3 保持现有 QQ GetOneBotLoginInfoRequest 兼容，补齐 exact-scoped get_msg typed read-only 操作，验证返回 message ID/group/sender，禁止任意历史查询。

## 2. Installation 自身身份缓存

- [x] 2.1 添加按完整 installation/account generation 隔离的身份缓存与 actor 只读访问接口，保存 ID/username/display name 和 ready/refreshing/unavailable 状态，不保存凭据。
- [x] 2.2 接入 installation 可用、重连、账号变更、成功 reload 后刷新；新增 self_identity_refresh_interval_ms，采用用户批准的 300000ms 默认值并支持覆盖；合并并发刷新，拒绝旧 generation 的迟到结果。
- [x] 2.3 验证 validation-only/失败 reload 候选不改变线上缓存；API 错误按现有预算降级且不会每条消息重试，可靠 self_id 冲突会使旧身份失效。

## 3. 跨平台关系识别

- [x] 3.1 拆分旧 TriggerSignals 的关系证据和 direct intent；定义内部 confirmed/not-matched/unknown 状态，不直接输出三态 JSON 给模型。
- [x] 3.2 实现 QQ 原生 at 段 ID 比较，排除 all/他人/普通文字伪造；核实 CQ 字符串经 ingress 标准化后的等价行为。
- [x] 3.3 实现 Telegram mention/text_mention、caption 和 UTF-16 offsets，使用验证过的 username/ID；回复检测从 is_bot 改为 exact self ID。
- [x] 3.4 实现 QQ reply 的 exact local source/confirmed receipt 优先解析和受限 get_msg 补查，错误/缺失/跨群/跨 bot 返回 unknown，并合并相同引用查询。
- [x] 3.5 当前实时引用之外不批量联网补历史，不递归查引用链；所有关系解析先满足作用域权限，不让用户文本发起任意查询。

## 4. 稀疏模型标记与持久化

- [x] 4.1 在新 observation 的 pending/journal 中保存独立关系 sidecar；保持旧内容 fingerprint/seen/ref 身份算法，不修改原始 message_store。
- [x] 4.2 保证实时消息在同步 source 与通知入队的各路径上均正确携带一次关系快照，不能先以无标记历史投影去重而吞掉实时证据。
- [x] 4.3 统一 wire renderer：仅 confirmed 命中在已有 JSON 信封外添加 [at:self] / [reply:self]，普通消息 byte-for-byte 不变，不新增角色消息或 addressing 对象。
- [x] 4.4 在固定 protocol 中解释一次标记、正文边界、非强制回答以及无标记不代表否定证据；用户正文始终保留 JSON 转义。
- [x] 4.5 将相同 renderer 用于 provider 请求、token accounting 和压缩输入；批次内标记绑定原消息，摘要中的文字不成为运行时关系证据。
- [x] 4.6 显式升级 wire/protocol 兼容 fingerprint；旧 journal 无 sidecar 时保留原样，身份刷新不改历史、不制造编辑、不重发已完成消息。

## 5. 调度策略与配置迁移

- [x] 5.1 改为仅显式授权 ChatCommand 设置 non-declinable direct intent；已确认 @/回复只请求可 decline 的评估，普通消息继续受 proactive_enabled 控制。
- [x] 5.2 处理混合批次、并发 pending、旧 direct/direct_pending 状态：使用持久显式命令证据，保护 /chat 请求，避免旧 mention 强制回答。
- [x] 5.3 保持 group gate、命令 ACL、自身回声抑制、重复通知去重、单 session owner 与发送回执安全边界不变。
- [x] 5.4 退休 bot_username；三个旧字段只作类型校验和固定诊断，不允许 API 失败时回退到配置名称。
- [x] 5.5 更新配置示例、actor 功能/迁移文档及本地部署配置的退休字段；保留 local_config/llm/prompt.txt 的路径和内容，不自动 reload。
- [x] 5.6 记录与 remove-chat-llm-wake-gate 旧 direct policy 的替代关系，确保后续 spec 同步不会恢复 mention 必须回答的旧约束。

## 6. 构建与验收（不新增单元测试）

- [x] 6.1 在独立非在线工作区验证 core/SDK 构建；使用冻结 installed SDK 与 scripts/actor_build.py 验证 actor 构建，至少 6 个并行 worker，低负载时使用全部 CPU。（build/addressing-core + addressing-sdk-verified + addressing-actors-verified，20 workers，通过。）
- [x] 6.2 离线回放 QQ @自己/@他人/@全体/reply 和 TG mention/text_mention/caption/回复其他 bot，覆盖同名、多 installation、无效 offsets、引用失败和越权作用域。
- [x] 6.3 检查真实序列化请求：普通消息 addressing 增量为零；命中仅出现对应短前缀；用户伪造标签不能产生运行时信号；记录混合批次的实际 token 增量。
- [x] 6.4 用脱敏会话副本验证重复通知、历史 bootstrap、身份刷新、旧 pending direct 迁移、旧 journal、provider continuation 失效和发送去重；不改在线数据库。
- [x] 6.5 验证启动/成功 reload/重连/定时刷新、5 分钟默认值与显式 override、查询缓存 miss 不发网络请求、失败候选无副作用、并发刷新合并、账号变更拒绝旧结果。
- [x] 6.6 在隔离或经明确授权的环境验证 /chat 不可 decline、@/回复可以 decline、proactive_enabled=false 时普通消息不评估；记录证据及未完成项。
- [x] 6.7 提供匹配 core/SDK/actor 的部署与回滚步骤，明确 core 更新需单独授权和重启，本实施任务不自动部署在线服务。
