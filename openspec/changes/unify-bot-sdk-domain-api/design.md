## Context

这是已获用户批准、开始实施的 SDK 接口设计，不是实现完成报告。用户要求使用 subagent 并行完成；当前工作区既有修改保留，写入任务使用独立 snapshot worktree，进度与边界见 [implementation-status.md](implementation-status.md)。

逐 actor 的现状、代码入口、富媒体路径、数据迁移和验收见 [actor-migration.md](actor-migration.md)。该附件是本设计的组成部分：覆盖五个顶层 actor 和 ExHentai 的 QQ forward probe，不以一条“迁移所有消费者”替代实际迁移方案。

现有基础可以保留：`BotOperationGateway`、exact-installation registry、平台-owned descriptors、统一错误、actor generation 生命周期及 binary gateway codec。缺口在公共领域契约：`common::MessageSegment` 的自由 JSON、QQ/TG 的不同媒体/成员 DTO、原始事件和单消息发送约束仍要求 actor 做平台适配。

本设计承接两个有效基线：

- `decouple-bot-platform-contracts`：公共 SDK 不反向依赖 QQ/TG SDK，平台注册与 transport/recipe 所有权不回收至中央平台 switch。
- `auto-detect-chat-llm-addressing`：`GetSelfRequest` / `SelfInfo` / `bot.self.get` 和 `SelfIdentityCache` 已有进行中的实现；其中只读缓存、生命周期刷新、旧 QQ login 查询的缓存投影和严格 get_msg 作用域校验都需要保留。前序未完成项仍是实施依赖，不因本提案而视为完成。

用户已经明确纠正会话模型：话题属于完整会话地址，应内部组合进一个 ID；不得在公共 `ConversationRef` 添加 `thread`、`topic_id` 或供业务拆解的平台寻址字段。

## Goals / Non-Goals

**Goals:**

1. SDK 用户用结构体调用公共收发、删除、self、成员、引用和媒体接口，不解析 CQ/entities/file_id，也不为这些能力写平台分支。
2. 完整会话身份成为不透明值，群/私聊/话题不会因相同 ID 或错误拼接而混淆。
3. 同一消息表达具有可检查的语义；不能完整表达时明确失败或按显式策略拆分，不静默丢内容。
4. self 查询融入公共客户端且仍为 cache-only；资料、提及和发送者使用相同 `UserRef`，不新增另一身份事实来源。
5. 迁移不破坏 source/Bridge/Chat LLM 的持久身份、ACL、去重、取消和不确定提交规则。

**Non-Goals:**

1. 不增加第三个平台、不提供任意 provider API、不承诺 TG 任意消息历史或任意用户查询。
2. 不统一所有平台专有功能；QQ 合并转发/戳一戳、TG 编辑/话题管理保留平台 SDK 扩展（仍属于包含 core 的完整 SDK，不是独立产品）。
3. 不建设隐式跨安装媒体搬运、无限缓存、自动补偿或发送事务；`AllowSplit` 不等于原子发送。
4. 不把 `is_self`、LLM 稀疏标记、proactive/decline 或命令权限移进 self cache。公共事件提供客观证据，业务策略继续属于 actor。
5. 不新增配置默认值、新单元测试或自动部署；不修改在线数据库及用户 prompt。

**最新实施顺序（用户确认）**：先依据官方 OneBot11 规范核对标准消息段、接口与事件的完整覆盖并补齐标准实现；不能要求用户先选择 LLOneBot/NapCat。所有非标准扩展的新增/改造暂停，已有代码保留，不删除、不擅自替换行为。标准完成后，再列出具体非标准扩展及其影响，集中询问用户后续处理。标准之外的文件接口、无作者字段的 provider-self forward 行为等不得借兼容迁移提前实施。规范提供的能力、代码是否实现与真实 provider 运行验收分开记录；规范仅返回 provider 本地路径时，不猜作 OBCX 本机可读路径。用户随后明确选择“全部移植”：原迁移范围之外的标准群管理、审批、凭据、服务操作、完整事件、hidden quick operation 和异步/限速形式也纳入公开 typed QQ SDK，见 `onebot11-standard-sdk` 与任务17；不能继续按“范围外”排除。接口实现不等于执行操作；不以字符串可序列化冒充标准功能完成。

**全标准补充边界**：平台 SDK 通过具名 typed 请求/结果表达标准能力，不增加任意 action/params JSON 通道。标准 xml/json 消息内容须作为专门、受界限约束的内容意图，不是通用操作逃生舱。显式 provider-delegated URL/reference 意图与共同 `HttpSource` 的有界 core 获取分开；provider 的 cache/proxy/timeout 不伪装为 core 获取选项。SDK 负责其拥有的准备、包络、请求/输出预算，未知 provider 读取/内容展开不能冒充已验证；调用者要求端到端保证但无证据时提交前拒绝。

