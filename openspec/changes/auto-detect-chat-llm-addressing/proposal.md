## Why

`chat_llm` 依赖手工填写 Telegram `bot_username`，遗漏 QQ 的 @ 检测，并把回复任意机器人的消息误判为回复自己。需要自动获取当前 installation 的真实身份，将平台关系转换成低 token 开销的模型提示，而不是重复注入身份对象、布尔字段或把 @ 强制等同于必须回答。

## What Changes

- 新增统一 `bot.self.get` / `GetSelfRequest` / `SelfInfo`，QQ 与 Telegram 共用相同请求/结果；查询只返回 core 按 installation 维护的缓存，不触发网络请求。QQ 内部调用 get_login_info，Telegram 内部调用 getMe。
- core 在启动后及每 5 分钟后台刷新身份；用户明确批准唯一配置默认值 `self_identity_refresh_interval_ms = 300000`，可在各 bot installation 配置中覆盖。缓存尚未就绪/失效返回明确错误，刷新失败可保留同账号的最近成功快照。
- 按平台结构识别 @ 和回复，使用真实 ID/username，不使用昵称子串匹配。QQ 回复优先查本地精确消息记录/发送回执，必要时使用受限的 typed `get_msg` 查询。
- 给命中的消息增加 `[at:self]`、`[reply:self]` 短标记；普通消息不增加任何 addressing 字段、false 值、独立消息或身份信息。
- 标记位于现有消息文本 JSON 信封之外，由运行时构造；用户正文保持 JSON 字符串边界，不能伪造调度关系。协议说明只在固定提示词中出现一次。
- **BREAKING**：已确认的 @/回复仍可触发评估，但允许 `decline_reply`；只有显式 `/chat` 保留不可主动拒答的 direct 语义。普通消息仍受 `proactive_enabled` 控制，所有入口保留现有 ACL。
- 退休 `bot_username` 手工配置；与已退休的 `bot_names`、`bot_nickname` 一样，旧值仅做类型校验并警告，不作为身份来源。
- 除用户明确批准的身份刷新间隔外不增加配置默认值；不改模型人设、不清空会话、不改原始 message_store、不部署或自动重启。

## Capabilities

### New Capabilities

- `bot-self-identity-cache`: 统一只读 API、精确 installation 隔离、5 分钟可配置刷新、失败状态和并发合并的自身身份缓存。
- `chat-llm-sparse-addressing`: 跨平台 @/回复归一化、稀疏短标记、非强制回答语义，以及历史/去重兼容。

### Modified Capabilities

- `qq-telegram-bot-contract`: 为共同自身身份能力提供统一 typed operation；QQ 精确引用查询仍是平台特有扩展，不假装 Telegram 支持任意历史拉取。

## Impact

- Core/SDK：公共自身信息请求/结果与缓存、两平台 operation 实现、配置/recipe/transport 生命周期；QQ 原生 get_msg 的 typed 出口。不在 core 判断 is_self 或生成 LLM 提示词；这些逻辑全部在 chat_llm。
- Actor：`local_actor/chat_llm` 的配置、触发检测、source reference 解析、消息投影、会话调度、prompt builder 和文档。
- 配置：actor 示例和 `dev/onebot/config/bridge_actor.toml` 删除三个退休的手工身份/别名字段；`local_config/llm/prompt.txt` 路径和用户自定义内容保留。
- 兼容：历史 journal 不回写、不因身份缓存刷新产生伪消息编辑或重放；新 wire 格式使用显式版本变更，旧 provider continuation 不跨版本复用。
- 本计划明确调整 `remove-chat-llm-wake-gate` 变更中保留的旧 direct-turn policy；该变更尚未归档，后续同步 specs 时须以本计划的新 @/回复语义为准。
- 验证采用构建、离线回放、请求体检查和人工集成验收；不新增单元测试。Core/SDK 更新与在线 actor 热重载分开实施。
