## Why

`chat_llm` 每轮从独立 SQLite 历史库重建最近若干条文字消息，滚动删除历史前缀、过滤上轮用户提问并丢失跨轮工具记录，既破坏上下文连续性，也难以复用模型前缀缓存；含图片的整条消息又被直接拒绝，原始群消息还与 message-store 重复采集。需要在不改动现有 message-store 基础服务的前提下，复用其消息记录，以持久化追加式会话、显式 token 预算和按需看图工具解决这些问题。

## What Changes

- **message-store 保持不变**：不修改其源码、schema、表、索引、接口、事件格式或既有读写行为，不提取新共享存储库，不要求基础服务迁移或重启。
- chat_llm 允许拥有自己的 DB，但所有存储读写必须通过 core `DbManager`/数据库接口，在 `BlockingExecutor` 上执行，不再自行使用 `sqlite3` 打开、操作数据库。
- chat_llm 的 `ValidationOnly` 仅校验配置/profile 和静态能力，不访问数据库、不要求运行中的 gateway；schema 检查留到 startup/reload。为此不修改 core 或创建 actor 私有执行器；core CLI 自身既有 DB 打开行为不在本次变更范围内。
- 通过源消息主键/完整唯一键做逻辑关联，保存平台、bot installation、conversation、message ID 等精确身份，而非复制完整原始消息归档；不要求真实外键。同库可按键 JOIN，分库通过 core 接口读取后合并，不强制共库。
- **BREAKING**：弃用旧 chat_llm `messages_v2` / `history.db`，不导入、不兼容读取、不再创建旧库；移除 `history_db_path`、`history_limit`、`history_ttl_days`。旧库在旧写入者停止后可直接删除，但启动和配置校验不得自动删除任意路径；这不禁止使用 DbManager 管理新的 LLM 状态库。
- 新 LLM 状态库只保存会话日志、provider-native reasoning/CoT 续接状态、源消息引用、稳定文字/图片标识投影、工具调用/结果、发送回执和摘要。图片以 `<image>稳定标识</image>` 表示，关联平台原生文件标识；不保存解析得到的临时 URL 或图片文件，不再次完整采集 payload/raw。
- 普通群消息只读复用 message-store；已授权且被 Consume 的 chat 命令、未出现在 message-store 的 bot 出站回执作为 LLM 会话事件保存，不向基础服务补写，不改变命令传播或 Bridge 转发语义。
- 固定系统指令与工具定义，在平台、bot installation、群、topic 精确隔离的会话中只追加；直接回复/主动插话策略放在当轮末尾，工具终结结果也持久化，重启恢复不重放发送副作用。
- **通用 reasoning 生命周期**：无论 provider 返回明文 reasoning、带签名 thinking block，还是 opaque/encrypted continuation，均按 exact 会话与 epoch 持久化并隔离。每次工具续接、后续用户轮次和重启后的请求构建，都按已核实的 endpoint 协议附带允许重放的状态，保留内容、签名、顺序和调用关联；不能仅保存最终回答，也不能仅支持 Codex 密文。未返回的内部 CoT 不可获取；端点不支持历史重放时显式报告能力边界，不硬塞字段、不静默丢失。
- reasoning 不写普通日志、不发群，不转换成普通 assistant 文本。签名保护的内容不可改写，密文保持不透明；临时图片链接的可检查回显仍须剔除，无法安全修改的受保护块应明确失败，不能破坏签名。opaque continuation 允许原样存储，但不承诺检查其密文内部是否包含图像访问数据，也不将其作为图片 URL/像素恢复来源。压缩和不兼容切换明确处理 reasoning 生命周期。
- `/toggle_think` 明确为 `(platform, bot installation, group_id)` 群级开关，同群所有 topic 共享；它只控制主动 decline 理由是否发到群里，不改变模型 reasoning、工具定义或按 topic 隔离的上下文。
- **BREAKING**：上下文容量改为必填 `context_window_tokens`，接受正整数或 `"128k"`（128,000 tokens）写法；输出预留、压缩触发/目标、近期轮次保留和媒体限制均显式配置，不引入默认值。
- 接近 token 预算时才摘要压缩，保留近期完整轮次；摘要原子提交并开启新上下文 epoch，失败不静默删历史、不发送已知超预算请求。
- 支持 QQ/TG 当前与历史图文、纯图及多图的按需查看。模型调用稳定声明的 `view_image(image_id)`，执行器校验范围后调用平台接口：QQ 此时解析短期 URL 并交给支持外链的 provider；Telegram 使用 typed 接口取到内存后构造当轮 data URL，不泄露 bot token。临时图片输入仅在当前工具轮次使用，不写历史、不落盘，不以五分钟作为隐式有效期默认值。
- 明确区分可缓存的持久文字/标识前缀与本轮临时视觉输入；下一轮移除临时图像、以后按需重新解析。放弃“所有含图请求跨轮完整前缀相同”的不适用保证，不为了旧 URL 重放错误而持续重写持久历史。
- 记录 provider input/cache usage、预算估算和压缩/媒体失败，通过现有测试与受控集成演练验证前缀稳定、图像追问及恢复；不新增单元测试。