新增标准 login 查询与旧 cache-only self/QQ兼容入口分名：显式同步查询可经同一 identity/refresh coordinator 获取精确观测并在同一缓存发布，保持300000ms周期及代次/取消保护，不另建缓存、不用回读缓存伪装原生结果。标准 get_msg/get_forward_msg 可用独立的 installation-scoped opaque selector；selector不是会话/存在性/引用证明，结果为detached snapshot，不推断会话、不发布消息事件、不授予reply或自动历史lookup。既有scoped forward及共同ResolveReply不改变。async/限速确认只证明受理，不伪造最终结果或发送回执。凭据必须通过明确的敏感结果/日志脱敏边界；任何真实凭据请求、管理操作和服务重启仍未授权。

## Decisions

### 1. 值模型：完整会话 ID，而不是公开平台参数

```cpp
namespace obcx::bot {

struct ConversationRef {
  BotInstallationRef installation;
  ConversationId id;
};

struct UserRef {
  BotInstallationRef installation;
  UserId id;
};

struct MessageRef {
  ConversationRef conversation;
  MessageId id;
};

} // namespace obcx::bot
```

`ConversationId`、`UserId`、`MessageId` 为 SDK 提供的完整强类型值，不能由普通业务调用平台原生字段构造器。引用从归一化事件、受验证的配置解析和操作结果取得；SDK 提供确定的持久化 codec，业务只保存/恢复整个值，不解析编码。

`ConversationId` 内部包含会话种类、原生地址和必要子会话信息。用户提出的 `xxx|xx` 是组合思想，不是要求业务使用的协议。实施冻结语法为 `obcx1:<tag>:<lowercase hex payload>`：payload 用 32-bit big-endian 长度前缀编码 UTF-8 surface、native kind 与 1..8 个组件；单组件最多 1024 bytes，整个编码最多 4096 bytes，两种边界同时执行。tag 区分 conversation/user/message/media/media_identity/media_group。禁止非规范大小写、无效 UTF-8、额外/缺少组件及 outer installation surface 不符；具体 native kind/组件规则和 golden 样例由平台 codec 固定。

公共 SDK 序列化/恢复不透明值只依赖共同结构、版本与规范化校验，不要求 actor 注册 provider codec 或链接平台 SDK。process 私有、注入式 codec registry 按 SurfaceId 调用 owning codec，在 ingress/config/legacy/dispatch 边界完成原生作用域检查后才允许 provider I/O。无全局 singleton 或 actor 可见 registry；业务不能把“结构可解码”误作“原生目的地已授权”。

实施补齐配置/历史消费者的接点：generation-owned `ReferenceResolver` 是独立、纯读取的 SDK 配置/legacy facade，接收明确标为 legacy 的源地址/归档值，返回 `BotOperationResult<ConversationRef/MessageReceived>`；其内部使用注入的平台 binding，不把私有 codec/registry 暴露为 actor service，不做 I/O、不注册 provider、不授予 reply lookup 权限。原始 JSON 只允许作为显式归档恢复边界的输入：共同 ReferenceResolver，或所属平台 SDK 明确命名的纯 typed 专有内容恢复函数（如 restore_special_content）。后者复用相同 source/scope/记录时间及有界输入校验，只返回 scoped typed views 与明确顺序，不做 I/O、准入或授权；不回到普通消息/媒体路径。ReferenceResolver 的兼容职责还包括 scoped user/message display-ID 双向转换，以及保留旧段序、引用位置、原始 mention 拼写和图片 identity/locator fingerprint 的独立 typed compatibility projection。Fingerprint 值由 SDK 不透明序列化，actor 仅整体 hash/store，不能解释为下载或授权来源；旧 image_id/source fingerprint 的算法与响应策略仍属于 actor。兼容 projection 还携带有界、不可序列化的临时图片秘密匹配证据，精确保留旧 url/file 候选的顺序及原始字节，即使图片不可读也不遗漏；actor 继续用原 scrubber 在文本/命令替换完成后、fingerprint 前脱敏，不把秘密用作下载定位或持久字段。平台在任何拥有式构造前检查包含重复文本、fingerprint 与脱敏证据的完整展开预算，且借用验证与原归档的精确对应。历史表没有独立 topic 列时，调用者显式声明未观察，而非伪造主会话；所属平台只在已验证完整 canonical repository archive 足以证明时还原 native topic 事实。另以 `LegacySourceFacts.topic_in_legacy_scope` 保留旧会话键的 topic 选择事实：不能把原始 thread 的存在直接等同于历史 session topic，也不能因 typed 主会话规范化而抹掉历史 thread 1 的字节。该字段不改变 `ConversationRef`、普通路由或授权，actor 继续拥有旧 Scope::json() 算法。具体冻结与 owner 边界见 integration-seams.md。完整历史事件恢复须提供有记录的接收/首次本地入库时间，缺失时明确不可用，不以当前时间或零值伪造。

