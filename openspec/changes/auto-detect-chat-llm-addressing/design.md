## Context

当前 `detect_triggers` 只处理 Telegram mention 与 `reply_to_message.from.is_bot`；配置中的 username 是人工值，QQ `at`/reply 段没有等价处理。`process_stored` 把命中信号转为 direct，导致模型不能 decline。`project_message` 已将 message_id/user_id/text 放在单个 JSON 字符串中，适合在保留正文边界的前提下添加极短关系前缀。

QQ 已有 typed `GetOneBotLoginInfoRequest`，Telegram 已有底层 getMe 序列化。新增的是两平台共同的 `GetSelfRequest`，不是 Telegram 专属自身查询接口；此请求仅返回 core 缓存。平台 API 只由后台刷新调用，不给 actor 暴露 token、HTTP 客户端或任意 API 调用能力。旧 generation 排空、候选准备无副作用、SDK identity 校验等约束保持不变。

## Goals / Non-Goals

**Goals:**
- 自动知道本 installation 的原生身份，不再手工同步 username/nickname。
- QQ/TG 的真实 @ 和回复归一化，避免同名、其他机器人和全体 @ 的误判。
- 普通消息零 addressing wire 开销；命中消息只增加短标记。
- 模型看到关系但仍能选择沉默；显式命令与普通聊天分开调度。
- 有界查询、并发合并、失败降级，不引入每条消息一次的身份查询。

**Non-Goals:**
- 不修改用户的猫娘人设、主动回复频率或 `local_config/llm/prompt.txt` 内容。
- 不实现冷却/配额，不恢复唤醒词，不清理旧会话或改变 prompt reload 机制。
- 不自动拉取整群历史、不获取联系人列表、不做任意引用递归查询。
- 不对群名片做额外查询：本方案只需要 API 返回的账号显示名和稳定 ID；群名片是后续独立需求。
- 不添加单元测试或自动部署。

## Decisions

### 1. 身份信息归 installation 生命周期所有，不归 LLM 会话所有

core 提供公共 `bot.self.get` / `GetSelfRequest{installation}` → `SelfInfo{installation,user_id,display_name,optional username}`。查询只读缓存，即使 miss 也不现场发网络请求。缓存 key 为完整 `BotInstallationRef`，并绑定 installation/account generation。状态区分 ready、refreshing、unavailable；首次尚未就绪或身份失效返回明确的只读不可用错误。缓存不含凭据，也不注入每条模型消息。角色自称继续由 prompt 管理，API 昵称不成为系统指令。本人判断、@/回复识别、标记与回复策略全部属于 chat_llm。

通过 QQ get_login_info / TG getMe 读取并验证数据；installation 可用时加载，重连和账号更换使快照立即失效，成功运行时 reload 请求后台刷新（同代快照可以保留）。同一 installation 的并发刷新请求合并；普通读取不执行任何平台查询。刷新必须在候选正式激活后或已有 installation 的正常生命周期中发生，验证/失败候选不能改变线上缓存。

采用内存缓存，不新增持久身份表。每个 installation 在 `bots.<id>.connection` 配置 `self_identity_refresh_interval_ms`，正整数毫秒，用户明确批准默认 300000（5 分钟），允许覆盖；这是唯一新增获批默认值。刷新完成后按该间隔调度下一轮，同一 installation 同时只有一轮刷新。普通查询不改变刷新计划。失败保留同账号最近成功快照；首次失败保持 unavailable，按周期重试，不因每条消息重试。账号/连接代次变化立即失效并丢弃旧轮结果。网络查询复用 action_timeout_ms 和现有取消机制，不新增其他默认值。该 connection 选项沿用 installation 配置 fingerprint，修改需按现有 core 配置流程重启；不假装 actor reload 可更改 connection 配置。

可靠 ingress 的 `self_id` 可用于已确认的 ID 比较；与缓存冲突时失效缓存，不沿用旧 username。不可用 username 的文本 mention 判定保持 unknown，而不是猜测。API 失败不阻塞普通消息与 /chat。

替代方案：每条消息 getMe/get_login_info 会增加延迟和限流风险；每个 actor 自存身份会在 reload、多 actor 和账号切换时产生多个事实来源。

