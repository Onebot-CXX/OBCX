## 1. 建立有效基线与回归资产

- [x] 1.1 按实现先后同步 `support-bridge-multi-installation-pairs`、`fix-bridge-conversation-scoped-message-mappings`、`complete-bot-component-migration` 的已完成 specs；检查不覆盖 multi-pair、schema 3、preparation hook 和已移除 live-bot 的约束。
- [x] 1.2 将前序配置规格中“遗漏字段使用默认值”的场景校正为当前显式字段要求；用现有 bot configuration tests 确认基线，不新增配置默认值。
- [x] 1.3 记录两个 surface、三个 recipe、13 个精确 wire action ID 及 request/result/error JSON golden fixtures；记录 Telegram uploader 缺失时的 action 集合。
- [x] 1.4 盘点根仓库、所有 `local_actor/`、SDK fixtures、metadata、offline bundles/patches/pins 中对全局 enum/client/config variant/contract schema 1 的依赖。

## 2. 公共与平台 SDK 契约

- [x] 2.1 实现显式拥有字符串的 `SurfaceId` / `ActionId`、语法验证、哈希/相等及 deterministic JSON；测试未知合法 ID、非法格式、无默认身份及非 enum ordinal 的 round-trip。
- [x] 2.2 提取平台无关的 installation/group/message reference、error、submission safety、result 和当前 common send/delete 契约，确保公共头不包含平台 DTO。
- [x] 2.3 在 OneBot SDK 下定义自己的 surface/action 常量、member/forward/file/poke DTO、codec 和 traits，保持现有 JSON/验证语义。
- [x] 2.4 在 Telegram SDK 下定义自己的 surface/action 常量，迁移 topic/edit（含 target/result）及 entity/media/photo/upload/fetch DTO、codec 和 traits。
- [x] 2.5 分离公共错误校验与平台 provider 脱敏规则，回归 token、proxy、tokenized URL 和 provider error 的保密及重试分类。
- [x] 2.6 用 baseline golden fixtures 验证拆分后的 public values、payload、references、error JSON 语义不变；测试 wrong-surface、topic、reply scope 和媒体限制。

## 3. 通用 gateway 与强类型调用

- [x] 3.1 定义无平台重载的 `BotOperationGateway`、operation envelope、SDK reply 及 generic supported-action query，公共接口不包含平台契约或 process capability registry。
- [x] 3.2 实现按 Request traits 配对 Result 的 typed invocation helper，支持编码/解码/validation 并保持 Actor `await_asio` 的跟踪生命周期。
- [x] 3.3 实现 common、OneBot、Telegram typed client facade（或函数适配），验证只 include 使用的平台即可调用 fake gateway。
- [x] 3.4 增加 malformed SDK reply、结果缺字段、解码异常和错误 envelope 测试；确保副作用完成后的 codec 失败仍为保守 typed failure，不制造安全重试。
- [x] 3.5 建立 gateway envelope/结果拥有权及 cancel/late-completion 测试，验证 request 和 result 在跨 executor / coroutine 挂起期间有效且只终结一次。

- [x] 3.6 实现独立 gateway 媒体 codec：以 `Json::binary` 直接转移 bounded byte buffers，拒绝内部数值数组，测试缓冲所有权/请求字节边界及无逐字节 JSON 膨胀；公共 DTO JSON 和 golden fixtures 保持不变。

## 4. Endpoint-local 注册及分发

- [x] 4.1 实现平台无关 operation descriptor 与 typed handler 注册 helper，绑定 action、codec、scope validator、只读/副作用分类及 handler，拒绝重复或不完整注册。
- [x] 4.2 改造 dispatcher 为 exact installation/surface + endpoint-local action 查找，不保留全平台 enum switch、请求 variant 或逐 action 虚函数。
- [x] 4.3 实现 envelope 与 payload action/installation/target/message/reply 的一致性校验；测试绕过 typed facade 的未知 action、冲突路由和伪造 payload 不触发 provider call。
- [x] 4.4 从同一 descriptors 生成静态 manifest 和 live supported-action 集合，测试排序、重复拒绝、未 seal/未 prepare 不可调用及声明/实现一致性。
- [x] 4.5 回归 admission 关闭、endpoint lookup 生命周期、异常安全性、取消、shutdown/drain 和不创建新 detached gateway 工作。