私聊与群的相同数字、同群不同 topic、同一原生 ID 的不同 installation 必须互不相等。`MessageRef` 独立保存完整会话引用和消息 ID，不能把所有身份压成无作用域裸字符串。TG 话题信息是内部路由内容，发送同一个引用自然回到原话题；创建/删除话题仍走 TG 扩展。

配置允许人类继续按原生平台格式声明目标，但仅平台配置解析/显式迁移入口理解它。运行时交给普通 actor 的目标已经是 `ConversationRef`。command ACL 的群范围与 exact-conversation 范围保持区别，内部投影不能把旧“全群含话题”权限悄悄缩窄为一个话题，也不能把 exact-topic 权限扩成全群。

替代方案：公开 optional thread 已被用户否决；给业务一个拼接 helper 仍要求业务理解地址组件；保留 GroupTarget/PrivateTarget/topic 三套发送入口会继续暴露适配责任。

### 2. 消息与媒体：领域结构体，不透传原生段

下面是签名级草案，省略完整 codec/校验实现：

```cpp
namespace obcx::bot {

enum class TextStyle { Bold, Italic, Code };
struct Text {
  std::string content;
  std::vector<TextStyle> styles;
};
struct Mention { UserRef user; };
using RichText = std::vector<std::variant<Text, Mention>>;

// 来源均为拥有生命周期的数据值，不包含 stream、客户端或 executor。
using MediaSource = std::variant<MediaRef, BytesSource, LocalFile, HttpSource>;

struct Image { MediaSource source; RichText caption; };
struct Animation { MediaSource source; RichText caption; };
struct Video { MediaSource source; RichText caption; };
enum class AudioKind { Audio, Voice };
struct Audio { MediaSource source; AudioKind kind; RichText caption; };
struct File { MediaSource source; std::string filename; RichText caption; };
using MessageBlock =
    std::variant<RichText, Image, Animation, Video, Audio, File>;
using MessageBody = std::vector<MessageBlock>;

enum class DeliveryPolicy { SingleMessageOnly, AllowSplit };
struct SendLimits {
  std::size_t maximum_messages;
  std::size_t maximum_media_item_bytes;
  std::size_t maximum_media_total_bytes;
};
struct SendMessageRequest {
  ConversationRef target;
  MessageBody body;
  std::optional<MessageRef> reply_to;
  DeliveryPolicy delivery;
  SendLimits limits;
};
struct DeleteMessageRequest { MessageRef message; };

} // namespace obcx::bot
```

内容块顺序是契约的一部分，caption 是明确附属文本。底层不能把前置文字偷偷搬成图片 caption、把提及改成姓名文本或只发送第一张图片。原生格式差异归平台 adapter：QQ 转 CQ/结构化段；TG 转文本/entity offsets、媒体请求或 multipart。`RichText` 不向用户暴露 UTF-16 offsets/parse_mode；原生文本有非已知用户的 username mention 时，接收端使用单独的 unresolved-mention 类型，不伪造 `UserRef`。

第一版公共发送不支持任意原生 JSON 或未知内容原样重发。图片为必须落地的公共媒体能力；动画、视频、音频、文件按实际 provider/recipe 能力发布，不用“平台有此功能”代替“本安装已实现”。`Animation` 与静态 `Image` 区分，不能丢帧当成功；`Text.styles` 只表达显式已建模样式，至少覆盖 Bridge 的发送者斜体，目标不支持时拒绝而非静默剥离。其余专属富文本、sticker/video-note 和相册编辑保留扩展。Bridge 迁移完成要求逐项覆盖其实际媒体路径或记录明确阻塞，不能只实现图片就宣称 actor 已迁移。

actor 可明确构造目标 caption、执行占位/缩放/转码或选择无样式文本；这些是 actor 展示策略，不是 SDK 自动降级。普通 adapter 只能忠实执行已经构造好的 body。无法支持的媒体、caption 组合或格式应明确拒绝。

媒体来源约束：