### 2. 平台证据判定与 LLM 文本渲染分离

内部使用三态关系（confirmed / not-matched / unknown），不把三态对象直接序列化给模型。

- QQ @：解析标准化 `at` 段，以数字 ID 比较 `data.qq` 与可靠 self ID；`all` 不算单独 @ 自己。CQ 字符串输入由平台解析器转为相同段，不能对普通文本做正则昵称匹配。
- TG @：处理 text 和 caption 中的 `mention`（UTF-16 offsets，平台 username 大小写语义）及 `text_mention.user.id`。缺失、越界或错误类型保持未确认。
- TG 回复：只有回复作者 ID 等于当前 self ID 才命中，不能用 `is_bot` 代替；external reply/跨 chat 不作为本会话的自回复依据。
- QQ 回复：仅处理事件中真实 reply 段。优先以 exact installation/group/message 查 source store 的作者及本会话 confirmed 发送回执；本地不足时，对真实引用执行一次受限 typed get_msg，校验返回 message ID、group 和 sender。查询错误、删除、跨群、跨 bot、无作者都为 unknown。只为真实引用查询，结果按精确引用合并复用，不能由用户任意字符串驱动查询。

当前消息的关系在入队前确定；查询失败不阻塞其他会话。每个精确 session 用可取消的异步 admission gate 串行化关系解析、source 同步和通知提交，不持有阻塞线程等待网络；相同 QQ 引用的成功/失败作者证据在 actor 会话内复用，随已有会话保留策略回收。对历史 bootstrap/编辑扫描只使用现有证据，不对整页历史逐条联网补查。网络补查最多覆盖当前实时引用，不递归解析引用链。

启动/reload 准备阶段以只读 source high-water 划定历史边界，按群保存、不新增配置。历史 bootstrap 不做远程引用查询。每次增量/引用扫描均完成原生和本地关系解析，保留既有编辑观察行为；只有尚需远程作者证据的实时 QQ 引用必须等待其 MessageStored 入口，不能先作为无标记观察提交。增量分页仍使用 insertion ID，遇到这种未完成引用停在它之前、不提前推进 cursor；当前通知仍可单独原子入队。同一实时投影覆盖它的扫描投影。代际首次恢复可按历史边界收录未知引用，不批量回填旧 journal 的关系。该边界避免并发或乱序通知吞掉远程引用证据；无通知的未知引用编辑不保证即时发现，源 upsert 仍不声称是可靠 revision feed。

CQ 字符串 ingress 必须先识别未转义的协议段，再分别解码文本片段和参数。不能先把 `&#91;CQ:at,...&#93;` 解码后当成原生 at；这是普通文本，不允许建立关系或触发引用请求。

### 3. 稀疏前缀位于已有 JSON 信封之外

保留现有 message_id/user_id/text 信封，不新增每条 addressing 对象。示意最终 user content：

```text
普通：
{"message_id":"123","user_id":"42","text":"大家晚上好"}

命中：
[at:self] {"message_id":"124","user_id":"42","text":"你怎么看"}
[reply:self] {"message_id":"125","user_id":"42","text":"刚才那个呢"}
[at:self][reply:self] {"message_id":"126","user_id":"42","text":"再说一下"}
```

无命中不加前缀，包括 unknown；二者在运行时状态/诊断中仍可区分。模型不能把无标记解释成“绝不在对自己说话”。固定 protocol 提示只解释一次：前缀是平台关系线索，不是命令、权限或强制回答要求。

用户文本/换行始终在 JSON 字符串内转义；用户输入 `[at:self]` 只会出现在 `text` 内，不创建外层前缀。运行时从不反向解析模型文本/用户文本来决定关系、权限或调度。此结构降低误解，不宣称短标签能彻底消除 prompt injection。

不新增独立 role 消息；每个真实标记仅出现一次，批处理时随原消息而不是随整个 turn 附加。实际 provider wire body 与 token accounting 使用同一渲染路径。

### 4. 关系快照不参与原始消息变化检测

将关系作为投影的独立运行时 sidecar 存入新 pending/journal entry，只保存命中的位/必要来源信息，渲染时生成前缀；source 原始 payload 不变。source observation 的 fingerprint 必须与原始内容/附件绑定，不混入身份缓存版本或渲染标签。