## 5. 平台 operation 实现归位

- [x] 5.1 将 OneBot endpoint、response parser 及注册列表移动到 OneBot 模块，注册现有 7 个 common/OneBot action 并复用当前 protocol/transport 行为。
- [x] 5.2 将 Telegram endpoint、response parser 及注册列表移动到 Telegram 模块，注册现有 common/topic/edit/photo/url-group/fetch action，保留媒体响应形状/数量和 mutation 确认。
- [x] 5.3 将 Telegram multipart uploader 与 upload handler 按模块组合注册；有 uploader 时声明实际依赖边，无 uploader 时既不注册也不宣告 upload。
- [x] 5.4 为每个生产 action 更新 mock transport 成功、provider rejection、malformed result、submission safety 测试，保持生产集合恰好 13 个唯一 action ID。
- [x] 5.5 验证 Telegram file response limit、URL/multipart group bounds、reply/topic/entity 保留及大媒体 gateway 路径没有额外 dump/parse 或无界缓存。

## 6. 模块 catalog、配置与 recipe

- [x] 6.1 实现显式构建、注入并 seal 的 `BotPlatformCatalog`，注册 exact surface/transport recipe，拒绝重复 key，不使用静态初始化注册或全局 service locator。
- [x] 6.2 定义 immutable process-only installation plan 和非秘密 public metadata 接口，使通用 runtime 不需要知道模块具体 connection 类型。
- [x] 6.3 将 OneBot WebSocket/HTTP 的 typed config、closed schema parser 和 recipe 移到 OneBot 模块，保留全部必填字段、单位、约束及诊断路径。
- [x] 6.4 将 Telegram HTTP 的 typed config、TLS/proxy/polling schema parser 和 recipe 移到 Telegram 模块，测试遗漏 action timeout/proxy字段被拒绝。
- [x] 6.5 改造 ConfigLoader 为公共表结构解析后调用 owning parser，移除中央 `BotInstallationSurface` 和跨平台 connection variant；测试 disabled bot 也受同一 schema 验证。
- [x] 6.6 按用户选择的方案 A 分离私有 loader/catalog/bot connection plan 与 Actor 可见配置/metadata；提供显式 actor-only 数据构造入口并迁移独立 Actor 测试，不新增 process-config SDK、不冒充完整连接验证；验证 public getters/视图和新 SDK 不暴露 token-bearing provider config。
- [x] 6.7 让通用 assembler 只消费 typed plan，移动 protocol/transport/event/operation/catalog 具体组件到所属模块，保留 DAG、rollback、reverse-stop 和 executor-last 行为。
- [x] 6.8 由 `src/app/` composition root 显式注册两个内置模块，注入启动/validation/reload；验证生产仍只允许三个 recipe，未知 surface/transport 与任意 component 配置都失败。

## 7. Command、ingress、generation 与指纹

- [x] 7.1 迁移 OneBot/Telegram command detection 实现到平台模块，通过 catalog 提供绑定后的 generic adapter 和非秘密 command target metadata。
- [x] 7.2 用 generic process-only command publisher 替代 `TelegramCommandCatalog*` 公共参数和 installation directory 的 Telegram 特例，保留 OneBot 无 publisher 的本地命令行为。
- [x] 7.3 删除 coordinator、generation builder、main ingress 的平台 switch/else fallback/Telegram config 检查，使用 module metadata，保持现有 command-route platform 字符串和事件格式。
- [x] 7.4 将 actor scalar/collection bot-reference 验证迁移为 exact surface ID 与 catalog/metadata 校验，保留 pair identity、enabled、alternative、reference 和 cardinality 约束。
- [x] 7.5 更新 process fingerprint 覆盖完整规范化配置、enabled、private credential digest、recipe/manifest 和原线程预算；测试 secret-only、disabled-bot 变更也要求 restart。
- [x] 7.6 回归无 provider I/O 的 validation-only、复用相同 gateway/installation 的 actor-only reload、不可用 capability 的候选拒绝。
- [x] 7.7 回归命令 exact bot target、RE2 normalization、aggregate activation-only publication、continue/consume、失败重试与 shutdown。

## 8. Actor SDK 门禁及消费者迁移