- `MediaRef` 是带 installation 所有权的不透明定位值，不含凭据，不暴露 tokenized URL；可直接复用的素材只限正确安装。可选 `MediaIdentity` 是独立的稳定缓存身份，不是可下载/发送的 locator；缺失时不以临时 URL 或原生 file_id 替代。
- `BytesSource` 使用拥有/共享只读缓冲和确定元数据，gateway 保持 binary 编码，不生成逐字节 JSON 数组。SDK/安装的硬边界和请求边界同时生效。
- `LocalFile.path` 是调用方已经处理好的最终路径字符串。文件放置、访问权限、Docker 挂载与 path mapping 属于调用方；SDK/core 不映射、不推断文件系统、不限定 OBCX-host、不要求共享文件预注册或新增统一路径授权配置。adapter 负责原生路径/file URI 编码或已支持的上传机制，不改变路径所指向的文件。
- `HttpSource` 由调用方选择，SDK 保留 HTTP(S) 协议校验、凭据隔离和既有传输重定向限制；不继承 bot 凭据、不暴露认证下载地址，不新增统一部署授权门槛。
- 跨 installation 复制必须显式 `read_media(source, limits)` 获得拥有的字节，再 `send(destination, BytesSource)`；两边独立路由/权限/预算，不隐式访问另一个安装。

`SendLimits` 必须显式提供，没有默认参数；纯文本可显式使用零媒体预算。逻辑调用继承既有 deadline/cancellation，不为每个拆分步骤重置总预算。数量/字节边界约束已知大小及 SDK 实际读取、分配和准备总量，不能先无限加载再检查。只在 provider 可见的路径不需要 OBCX 本机 stat；未知文件大小不是零，也不能声称 SDK 验证过远端文件存在或实际大小。

**最新用户确认的责任边界覆盖此前的 host-only/注册设计。** Bridge/ExHentai 保留既有 mapper、路径配置、owner/lease/配额/清理策略，向 SDK 传入最终路径；`obcx.path-mapping` 继续是调用方使用的外置库，不成为 core/SDK 依赖。删除候选中的 `MediaSourcePolicy` 部署授权门槛和共享文件注册前置条件，不另起同义 installer/registry。QQ forward 使用 typed nodes 和相同媒体来源，adapter 内部构造原生文件表示，actor 不拼 CQ JSON。原生不支持的来源组合明确拒绝，不能用“缺路径策略安装器”代替真实能力判断。

严格的跨安装复制意味着 Bridge QQ→TG 的 URL-first 链路和 Chat LLM QQ URL-only 视觉链路改为显式读取字节；附件明确记录这一行为/资源差异，不能伪称网络路径完全不变。保留 URL-first 需要另行审核 locator-transfer 扩展，不是公共接口的隐式例外。

替代方案：仅给 `type/json` 外包一层 class 不消除平台字段；让 actor 自选 multipart/get_file 仍然是 SDK 泄漏；自动跨安装下载会隐藏权限和资源消耗。

### 3. 公共客户端与 self 查询整合

`BotClient` 是薄的、非全平台虚表 facade，由 runtime/actor service 注入；公共 SDK 不包含 QQ/TG DTO。内部保留 gateway/typed codec/registry，普通用户只依赖领域头文件，不构造 `OperationEnvelope`。发送专用 codec 必须把完整 `SendOutcome`（包括 `SendFailure` 中的确认回执）作为有类型的领域结果传递，不能套用旧 `BotOperationResult<Json>` 的普通 error 分支而丢失部分结果；无法可靠解码整个 outcome 时仍保守报告未知，不捏造或重试。平台扩展通过受控 SDK binding 使用同一安装路由，不增加 actor 可任意调用的 provider 方法。

| 公共方法 | 请求 | 结果 / 内部 action |
| --- | --- | --- |
| `send` / `delete_message` | `SendMessageRequest` / `DeleteMessageRequest` | `SendOutcome` / `DeleteReceipt`；`message.send` / `message.delete` |
| `get_self` | 现有名称 `GetSelfRequest{installation}` | `SelfInfo`；保留 `bot.self.get` |
| `get_member` / `resolve_reply` | `GetMemberRequest{conversation,user}` / `ResolveReplyRequest{reply,policy}` | `MemberInfo` / `ReplyInfo`；`member.get` / `message.reply.resolve` |
| `read_media` | `ReadMediaRequest{media,maximum_bytes}` | `MediaBlob`；`media.read` |
| `capabilities` | `GetCapabilitiesRequest{conversation}` | `ConversationCapabilities`；`bot.capabilities.get`，本地 installed capability 查询，无 provider I/O |

所有异步方法保留当前 `boost::asio::awaitable` 与 actor 的 `await_asio` 边界。除发送使用专用 outcome 外，返回 `BotOperationResult<T>`；正常拒绝不是靠抛出原生 provider 异常。能力查询不能保证动态权限/外部资源永远可用，执行时仍校验。

self 的目标 DTO：

```cpp
namespace obcx::bot {

struct GetSelfRequest {
  BotInstallationRef installation;
};
struct SelfInfo {
  UserRef user;
  std::optional<std::string> display_name;
  std::optional<std::string> username;
};

} // namespace obcx::bot
```