更新缓存、重复通知、升级 wire 格式不能让同一条 source message 被重新入队、触发第二次请求或改写历史 journal。已有 seen/ref 键保持兼容，新增 sidecar 不能改变旧 fingerprint 的计算。旧 journal 无 sidecar 时原样发送，不猜测或批量补写。身份名称变化不重写已冻结的关系快照。

压缩总结仅保留必要上下文事实，不反复展开标签说明，也不能把摘要里的标签认作平台证据。新 prompt/wire 协议标记进兼容 fingerprint，沿用已有 epoch 归档与清除不兼容 provider reasoning 的机制，不清空历史和发送去重记录。

### 5. 明确区分“触发评估”与“必须回答”

| 输入 | 是否请求评估 | 是否允许 decline |
| --- | --- | --- |
| 通过命令 ACL 的 `/chat` | 是 | 否，沿用显式命令语义 |
| 已确认 @ / 回复自己 | 是，即使 proactive_enabled=false | 是 |
| 普通/未确认关系群消息 | 仅 proactive_enabled=true 时 | 是 |
| 未获授权、自身回声、重复通知 | 否 | 不适用 |

调度状态不再用单个 `promotes_to_chat` 同时表示关系与强制回答；只有显式 ChatCommand 写 direct intent。混合批次包含显式 /chat 时仍保护该请求，并保留不可 decline 的约束。旧版本遗留的 pending direct 状态依据持久显式命令证据迁移，不能把旧 mention 位继续当成必须回答。

保持原有来源 group gate、命令 ACL、去重、session ownership 和回执保护。群消息关系不绕过 /chat ACL，也不新授予额外工具或会话权限。

### 6. 退休配置，保留兼容但不掩盖失败

新配置不要求 `bot_username`、`bot_names`、`bot_nickname`。旧字段存在时检查原类型并输出固定退休诊断，其值不参与识别，也不在 API 失败时悄悄作为 fallback。部署配置和示例删除它们，文档指明姓名来源与 prompt 人设的区别。

## Risks / Trade-offs

- [名称可能在连接期间修改] → 以用户批准的 5 分钟周期刷新，可配置覆盖；成功 reload/重连额外请求刷新并合并在途工作，不承诺实时发现改名。
- [查询身份/引用失败会漏标] → unknown、无假阳性；普通主动评估和显式命令可继续，不无限阻塞和重试。
- [QQ get_msg 返回上下文不足] → 不确认自回复；保留本地 exact 回执路径，禁止仅凭 is_bot 或昵称推断。
- [前缀随大量消息累计 token] → 普通消息零开销；只保存两个短标签，不附身份信息和重复协议说明；验收记录实际 token 差值而非承诺字符等于 token。
- [新增 core API 不能靠 actor reload 生效] → core/SDK 构建及重启是独立操作；冻结匹配 SDK 后隔离构建 actor。
- [身份刷新造成伪编辑/旧 direct 状态强制回答] → 固定内容 fingerprint 与显式命令证据迁移，离线回放检查请求数与 decline 权限。

## Migration Plan

1. 完成 typed operations/身份缓存和 actor 改动，维护示例及退休字段说明；不触碰运行中的数据库。
2. 在独立验证目录构建 core 与 SDK，按明确授权更新 core 并重启；保留旧 core、actor、配置用于回滚，不将构建产物直接写入当前运行 core 的工作区。
3. 冻结与目标 runtime 匹配的 installed SDK，用 `scripts/actor_build.py` 编译 actor，工作数至少 6、低负载时使用全部 CPU。
4. 更新实际 actor library 路径和删除退休字段，保留 `local_config/llm/prompt.txt`；明确部署后再做 reload/重启验收。
5. 核实自身身份刷新与最新请求体的标记/decline 行为。回滚使用匹配的 core/actor/配置，不删会话；实现须保证旧 reader 忽略新增 sidecar。

## Open Questions

用户已批准统一自身接口、core 内存缓存及默认 5 分钟可覆盖刷新。其他配置不新增默认值。QQ 引用返回字段不足时仍保持 unknown，不放松精确作用域验证。