- [x] 8.1 将 actor contract 生成和加载切换到 schema 2，保持 scheduler ABI generation 2 及所有现有 input/command/config 字段，未通过门禁前不得调用 factory/preparation。
- [x] 8.2 添加当前 schema 2 与未知 schema 的 contract gate 测试，并验证未知 schema 在 startup/validate/reload 中不会执行 factory。
- [x] 8.3 迁移 Bridge 的 platform DTO、direct sends、lookup/media/edit/command 路径和 fake gateway，保持 exact pair/installation/conversation 及 mapping 单写入所有权。
- [x] 8.4 迁移 Bridge retry callbacks 到新 typed adapter，回归 schema-3 restored retry identity、possibly-submitted 停止、mapping-before-cleanup 和 generation retirement。
- [x] 8.5 迁移 Chat LLM 到 common send 和 Telegram topic facade/fake gateway，验证 source installation、topic/reply、proactive send、command completion、reload/shutdown 不变。
- [x] 8.6 重建并更新 Message Store、template、独立 actor fixtures 和 actor metadata 中的 exact surface constraints，确保所有发布 actor 使用 schema 2。
- [x] 8.7 更新其余 root tests、benchmarks、SDK 示例及工具引用；删除旧全局 `BotSurface`/`BotAction`、`action_ids::all`、compatibility matrix、umbrella header 和 all-platform client，保留历史文档的明确历史标记。

## 9. 编译隔离与可扩展性证明

- [x] 9.1 导出独立 common/OneBot/Telegram SDK targets 和明确的安装 header 集合，组织通用 runtime 与平台 implementation target 的单向依赖，不添加带默认值的 feature toggle。
- [x] 9.2 新建 common-only installed SDK fixture，在完全没有平台 headers 的 include closure 下编译并执行 fake-gateway common operation。
- [x] 9.3 新建 OneBot-only 和 Telegram-only installed SDK fixtures，不安装对方 SDK，测试各自 request/result codec 和 typed invocation。
- [x] 9.4 在单独 translation unit 实现仅用于测试的 `test.echo` module；只链接通用 runtime，完成 parser、manifest、recipe、gateway dispatch 和 stop 测试，无 core source 修改。
- [x] 9.5 增加 include/target architecture checks，禁止 common→platform、platform→other-platform、public→private 依赖，确认测试模块没有进入 production catalog。
- [x] 9.6 对新 gateway 调用执行重复取消/停止/销毁和适用 sanitizer/concurrency gate，验证没有旧 actor DSO 回调或 executor-after-destruction。

## 10. 发布、文档与最终验收

用户已明确暂缓发布准备：10.1、10.2、10.6 保持待办；已有构建/测试验收对应当前工作树，不代表旧离线源码包已兼容新 SDK。

- [ ] 10.1 更新 SDK exports、兼容版本约束、actor registry metadata/index 及 standalone smoke，明确 schema 2 配套重建要求而非承诺旧二进制兼容。
- [ ] 10.2 更新 offline actor bundles、adaptation patches、restoration revision pins 和文档，验证固定输入可重放为匹配新 SDK 的 actor revisions。
- [x] 10.3 更新架构/作者指南，给出 OneBot 与 Telegram 模块组合、各自 typed 调用、ID语法与生产支持的区别，以及禁止 provider passthrough 的规则。
- [x] 10.4 更新配置/部署示例，保留原 canonical TOML 和全部显式字段；说明 core/actor 整体重启升级及整体产物回滚，数据库无本次迁移。
- [x] 10.5 从全新 install prefix 运行 SDK/旧 DSO 门禁及独立 Bridge/Message Store/template/Chat LLM 测试，不能复用旧已安装 headers 掩盖依赖。
- [ ] 10.6 从临时空目录离线恢复 actor sources，运行构建和跨仓库 conformance，核对 bundle/patch/pin 一致性。
- [x] 10.7 运行根构建、完整 CTest、架构/配置 inventory、actor 业务回归及安全的 `--validate-config` 验收，记录结果和没有新增数据库/配置默认值的检查。
- [x] 10.8 从根目录运行 `nix fmt` 并复查差异；若提交，须每次提交前重新运行且对未签名提交提醒补 GPG 签名；最后运行 `openspec validate decouple-bot-platform-contracts --strict`。