保留名称及 action，升级字段为新 SDK 契约：旧 `installation + user_id` 由统一 `UserRef` 表达；旧空 display_name 转为缺失。`SelfInfo.username` 与接收端 unresolved mention 的 username 使用同一平台 codec 的规范化比较值，普通 actor 不自行处理大小写；需要原始展示拼写时由显式 profile 扩展提供。该布局变化是 BREAKING，不伪称原 DTO 二进制/JSON 完全兼容。迁移代码集中在 codec 和 cache publication 边界，不让 actor 维护新旧两套字段。`SelfClient` 不再作为第二公共服务暴露；迁移指南改用 `BotClient::get_self`。现有 QQ `GetOneBotLoginInfoRequest` 若继续作为专属兼容扩展发布，仍只是同一 cache 的只读旧值投影，绝不重新调用 provider。

```cpp
namespace bot = obcx::bot;
auto self = co_await client.get_self(
    bot::GetSelfRequest{.installation = event.message.conversation.installation});
// self 成功后，self.value->user 与消息的 UserRef 直接比较。
// 比较、关系标记及回复策略属于 actor；此请求不计算 is_self。
```

缓存保持 installation/account-generation 所有权，读取立即返回已有快照或 `CacheUnavailable`，包括首次未就绪、失效和关闭时；读取不等待、触发或请求刷新。后台仍用 QQ get_login_info / TG getMe，TG 验证 is_bot。重连、账号冲突、成功 reload 的刷新与并发合并、旧代结果拒绝沿用前序约束；同代刷新失败可保留旧快照。刷新配置沿用前序已批准政策，不在本提案引入、扩大或重新定义任何默认值。

`MemberInfo` 只统一可证实的基础视图：`UserRef`、display_name、可选 username/is_bot、会话内可选 role/membership。QQ 名片优先于昵称、TG first/last name 合成由 adapter 完成。未知角色不能变成普通成员。群资料独有项保留平台扩展。TG 没有任意用户查询，不提供虚假的 `get_user(any_id)`。

`ResolveReplyRequest` 只能使用接收端产生的真实 `ReplyRef`，其中包含 source message 和目标消息的完整作用域；runtime 验证观察到的引用关系，不接受仅凭用户文本构造的任意目标。`ReplyLookupPolicy` 必须显式选 `LocalOnly` 或 `AllowProviderLookup`。先查现有事件/本地证据，再按策略做有界查找；QQ 可用严格 get_msg，TG 没有事件/缓存证据时返回明确不可用。不递归、不过界、不承诺完整历史；失效的证据不能启动自由 get_msg。

实施审计确认没有可复用的进程级 relationship admission store，因此添加每 installation 的共同 domain 设置 `reply_evidence_max_entries` 与 `reply_evidence_ttl_ms`，两项均为必填正值，**不提供默认值**。由共同配置层解析并计入 plan fingerprint，不在 QQ/TG connection grammar 重复解析。只保存已准入的 source/target/observation/author 关系，条目受 SDK 字段硬边界、显式容量、TTL 与关闭/账号作用域约束；历史恢复本身不写入此 store，也不触发 provider 查询。这不是新的 self cache；现有 self 刷新政策保持不变。

替代方案：保留单独的 SelfClient、媒体 client 和 member client 会割裂公共用法；让 get_self 自动等待初始化会在正常消息路径制造 provider I/O；把是否本人写进 SelfInfo 会混淆事实、解析和业务策略。

### 4. 接收模型：统一客观证据，不下沉 actor 策略

```cpp
namespace obcx::bot {

struct MessageReceived {
  MessageRef message;
  Sender sender;
  std::optional<TimePoint> sent_at;
  TimePoint received_at;
  ReceivedBody body;
  std::optional<ReplyInfo> reply;
  std::optional<MediaGroupRef> media_group;
};

} // namespace obcx::bot
```

`Sender` 区分已知用户、会话身份及未知，不能把匿名/频道身份伪造成用户。`ReceivedBody` 复用公共文本语义，媒体只使用 `MediaRef` 并附可用 typed 元数据/`MediaIdentity`，允许显式 `UnresolvedMention`、`MentionEveryone` 和 `UnsupportedContent`，分别表示没有可核实用户引用的真实提及、全体提及和未建模内容；这些不是任意发送 payload。

`MediaGroupRef` 包含完整 `ConversationRef` 和不透明组 ID，不暴露 TG media_group_id 的解析规则。每个成员仍是独立 `MessageRef`；组证据不意味着成员齐全或原子到达，聚合、超时和延迟成员策略属于 Bridge。专有贴纸/卡片/forward 的 typed 扩展视图可复用公共引用，不能让普通图片路径重新读取 `event.data`。