## Capabilities

### New Capabilities

- `chat-llm-message-references`: DbManager 管理的 LLM 自有状态库、对现有 message-store 的只读主键关联、同/分库读取合并、无源记录的命令和回执及旧库弃用。
- `chat-llm-durable-context`: 持久化追加式模型上下文、完整工具轮次、跨 provider 的 reasoning 隔离/持久化/协议化重放、稳定前缀、精确会话隔离、群级 toggle_think 及无发送副作用恢复。
- `chat-llm-token-compaction`: 显式 token 配置、完整请求预算、摘要压缩与 provider usage 观测。
- `chat-llm-vision-context`: 稳定图片标识、模型主动调用 view_image、平台临时解析、两个 provider 的当轮多模态输入及短期资源生命周期。
- `core-image-access`: OneBot 11 原生图片引用、typed get_image 与 LLOneBot 临时图片 URL 解析，以及供 actor 复用的 core 有界图片 HTTP 获取接口。
- `bridge-core-image-access`: Bridge 将已有图片探测/下载调用迁到 core，保留格式识别、归一化、重试/占位和转发业务语义。

### Modified Capabilities

现有 message-store、DbManager、命令 ACL/传播和 generation 主契约不变。新增 core 图片访问能力与 Bridge 消费迁移已获用户明确授权；不扩大为平台网关或通用数据库重构。

## Impact

- `local_actor/chat_llm/`：actor 编排、消息引用/会话仓库、PromptBuilder、两个 provider 客户端、配置、依赖清单及文档。
- 部署配置示例：显式配置 LLM DB、message-store 只读数据源、namespace、预算及媒体限制；chat_llm 订阅既有 `MessageStored`，不改变 message-store 发出的内容或其他消费者链路。
- 不修改 `local_actor/obcx-message-store/` 或 core 数据库 API，不新增普通存储库，不对 message-store 执行任何 DDL/DML、迁移或 pin/清理写入。
- 用户已追加授权最小 OneBot 平台 SDK 扩展，以及 Bridge 图片获取改用 core 接口。目标实现明确为 LLOneBot / OneBot 11，复用标准 `get_image(file)` 并补齐 typed 网关。已核实的 LLOneBot 支持显式 `download: false` 和 `url/file_name/file_size` 扩展返回；标准仅保证文件路径，缺 URL 时不能冒充公网链接。OBCX 不管理 rkey，也不拼接 QQ 签名链接。Bridge 的重试、占位、转码/归一化仍属于 actor，重复 HTTP 拉图实现移到 core。
- 新 LLM DB 可以与源消息 DB 分离；二者之间不依赖跨库事务或强制外键。输入快照和图片引用是保留会话的派生状态，不是第二套原始消息/媒体库。撤销本地图片目录、内容寻址缓存、QQ HTTP 下载器和媒体 GC 的原方案。
- 本根 change 使用 CLI 解析的规划位置；四项 chat-llm 能力最终归属 chat_llm 主规格，`core-image-access` 归属 core，`bridge-core-image-access` 归属 Bridge。归档时按所有权同步，不把 actor 业务规格自动同步成核心规格。
- 实施不自动部署、重启或操作现场数据库。已授权的 core/SDK 源码变更在独立目录编译；actor 编译使用新冻结 installed SDK 和 `scripts/actor_build.py`，不覆盖运行中的 core 或既有 SDK。