TG `mention`/`text_mention`/caption entities 的 UTF-16 处理和 QQ at/reply 解析留在平台模块；只从原生结构化证据产生对应内容，不把普通文字中的 `@name`、CQ 字面量或 `[reply:self]` 当证据。TG 文本 username mention 无法解析用户时保留平台已确认的 username 与规范化比较语义，不伪造 ID；actor 可通过公共同型用户名匹配值和缓存 self 信息进行平台无关比较，不需自己处理 TG 大小写规则。接收 caption 属于附件的 RichText，不能仅藏在原始 JSON。

`ReplyInfo` 提供目标引用、可选作者和证据可用性；外部/跨会话引用明确区分，不能当作本会话自回复证据。原始消息时间不可获得时 `sent_at` 缺失，`received_at` 记录接收时间，不用接收时间伪装原始时间。self identity 从共同 cache 取得，不再提供 TG `self_id="0"` 的虚假公共身份。

与先前口头草案相比，这里不把 `Addressing{is_self...}` 塞入核心事件：前序 self 变更明确将本人判断、稀疏标签和响应策略留给 Chat LLM。公共 SDK 只统一可比较的引用和客观提及/引用证据；actor 做一次平台无关判定即可。`GetSelfRequest` 始终纯读身份。这不阻止以后提供独立、纯函数式的公共关系 helper，但不在本次引入第二套判定服务。

原生 payload 若需归档/排错，仅通过明确的平台或归档扩展保存，不能成为公共业务正常路径的必读数据。Message Store 的 source raw evidence 可以继续保留；typed 事件不能覆盖它。更新/删除事件按相同 `MessageRef` 身份映射，不能用 `sub_type` 平台字符串迫使普通消费者重新分支；无法获得的原生事件不虚构发布。

替代方案：只统一发送端会让每个 actor 重写 ingress 解析；将未知作者/时间补成默认值会产生错误权限及关系判断；将 self 相对关系冻结在核心事件可能在账号代次变化后产生错误策略。

### 5. 执行、失败与兼容边界

发送先完成纯值/作用域/能力校验，再在显式预算内准备必要媒体和构建有序执行计划，全部可预知的不支持必须在首个发送副作用前失败。既有 exact installation/surface、registry admission、generation lease 和总 deadline 继续生效。权限变化、资源消失和 provider 不确定结果无法靠预检消除。

`SingleMessageOnly` 表示一个原生消息，不是一个 HTTP 请求；TG media group 返回多个消息，因此属于拆分。`AllowSplit` 可用多个请求或原生 batch 保持内容及顺序，但不能重排文本/媒体。原始 `reply_to` 只用于第一条输出，不暗建回复链；所有输出保留完整目标会话（含内部话题）。文本长度/entity 编码、native upload 细节和内部操作选择属于 adapter。

```cpp
namespace obcx::bot {

// ContentSlice 标识请求中的输入块/片段，不是原生 entity offset。
// PublishedMedia 保留已验证的 MediaRef/可选 MediaIdentity，已证实时关联输入块。
struct SendOutputDetails {
  MessageRef message;
  std::optional<std::vector<ContentSlice>> content;
  std::vector<PublishedMedia> reusable_media;
};
struct SendReceipt {
  std::vector<MessageRef> messages;
  std::vector<SendOutputDetails> output_details;
};
struct SendFailure {
  BotOperationError error;
  std::vector<MessageRef> confirmed_messages;
  std::vector<SendOutputDetails> output_details;
  bool may_have_unconfirmed_messages;
};
using SendOutcome = std::variant<SendReceipt, SendFailure>;

} // namespace obcx::bot
```

`output_details` 与确认引用逐项对应，记录输出到输入内容的可验证关联，以及 provider 真正提供的可复用媒体；未知字段缺失，不能猜造 file ID。畸形 batch 中已确认消息但不能确认输入对应关系时，`content` 明确缺失；仍保留该消息引用，不因 provenance 不全丢掉提交证据，不用未知关联执行成员级编辑/删除。Bridge 将输入块的来源成员关系与这些 details 组合，才能持久化拆分/相册的多对多映射；只返回或保存 `messages.front()` 不符合迁移要求。gateway 和物理 confirmation 同样保留关联信息。

第一步失败可 definitely-not-submitted；已有任何确认输出或可能输出后，整个逻辑请求都必须保守标为 possibly-submitted、不可自动重试整批。`may_have_unconfirmed_messages` 区分“只存在已确认的部分输出”与“还有未知提交”；每个已确认 ref 必须经过实际 provider 结果和目标作用域校验。首次失败停止后续步骤，不自动删除前序输出。取消、超时及 native batch 的部分/畸形回执也要保留可核实的结果，不能因 gateway 失败退化为丢失所有证据的普通错误。

已确认输出可发布独立的 typed confirmation 事件，用于 source echo/映射；这不是把失败逻辑请求伪装成整体成功。既有逻辑 success handler 仍只观察完整成功。两种通知都使用 process-owned executor 的 admission/close/drain 规则，通知失败不改变 provider 结果，也不重发。会产生消息的 typed 专有发送（如 QQ forward、TG cached sticker）须通过注册的 outcome-aware binding 进入相同 normalization/observation 路径；通用 core 不按 provider/action 名称分支，也不能把包装结果的 outer success 当作内部 SendFailure 的逻辑成功。单输出也要保留完整 SendOutcome；无共同 MessageBody 的扩展不得伪造 ContentSlice，已证实的 nested-node 关联留所属扩展 typed details。

为使物理 confirmation 能关联到 actor 已落库的 attempt，SDK 保留 `send(request)`，并提供显式 `send(request, OperationCorrelation{attempt_id})` overload。`OperationEnvelope` 可携带该可选 metadata，原 body/request/SendOutcome 布局不因 correlation 改变。attempt_id 是有界、非空 UTF-8 值；调用者使用其 actor/namespace 作用域内唯一的 attempt ID，先持久化准入。dispatcher 向物理与逻辑通知原样传递已提供的 ID，仅在未提供时生成进程唯一 ID。correlation 不是 provider idempotency key、授权或隐式去重机制，不能用它绕过 ledger 的再次提交检查，也不能只在返回后靠 MessageRef 猜测 notification 所属 attempt。

公共 capability 描述包含已安装操作、媒体种类/来源、支持的组合与已知限制；未知动态权限明确未知。不将 source-specific 原生 action 字符串暴露为业务分支依据，typed 扩展仍可查询自身是否支持。

持久化边界必须分层：新的 `ConversationId` 不是现有 `chat:<id>`/`group:<id>` 数据库键的直接替代。集中 legacy-to-domain 读取映射使用现有 source_platform/source_bot/conversation/topic 证据，维持既有存储分区、Bridge pair/来源身份、Chat LLM session 与已完成发送记录。保留身份不等于保证所有表形状不变：Bridge v3 每个来源/目标组合只有一个输出槽，需要 actor-owned 的显式多输出 ledger 升级，不能循环覆盖旧 mapping 假装一对多。旧记录缺失 topic 证据时不得猜测为某个话题；无法安全定位的恢复操作明确不可用或要求单独迁移。引用编码升级本身不产生 source edit、不重新入队、不重发，也不重新解释历史 ACL。

SDK 是包含配套 core 的完整开发包，不是脱离 core 独立交付的公共/QQ/TG 三个产品。公共与平台 target 只区分调用方选择的接口；SDK target 自动传递配套 core 的链接依赖，调用方不必另行查找或手工补链接 core。公共头仍不包含平台 DTO，平台私有 codec/registry/transport 仍不安装；这属于接口边界，不要求二进制排除另一平台或 host 实现。QQ/TG 实现继续编入同一个 core，不新增纯平台支持库，也不做运行时平台插件化。本次用户澄清覆盖前序文档中脱离 core 的独立链接验收要求；验收使用完整 installed SDK 和其 imported core。旧 common 段及旧 group/private/topic 公共发送入口不成为永久兼容 API；原生实现可继续私有复用。真正平台专有的扩展 request 迁移到新 `ConversationRef/MessageRef`，不能借扩展重新要求普通回复传 topic。

actor 数字调度 ABI 保持 V2；输入 contract schema 从当前模块化基线 2 升到 3，SDK/core version 提升为 2.0.0，消费方明确要求 >=2.0.0,<3.0.0。package manifest schema 仍为 2，不随 actor input schema 机械升级。新 core 在 factory/prepare/注册前拒绝旧 schema；旧 core 拒绝新 schema。现有可选 preparation/availability 的兼容规则只适用于通过新门禁的库，不意味着旧 DSO 可混用。当前配置语法能保留的保留，内部 target 解析升级不要求用户手工编码 ID。

### 6. 消费者按真实路径迁移，不只替换 facade

完整路径和验收在 [actor-migration.md](actor-migration.md)，实施必须承接以下五组结果：

| 消费者 | 必须交付的迁移结果 |
| --- | --- |
| Bridge | 双向图片/相册、音视频、文件、动画/贴纸、QQ forward/card、引用/编辑/撤回逐项迁移；完整回执/provenance、媒体缓存/lease 与版本化重试；建议 schema 3→4，多输出/未知结果不重放 |
| Message Store | 原始归档与 typed 投影分开；`MessageStored` 仍只准入一次，保留旧键、raw、更新判断及通知关联，不强加 bot 查询 |
| Chat LLM | typed source reader、cache-only self/引用、统一 member/media/send；保留 image_id/session/sidecar/去重与 prompt，显式处理 QQ 视觉由 URL 到 bytes 的预算变化 |
| ExHentai + probe | 普通消息/topic 使用共同客户端；共享文件和 QQ typed forward 保留专有路径/lease；direct 模式用独立单消息调用保持其逐条继续策略，probe 单独更新构建 |
| actor template | 最小示例及 package/schema/SDK 依赖升级；不凭空加入网络逻辑，调度 ABI 仍 V2 |

关键区别：Bridge 一个逻辑转发遇首错停止；ExHentai direct 当前业务允许独立输出继续，必须用多个独立 `SingleMessageOnly` 调用表达，不能把整批塞进一次 `AllowSplit` 再悄悄改变行为。actor 业务规格和数据脚本留在 owner 仓库，本 change 记录 SDK 依赖和联合验收，不收回规格所有权。

## Risks / Trade-offs

- [把不透明 ID 误当新数据库主键或权限范围] → 版本化 codec、唯一编码和集中 legacy 映射；对历史缺失信息明确失败，在数据库副本验证相同 source/ACL/session identity。
- [统一模型被误读成所有 provider 功能都已实现] → 明确最低公共能力和 actor 逐媒体退出条件；未实现组合/格式列为阻塞，平台扩展不冒充公共保证。QQ URL 转字节的资源成本及 TGS 等未验证格式单列验收。
- [拆分产生部分成功、通知重复和安全重试误判] → 保留确认回执及未知标记，停止后续发送，逻辑结果与物理确认事件分离，通知不触发重发。
- [媒体读取越过资源或职责边界] → 调用方负责来源选择、业务授权、文件访问/挂载和最终路径；SDK 保留结构/协议校验、凭据隔离、既有 redirect 限制、实际读取/缓冲的逐项和总预算及 deadline。能力依据真实原生传输/上传机制发布，不以 SDK 路径授权策略安装为前置条件。
- [self 迁移与正在进行的 actor addressing 改动冲突] → 共享原 cache，不改变其刷新政策；按前序实施进度明确接口切换点，保持 actor 策略/历史 sidecar 和去重，不并行维护两种身份真相。

## Migration Plan

1. 审核本提案及 `actor-migration.md` 的逐 actor 计划；锁定有效前序基线、媒体/回执补充、显式预算与 Bridge 数据迁移范围，确认 self cache 剩余验收。不改写或归档前序 change；在各 actor owner 仓库承接业务任务。
2. 在独立工作区实现值类型、codec、客户端、平台适配和 schema 3 门禁，复用 cache。先完成集中 legacy decoder 与 Message Store 的一次 typed 通知，再按附件迁移各 actor；Bridge 多输出 ledger 只在副本验证，保留来源/历史不完整证据，不更改在线数据。
3. 运行现有构建/检查/测试与离线样本回放，不新写单元测试。编译和测试至少 6 workers，低负载用全部 CPU。core/SDK 用隔离目录；actor 仅使用冻结 installed SDK 和 `scripts/actor_build.py`，不得从在线 core workspace 构建 actor targets。
4. 更新 SDK exports、文档、maintained actor 依赖版本、registry 发布元数据及安装清单；不恢复已退休的旧 offline bundles/patch/replay。在脱敏数据库副本上验证身份/去重/引用/部分回执。发布前准备匹配的旧 core/SDK/actor/config 及数据备份；任何持久格式写入变化需另行列清迁移与逆迁移后再执行。
5. 只有明确部署授权后才停进程并整体切换匹配 core/actor；不能用 actor reload 完成 SDK/core 升级。回滚整体恢复匹配产物及必要数据备份，不删 session，也不自动重放未确认发送。

## Approved Decisions and Implementation Gates

用户已批准下列接口与逐 actor 迁移方向，并要求并行实施。批准不包括在线数据库迁移、部署或重启；内部编码仍需在调用方迁移前冻结：

1. 接受显式 `SingleMessageOnly/AllowSplit` 和首次失败停止、保留全部已确认回执的规则。
2. 接受 `SelfInfo` 迁移为 `UserRef + optional profile`，保留 action/cache 语义但不承诺旧 DTO 二进制/JSON 兼容。
3. 接受普通业务只操作不透明引用；内部 ID 字节编码在实现前冻结，不要求用户记住或拼接 `|` 格式。
4. 审核迁移盘点补出的 `Animation/TextStyle`、typed 相册/媒体缓存身份、输出 provenance/可复用媒体及调用方最终路径边界；明确 Bridge/Chat LLM 的跨安装媒体采用 bytes 路径及必填预算，不把它描述成无行为变化。
5. 审核 Bridge 显式多输出 ledger 升级、历史输出不完整的处理及各 actor 退出条件；core contract schema 3 与 Bridge 数据库建议版本 4 是两个不同门禁，均不授权在线执行。

以上决策进入实施；任务只有在验证后才勾选，发布仍单独授权。实现若发现需要改变以上语义，应先向用户报告并同步 design/specs/附件，不让并行 worker 自行改动共同契约。
