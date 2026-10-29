# Implementation status

## Authority and current state

用户已批准 design/actor-migration 的方案，并明确要求 subagent 并行实施。允许实现与隔离验证；不部署、不替换在线 core、不重启、不修改在线数据库或用户 prompt。不新增/改写单元测试，不新增配置默认值，不提交或推送。

本轮起点：75 个任务；用户追加完整OneBot11标准后新增9项。当前已核实 12/84（包括标准清单/范围确认17.1）：批准/基线任务 1.1–1.5、公共引用值 2.1、typed 消息值 3.1、独立复核通过的 Bridge schema/model 11.1、模板 metadata/回复文档 14.1–14.2，以及隔离core/SDK构建检查16.1（不包含运行验收）。其余代码/运行验收仍分开跟踪。既有工作树修改属于有效输入，不能 reset/stash/覆盖。所有 writer 采用独立手工 worktree，基线包含当前 tracked + untracked 工作树内容，不只采用 HEAD；原仓库 index 保持原样。

## Baseline evidence

- Core HEAD: `5f0ab9b`；Bridge HEAD: `ba189a0`。完整 ref、snapshot tree 与原 index hash 记录在 `/home/vollate/Codes/OBCX-work/domain-api-20261026-224822/baseline.json`。
- `auto-detect-chat-llm-addressing/tasks.md` 当前全部已勾选；scout 核实 addressing-core/sdk-verified 产物和既有 21-case replay/52-request token 日志确实存在。它们是前序历史证据，不是新契约的运行验收。`packaging/actors/self-identity.md` 的 cache-only、generation fencing、刷新合并及仅该项 300000ms 已批准默认政策保留。
- `decouple-bot-platform-contracts` 仍有 10.1/10.2/10.6 未勾选。当前 `packaging/actors/README.md` 已声明旧 offline bundles/patch/replay 已退休；本次发布任务按 maintained repositories + packages.toml/registry 承接，不恢复过时 bundle。
- 当前 Bridge repository schema 为 3，逐输出 schema 升级需保持离线/独立授权边界。
- 机器 20 cores、初始低负载；编译/测试至少 6 workers，低负载使用 20。actor 只通过 scripts/actor_build.py + 新冻结 installed SDK 构建。

## Implementation topology

分类：**multi-seam**。公共 SDK 值/codec/client、平台 adapter、actor 输入/ABI、Bridge 持久化和各 actor 业务拥有独立契约/文件边界。先冻结共享接口；各组件产出 durable handoff 后才由 integration-only owner 合入，不让一个 worker 独占全部实现。

| Lane | Repo / explicit cwd | Exclusive ownership / decision | Isolation / authority | Gate / handoff / independence |
| --- | --- | --- | --- | --- |
| sdk-contract | core: `/home/vollate/Codes/OBCX-work/domain-api-20261026-224822/sdk-contract` | 公共领域值、ID codec、消息/事件/query DTO、BotClient 与 gateway codec；冻结后交平台/actor 使用 | 独立 snapshot；仅指定 core/bot 领域文件与契约文档，禁止平台/actor/CMake/runtime/ABI 修改 | common-header 编译/codec 静态核对；完整 API+变更文件交接；不负责平台执行 |
| bridge-ledger | Bridge: `/home/vollate/Codes/OBCX-work/domain-api-20261026-224822/bridge-ledger` | schema/存储 delivery/source/output/provenance/retry ledger 与显式 v3 导入 | 独立 snapshot；仅 repository/storage schema seam，禁止 forwarding/media/runtime 修改 | 离线/已有工具可行性核对、无在线 DB；列出 API 与迁移证据；不依赖尚未冻结的 BotClient 实现 |
| integration-scout | core `/home/vollate/Codes/OBCX`，只读 | 前序证据、构建/SDK 环境、runtime/ABI/command/MessageStored 精确接点 | 不修改 source/config/DB，不执行 build/deploy | 已交接具体接点/命令/缺口；不重复 SDK 或 ledger 工作 |
| package-contract（父会话） | core `/home/vollate/Codes/OBCX-work/domain-api-20261026-224822/package-contract` | SDK version、package validator/input-schema 与生成 JSON schema；构建闭包/exports metadata | 单独 snapshot，只改 CMake/package tooling/schema，不改平台/runtime 实现 | 现有 package-tool 验证；独立于待完成的 SDK 运行代码 |
| template（父会话） | `/home/vollate/Codes/OBCX-work/domain-api-20261026-224822/template` | actor package/CMake/文档；现有 handlers 不改 | 单独 snapshot，无其他 writer | TOML/ABI/schema/source-unchanged 检查已通过；冻结 SDK 编译待执行 |
| core-runtime | `/home/vollate/Codes/OBCX-work/domain-api-20261026-224822/core-runtime` | process binding/evidence、runtime/command/actor schema、一次 typed projection、配置/legacy facade | core/common parser；另明确授权两份 self_identity_cache 文件作为 SDK v3 override；其他 SDK 输入只读，禁止平台/actor/CMake/tests 改动 | 读取器/进程资源接线已分别审查通过；20个核心/helper输入通过，两个未整合旧recipe仍失败；父会话负责整包整合 |
| onebot-adapter | `/home/vollate/Codes/OBCX-work/domain-api-20261026-224822/onebot-adapter` | forward/origin/legacy topic 保持；最终路径修正获 a1bffe4c source review 通过，撤销旧授权/注册门槛 | foundation v14/73 份输入只读；不写其他组件 | 新工厂导入后51/54编译通过，仍是三个未改写旧API输入失败；23个原生产TU及三个共享TU通过，运行/完整SDK仍开 |
| telegram-adapter | `/home/vollate/Codes/OBCX-work/domain-api-20261026-224822/telegram-adapter` | cached-sticker/origin保持；HttpSource获3e3bf8ff通过，LocalFile注入/完整准备六文件获50e37f09通过 | foundation v12/71 份输入只读，独立 worktree | LocalFile增量47/47编译、六文件前后像及71份foundation核实；runtime/整包SDK/媒体验收另计 |
| store | `/home/vollate/Codes/OBCX-work/domain-api-20261026-224822/store` | Message Store 源码/metadata/docs，不改 tests | 独立 actor worktree；不在旧 SDK 或 mutable core headers 上编译 | d08e67f4独立source review无finding，五文件hash核实；冻结SDK build/离线运行验收待执行 |
| chat-llm | `/home/vollate/Codes/OBCX-work/domain-api-20261026-224822/chat-llm` | source reader/service/live/history/addressing 调用链已source review；send/member/media工具尚未迁移 | 独立 actor worktree；新版86文件source snapshot.v2和旧84文件均保持只读，均非installed SDK | helper ba9d9b47及接线5f8954f9 scoped通过；11个最终hash核实，未构建完整actor |

## Active run and decisions

- Foundation workflow `83ac01d1-ce49-4be6-b415-78c3dfc38b74` 已完成；mission: `7b63e8dc-d902-4665-be33-bf35395c24b4`。SDK child `56b988f4-7591-4c06-995b-9cda95cb3526`、ledger child `123c01a5-9199-4e27-bf2e-6f8e6f400aef` 与 scout `25799b20-bdee-4449-aca5-d21a680c41cd` 已交接；完成交接不等于实现已验收。独立复核与修复状态见下节。
- Scout 报告：`/home/vollate/.pi/agent/sessions/--home-vollate-Codes-OBCX--/subagent-artifacts/outputs/83ac01d1-ce49-4be6-b415-78c3dfc38b74/foundation/integration-baseline.md`。已明确 SDK/runtime/platform/CMake 的文件所有权；无 source 修改或构建执行。
- 内部 ID 语法冻结为 `obcx1:<tag>:<lowercase hex payload>`，payload 是 32-bit big-endian 长度前缀 UTF-8 surface/native kind/1..8 components；每 component ≤1024 bytes，整个编码 ≤4096 bytes，两种边界同时约束。tag 区分 conversation/user/message/media/media_identity/media_group；具体合法 native kind/组件由 platform codec 验证。公共 SDK 仅做确定的结构/规范化校验；process 私有、注入式 codec registry 在 ingress/config/dispatch 验证 native scope，不能要求普通 actor 注册 provider codec 或依赖 provider SDK。
- Bridge v4 保留 v3 mapping/media-group/retry 作为只读证据，旧对应 mutation API 在 v4 fail-closed；user/sticker/heartbeat 不无故禁写。离线导入先隔离所有旧 retry，后续 SDK binding 必须提供有确证的放行操作；这不是最终“一律禁重试”政策。不能构造假的 SDK ref；正常启动/prepare 不自动升级 v3，必须显式离线迁移。
- 发布兼容版本采用 SDK/core `2.0.0`，actor 依赖 `>=2.0.0,<3.0.0`，只提升 input contract schema 到 3，调度 ABI 仍 V2、package manifest schema 仍 2。版本改动由 integration/各 repo owner 执行，不修改原运行实例。

## Prepared next-wave isolation

TG 当前为71份 foundation 输入（v12）；QQ 为73份（v14）。已包含 reviewed origin、完整 reply budget、common-client 生命周期修正、新 topic fact、共享 HTTP 完成清理、最终路径责任注释及读取器/factory/assembler接线；QQ 另有 shared WebSocket 两文件。旧 SDK v3 不能覆盖这些输入。

导入均在 writer/reviewer 退出或明确无编译暂停时逐前像/hash 校验，每次导入前后当前 index 字节未变。历史物理 index 漂移已独立核实390条 staged mode/blob/stage 与 snapshot 一致，不还原、不推断原因。

Store 源码已交接。Chat LLM 七文件 helper 已审查，topic 阻塞已在共享/两端 source 层解除；入口接线已获5f8954f9独立source review，11文件hash核实；新86文件与旧84文件只读源快照均不变，都不是 installed SDK。ExHentai/Bridge 业务迁移尚未开始。独占边界与下一 gate 见 integration-seams.md；活动写入/审查/编译期间不替换输入。

## Next waves

1. 审查 SDK/ledger 交接，固定对外类型与 codec；独立整合 owner 处理 glue，不扩展产品范围。
2. QQ、TG 和 core ingress/dispatch/ABI 各在独立目录实现，统一编译后冻结 SDK。
3. 各 actor 独立迁移，Bridge runtime/media 使用已冻结接口和 ledger；更新本仓库 owner-local 任务、package/registry 要求。
4. 复用现有检查/回放与脱敏副本验证；新单元测试/测试重写需要额外授权。独立 reviewer 按实际 diff 检查，修复后再验证。
5. 仅在证据满足时更新 tasks 勾选；剩余 live-platform 验收、在线 schema 执行和部署不自动完成。

## Parent-side checkpoints

- Template 隔离改动：package 0.2.0 / SDK >=2.0.0,<3.0.0 / input schema 3，CMake 和文档同步；`src/` 相对 snapshot 无变化。owner-local `docs/domain-sdk-migration.md` 已承接任务，真实新 SDK build 仍未执行。
- Package-contract 隔离改动：CMake SDK/core 2.0.0，Python package validator 的 actor input schema 3；使用现有 generator 重生 schema，实际变化仅 package/resolved-packages 两份 JSON。旧 actor 用旧工具可通过、用新工具被拒绝；新 template 和未改动的 path-mapping library 用新工具通过。日志在 workspace `validation/`；这不是完整 loader/factory gate 验收。
- 原 `packages.toml` 由 `.gitignore` 忽略，是本地 build 输入，未包含在 Git snapshot，也未被修改。已建立 `core-only/packages.toml`，显式 empty roots/sources/providers + production profile，通过现有 resolver；后续 core-only 构建不必先完成所有 actor metadata，避免循环依赖。actor build 使用独立复制/改写的 workspace 和 provider 证据。
- 观察到 Chat LLM 原仓库的部分 unstaged/untracked 内容后来被暂存；HEAD 不变，逐 blob 比较与捕获的工作树快照差异为 0。保留最新暂存状态，不能还原原 index；集成时逐文件比较前像，禁止覆盖后续变化。父会话未操作该原 index 的 staging/reset。
- Ledger 使用已有冻结 `build/addressing-sdk-verified` 做 storage seam 隔离编译，`bridge-ledger-build/build-final.log` 通过；父会话修复独立复核问题后再次通过 `actor_build.py build --jobs 20`，日志 `bridge-ledger-build/build-review-fixes.log` 共 21 个 build steps。helper 确认 SDK unchanged，无部署/重启。这只是前序 SDK 编译，不是新 schema-3 SDK 或 DB 运行验收。
- Template README 已加入只用 `BotClient/MessageReceived/SendLimits` 的原会话单消息回复示例；所有限额显式传入，完整返回 SendOutcome。既有 template-reply 编译输入通过；installed SDK 检查仍待完成。package-contract `src/CMakeLists.txt` 已登记公共 domain/resolver/legacy 头、QQ forward/shared/special 头、TG domain operations/views 及当前七个新增平台 .cpp（含已审阅 QQ legacy_compatibility.cpp 和 TG legacy_binding.cpp）。退休 QQ image.hpp 和 TG legacy types/operations 不再安装，私有 native codec/policy/binding 不安装。此前60个选定 export 的静态内部 include 检查无缺失。随后登记 public forward_outcome.hpp（不安装 forward_policy.hpp），并向 bot_common_sdk 传播必需的 -freflection；TG阶段结束后，更新的61头直接include闭包检查通过；扫描器首次把同目录json_utils.hpp误判为根路径，修正解析顺序后通过，未改头文件规避。真实 installed SDK 与 bot_*_sdk target 链接闭包仍未验证。后续核查确认公共ReferenceResolver是纯虚接口、析构inline，不是待链接的具体函数；真正需核查的是QQ/TG公共out-of-line helper。

## Independent review and corrections

- Review workflow `2e5373ad-8f6f-4d35-9422-c900db129222` 已完成，报告目录 `/home/vollate/.pi/agent/sessions/--home-vollate-Codes-OBCX--/subagent-artifacts/outputs/2e5373ad-8f6f-4d35-9422-c900db129222/reviews/`。两条 lane 均有具体阻塞，未因 compile pass 直接验收。SDK 23 个原编译单元的 `.final.status` 均为 0；未执行新 runtime checks。
- SDK 接受两项 P1 与一项 P2：畸形发送结果的恢复路径缺少聚合预算/有二次复杂度及预检查前 JSON 复制；公共 ADL restoration 未在 binary copy 前检查 request budget、公开 encoding 未检查聚合 wire bound；一个畸形 detail 字段会丢掉其他有效 metadata。保留已审阅的 DTO 布局/variant 顺序，原 SDK writer 仅修复 codec 内部路径并重新编译；之后由原独立 reviewer 跟进。
- Bridge 接受一项 P1 与两项 P2：opaque bytes 直接写 JSON string 不支持非 UTF-8；候选 unique-key 校验接受 partial index；旧表 guard 只检查名称。父会话在已退出 writer 的 lane 修复为独立 `storage_format=1/bytes_hex` 无损小写 hex envelope、非 partial unique index、table + exact canonical trigger SQL 校验，更新 owner-local 文档并完成上述复编译。跟进 reviewer `770aeb89-8e23-4d43-bc4f-b6dc0903a653` 已关闭三项 finding，结论 OK with notes；没有运行 binary round-trip、SQL validation 或 DB 初始化/迁移。模型任务 11.1 已勾选，运行整合/恢复仍未验收。
- Workflow `f0067383-fb13-4858-88b2-86a70ceeb1ca` 已完成。SDK 首轮三项 finding 关闭后，跟进发现 factory-backed 值/较短字段仍在检查前复制；`04b8c180-3a75-422c-832e-c4f2f971de1b` 已完成该修复与独立复核，结论 OK with notes。共 23 个 foundation 文件、16 处本轮头文件更新，无新字段/variant/限额；25 个既有编译检查最终状态全零，未运行 runtime/installed-SDK 验收。
- 可供只读 reviewer 使用的 index-relative diff、精确新增文件清单、SHA256 在 workspace `review-inputs/`。HEAD-based watchdog diff 不能代表本轮增量；原 source/index 未被重置或重新暂存。

## Runtime handoff decisions

- Runtime child `0bdc3450-617c-490e-b394-d783029618aa` 草拟 process-only `platform_domain_binding.hpp`，由平台注册纯 normalizer/native codec/legacy 转换；共同 ingress 给原 envelope 附 typed projection，不二次准入。父会话批准方向，要求历史 full-event 恢复使用有记录的 receive/ingestion 时间，不以 now/0 或 native sent time 冒充；TG legacy self=0 不是 observed-self 证据。
- Reply relationship admission store 没有既有可复用实现：批准每 installation 共同 domain 配置必填正值 `reply_evidence_max_entries` 与 `reply_evidence_ttl_ms`，无默认值；共同 parser 负责、计入 fingerprint、有限容量/TTL/条目与生命周期，未准许改在线/local_config。SDK 自缓存政策不变。精确配置结构随 runtime 接口交接冻结。
- 配置/历史 actor consumers 使用新的公共 `reference_resolver.hpp` pure read-only facade，不包含私有 binding/native codec。该新文件归 runtime owner，SDK foundation 既有文件归原 owner；字段/签名交接后由父会话加入 SDK exports。恢复不会授权 reply I/O。
- Confirmation 需要关联 actor 预持久化 attempt，不能只靠 dispatcher 随机 ID/事后 MessageRef 匹配：SDK writer `c3ea36d8-297a-4f32-bd06-98d1ca0ccd92` 获准新增 `OperationCorrelation{attempt_id}`、可选 envelope metadata 和显式 BotClient send overload。body/outcome DTO 布局保持不变；correlation 不是授权/provider idempotency/隐式去重。runtime 消费新字段但不写 SDK 文件，完成后由父会话传播。
- 五个 actor 隔离工作树均有 `docs/domain-sdk-migration.md`，ExHentai 包括 probe；其中明确保留/替换/扩展/阻塞与 registry metadata。已读两份 registry README：独立 `local_actor/actor-registry` 是 publication source，core `actor-registry/` 是 development-metadata-only conformance mirror，均未发布或伪造下载资产。

## Latest runtime / platform / actor gates

1. **QQ forward 已 source 验收**：首轮e8d9ff75的P1临时 facade 生命周期、P2被拒 provenance key 未累计计费由884e37ec关闭。父会话核实基础47+修正47个编译状态及六文件最终hash；不是运行/observer/actor验收。
2. **历史 topic 已 source 验收**：共享f59bb8b6/15输入；QQ29cc1d73/47输入；TGfc56776f/38输入。native topic 与旧 scope selector 分离；完整归档才能推导未知 caller hint。三份共享文件已导入 runtime/两平台。
3. **公共客户端生命周期已 source 验收**：6f53b9be确认静态 coroutine 不保留 facade this，gateway仍须存活；父会话25 SDK+17生产/实例化输入通过，已导入两平台。先前仅格式检查失败已修正。
4. **共享 HTTP/TG 组合 source gate 已通过**：3d4f3da2审查共享修正，142f2479关闭原29ef2bf2取消slot竞态；TG44输入、五文件sticker修正与六个原生origin文件hash核实。core/QQ组合输入分别21/51编译通过。执行级并发/关闭验收未做。
5. **workflow843b12d6/e068b1d3已完成**：Chat source接线5f8954f9、Store d08e67f4均无finding；父会话核实11/5文件hash与两版只读快照。完整actor的send/member/media、媒体读取/上传资源、隔离全量link/installed SDK2仍开。总任务10/75；最新strict OpenSpec和主/package whitespace检查通过，未部署/合入原源码。

### Earlier handoffs — historical gate states

- Workflow `2c574368-94cd-4c60-a763-bfcb5bb2fbe0` 已完成且八份交接已读。source_bot 最终修复、single-output recovery 和 generic effect seam 均独立复核 OK with notes；25/15 编译状态由父会话核实全零。QQ/TG 分别 BLOCK（8/7 finding）；Chat LLM 明确 partial，保留旧 IDs/fingerprints/prompts，未伪装完成。
- Workflow `cf7718d5-fa1c-4da2-9dc8-23325a1c574e` 已完成。borrowed-body helper 独立复审通过，已导入 runtime/两平台/effect-budget。TG 修正及实际 cached-sticker policy 消费获 `11f60e34` OK with notes；六项原 finding 关闭，origin/cursor 仍未关闭。
- QQ 首轮 corrective review 发现三个局部回归，后续 `a255135b-231b-4076-8696-7cd635de80a6` 修复并由 `d3878290` 复核关闭：显式归档空 optional 身份、所有 forward node 的原生 mention/caption/reference 预检、专有 view 编码/解码界限对称。父会话核实 44 个既有编译状态全零；当时整体 QQ forward、origin、媒体仍未验收；旧 source authorizer 计划已被最新调用方最终路径决定撤销，不再是待安装门槛。
- Legacy facade 两项 P1（遗漏图片秘密脱敏证据、provider 无法借用检查 opaque fingerprint）在 `ece4ee32-0537-49af-aa1e-53f75d8c85a3` 修复，`8ee4f67a` 复审通过。父会话核实 15 个编译状态全零，五文件按前像/hash 导入 core-runtime/QQ/TG；已登记 legacy_compatibility.hpp export。两端原生派生/精确验证已在后续 scoped review 通过；actor 脱敏/指纹保持仍需实现和审查。
- Origin 的排队 refresh/断线 P1 已由 `e862b672` 复核关闭：connection eligibility 与 epoch 同步修改，loader/publication 独立拒绝断线结果。父会话核实 15 个编译状态全零，并在对应 writer/reviewer 停止后把完整十文件增量导入 core-runtime/QQ/TG。QQ native 捕获/退休已有后续 source review；TG 状态/游标提交与实际并发验收仍未关闭。
- Wrapper-aware budget workflow `a194c2de-74ae-4c0d-be60-39d9c4b137c0` 已完成，`a1c07b22` OK with notes，父会话核实 25+15 编译状态全零。真实预算包含 OperationReply 的 `{ok:true,value:<Result>}` 外层；新增 for_reply 与 envelope-aware overload，保留旧两参数 standalone SDK 语义。四文件已按前像/hash 导入 core-runtime/QQ/TG；完整 API 在 effect-envelope-budget.md。QQ forward/TG client 的实际消费仍待完成。
- Workflow `ee10987d-a032-42e4-9b3f-d7448da72bc4`：QQ native facade 已获 `f67b7d56` OK with notes，父会话核实 45 个编译状态全零；保留 structured/CQ 两种归档、旧指纹/原始序号/脱敏序列与借用完整预算。TG native facade 获 c26689e8 OK with notes，父会话核实38个编译状态全零及四个当前文件 hash。TG 对 malformed native 身份明确不可用；sender_chat 的 compatibility from 仅留旧 display/fingerprint，不能伪装真实作者。历史 reply 展示事实与严格 target/author 证据分离，不从旧显示 ID 重建权限。
- 父会话在 core-runtime 接入公共 BotClient 的两处 reply-aware decoder；预算在 gateway suspension 前建立，共用同一 allowance。初次仅格式检查失败已修正，最终 25 SDK + 15 生产/实例化输入全部通过（/tmp/obcx-reply-client-check/）；d54f0257 独立复审通过，已导入 QQ/TG。此前仅 body/legacy/origin/旧 effect 的合并编译也为 15/15，通过不代表完整 link 或安装 SDK。
- Workflow `27169376-6c6f-4954-b9e7-28be019d0293` 已完成：QQ native origin 获58a31d65 OK with notes，父会话核实九个文件 hash 和47个 final 编译状态全零。初次编译命令引用不存在的 write-queue 路径，修正后完整重跑；历史失败日志保留。真正的 originating-I/O 缺口在 source 层关闭，不宣称并发/取消/provider 运行验收。
- 父会话独立补齐 shared WebSocket 每次读取前的 handler factory，旧四参 API 保留同一 callable 状态；writer/close/queue 代码不变。959cefd6 workflow 的44961055审查通过，17个编译输入通过；在 QQ writer 显式无编译暂停时导入两文件及 common client，未更改 QQ-owned 文件。
- Workflow `04534b20-5177-4184-8fde-a7a123c345df` 已完成：QQ首轮两项finding由后续ca0b2402关闭；TG原生六文件及38输入核实，29ef2bf2指出的共享取消P1由62ccf6b8修正/导入；Chat helper获ba9d9b47 scoped通过，明确未接线。首次 nested-async 脚本未启动子任务，后按同工具支持的Promise链重提。所有编译池共用 `/tmp/obcx-domain-compile.lock`，至少6、低负载20 workers。

- 平台/Store/运行时复核 workflow：`0fdc7d3e-731a-423f-960d-ea7cf36c6715`。Store child `e409056a-db1d-4336-8fa3-86de15e3f341` 已交接；父会话检查其 12 行新增/2 行删除的 source delta，旧 SQL/四键/失败和 emit options 保持，typed projection 不进入 archive fallback。package 0.2.0 / SDK2 / schema3 validator 通过；Bridge/Chat LLM 必须要求新 producer。历史 reader 需投影既有 `created_at`，不能以 sent time/now/0 替代。
- Runtime 首轮独立复核发现 P0（无快照时 expected-account 冲突未清回复证据）和 P1（事件 factory/encoder 归档复制前无聚合检查）。修复 workflow `3849faf4-5c79-486e-b144-7321a1456801` 已完成；保留 writer 最新为 `e764cc9d-87f5-4c51-b945-5208a85e49fc`，reviewer 为 `a11b03a3-93c1-46e2-9a44-82371d1d2138`。
- P0 及 observation→token 的额外竞态已复核关闭：原 cache 的同步 generation hook 覆盖所有身份/连接/关闭转换，main 使用原子 `capture_message_admission_generation(optional<UserRef>)`，不增加第二缓存。cache 两文件是明确 runtime override。归档聚合检查已关闭，但 reviewer 发现 MessageStored 的短 `source_bot` 仍先复制后比较；父会话已改为借用字符串、先验证再拥有，15 个既有生产/实例化编译单元复编译全零（`/tmp/obcx-runtime-source-preflight/`），reviewer `bdfa29b3` 已关闭最终小修复，OK with notes。该关闭不涵盖随后独立发现的平台 originating-I/O account token 缺口。
- **路径责任修正（最新用户确认）**：此前候选的 process-private MediaSourcePolicy / shared-mapping installer / 共享文件注册门槛已被否决，不再是待补配置。SDK 接收调用方最终路径，不映射、不推断文件系统、不强制 OBCX-host 可见；外置 obcx.path-mapping 与挂载、授权、lease/cleanup 留在调用方。proposal/design/actor-migration/specs/tasks 已同步；公共 domain_media.hpp 仅改责任注释/诊断，25个既有编译输入通过（/tmp/obcx-path-boundary-sdk-check），结构/codec 不变。workflow6470d61c 的 QQ 原生撤除、TG 局部边界修正已由 bc4b7f6c 独立复核通过；公共契约两P1/一P2残留由94a5c605关闭。QQ接受最终绝对POSIX路径，其他路径表示明确不支持，不作filesystem推断。TG的HttpSource和LocalFile有界获取均已source审查通过，读取helper、进程资源和原生消费已分别接线/复核，尚需隔离整包集成与运行验收；保留实际读取/缓冲预算、凭据隔离、作用域和真实 effects，不以新名称重建部署策略门槛。forward 只执行完整构造的 typed nodes；无法证明内容的 message-reference nodes 在首个副作用前明确拒绝，不开任意 get_msg。common ContentSlice 不伪装 nested node 索引；确证 node provenance 留 provider typed details。
- TG/QQ 的多步骤发送使用既有必填 action_timeout 作为一个单调总预算，准备和每步共享剩余时间；不能超时后遗留无主 sender 或丢确认。专有 single-output sticker/forward 同样需要完整 effects；注册型 message-producing extension seam 已交接并由 `de36f7f2` 复核通过；不以 outer success 冒充 inner SendFailure 成功。QQ full-wrapper budget、稀疏 node provenance 与客户端 source gate 已由后续884e37ec关闭；TG完整reply/客户端补齐获142f2479通过，运行级效果验收仍开。
- 专有内容可经明确命名的纯 typed archive-restoration 扩展恢复，验证 source/scope/记录时间及输入界限；不做 I/O、不授予 reply 权限，普通图片/文件仍只读 MessageReceived/MediaRef。这里不引入 raw-content sender 或第二事件流。

- Chat LLM 较早 partial 覆盖metadata、必填image_read_max_bytes、BotClient.get_self和created_at；后续七文件helper保留指纹/脱敏/身份并获source review。父会话核实当前七文件hash，未以签名已变但调用未接线的候选冒充可构建actor。新scope接口/两端证明与snapshot.v2已提供，source调用链已获5f8954f9通过；send/media/tool与冻结SDK构建仍开，prompt/旧键不改。

## Final-path correction evidence

- 父会话逐项核实 QQ 13个文件状态/前后像、TG 两文件前后像，以及69/67份 foundation；仅在两端 writer/reviewer 停止后导入已复核 domain_media.hpp，索引保持不变。该阶段清单为 QQ v13、TG v11，后续读取器接线更新为 v14/v12。
- `/tmp/obcx-final-path-postimport-check` 保存合并后编译：QQ48/51（23个生产TU全部通过，三个旧注册/策略API输入保留并失败）；TG44/44。未新写/改写测试或样例来掩盖接口删除，不把该混合池称为全通过。
- package-contract/src/CMakeLists.txt 改导出 media_path.hpp，取消 shared_media.hpp；`review-inputs/package-contract.export-preflight.v3.json` 的61个头静态直接include闭包通过。该检查不等于link/installed SDK验收。
- `review-inputs/path-boundary.accepted.json` 与两端 `*.final-path.accepted.json` 记录限定source验收，`final-path.postimport-compile.json` 记录后续编译。总任务仍10/75；无部署、在线数据/配置变化或提交。

## Telegram acquisition follow-up

- `be418527` 的两文件 HttpSource 增量已由 `3e3bf8ff` 独立审查通过；父会话读取精确patch，核实当前前/后像、44个编译状态、67份foundation及原索引。证据为 `/tmp/obcx-tg-http-acquisition-d00eu647` 与 `review-inputs/telegram-adapter.http-acquisition.accepted.json`。
- HTTP获取共用原绝对deadline/取消并等待完成机制；已知大小预留、native引用大小和实际下载字节累计计量，完整准备先于任何发送；不继承bot/proxy凭据，不重试/跟随重定向。只声明source/compile，不宣称provider、并发或installed SDK验收。
- `23c648df` 查明既有 BlockingExecutor 的actor-disabled缺口及关闭/排空顺序。`3090ee39` 已交付独立两文件读取helper，七个生产编译输入通过；`ab1f2748` 发现初始化异常未通过handler完成的P1，已由 `e0dd15f5` 修正，`3f17bc6b` 回审关闭；父会话核实精确修复链与七项编译，在双方停止后把两文件导入core-runtime，索引未变。helper只消费注入的既有pool、不建新pool/配置/策略；进程资源/工厂八文件接线 `9b1ed9a6` 已由 `f827de67` 审查通过，父会话核实前/后像、20个成功编译及原索引。core-runtime工作区两个尚未整合的旧原生recipe仍因缺少既有DomainRuntimeOptions参数失败，未加默认值或改fixture。只在正常启动且actor graph未配置、不提供pool时复用原budget算法补齐进程pool，不提前到validation-only路径。无法强行中断阻塞内核调用，取消/截止后仍须等待已接受任务完成，不能声称硬墙钟返回/关闭保证。

- helper验收/导入记录为 `review-inputs/bounded-file-reader.accepted.json` 和 `core-runtime.file-reader-import.json`；最终cpp为4eb58dba，header为5c60af1b。package-contract已将私有生产TU登记一次，不导出helper头；v4静态export检查确认61个公共头未变，尚无full-link/installed SDK证明。
- 父会话已在停止边界把读取器和factory/assembler六文件导入QQ/TG，原有文件与接线前像逐一相同后才替换，双方索引未变；最新foundation为QQ v14/73项、TG v12/71项。`59e71b87` 已完成：TG六文件LocalFile接线获 `50e37f09` OK with notes，47/47编译通过；QQ为51/54，三个旧API失败日志与前次逐字节相同。父会话核实TG六份当前前/后像、246输入hash、47对象/状态、71份foundation与原索引；QQ54输入/状态、51对象、73份foundation与原索引亦核实。限定source验收记录为 `telegram-adapter.local-file.accepted.json`，QQ记录为 `onebot-adapter.file-reader-postimport-compile.json`。尚无运行/整包链接证明，未关闭任务3.3。
- `371ac0e9` 已交付439文件/两项缺席的集成清单。父会话在初始TG inventory及2c574368审查、cf7718d5修复与回审中找回四份历史文件的精确出处，记入 `telegram-adapter.integration-lineage.md`；未把早期整体BLOCK伪装为OK，也未扩大到运行验收。
- 父会话按 `core-sdk-integration.source-map.v1.json` 在新 `integration-01/source` 装配439个文件，逐一核对dirty基线前像、来源/目标hash及mode，保留两份QQ删除头的缺席；五个源工作区索引未变。该目录已去写权限，`assembly-receipt.json` 记录证据。`core-only`只是manifest/resolution目录，其两个输入复制到新代目录；未复制guard或live配置。
- `08dfb2df` 已完成：隔离完整configure/build通过，156/156 Ninja步骤，74/74生产TU；core共享库、app、41成员generic静态库均产出。父会话重新核对439源文件hash/mode、四个零状态、74源码覆盖及三份ELF/archive hash，证据见 `integration-01/parent-build-verification.json`。未运行app、未install/freeze、未构建actor；generic archive生成不等于独立消费者链接通过。
- 产物继承shell的 `NIX_LDFLAGS` / `out`，RUNPATH含主工作区 `outputs/out/lib`，app另有空分量；这不是源码或整包链接失败，但不能作为隔离SDK冻结依据。保留本代证据，后续清理构建环境再重建，不修补现有二进制或触碰live core。
- **用户澄清：SDK本身包含core，不要求分开链接或独立平台产品。** 此前把11个公共函数位于core判为拆库阻塞，是错误套用了前序独立链接要求；纯平台支持库、inline迁移和平台插件路线撤销，未实施任何该类提取。公共ReferenceResolver仍是纯虚接口，不是缺少具体函数。设计/spec/任务已改为完整SDK自动传递配套core依赖，保留头文件接口边界及私有实现头不安装。
- package-contract仅调整 `bot_common_sdk` 的INTERFACE链接依赖为包含既有 `obcx_core`，QQ/TG target沿用其公共依赖而自动获得core；不改11个函数、ABI或平台实现。不要求调用者手工补core。`d24e995d` 独立审查无finding，父会话核对精确前后像、patch和两个索引，receipt为 `review-inputs/sdk-includes-core.accepted.json`。
- **干净整包重建通过**：`integration-02` / workflow `b21463b1` / build child `5c5ec09b`，仅新增已审查的CMake依赖边，439只读源码不变。只清理继承的live-workspace rpath、out和.direnv/bin，保留Nix依赖旗标；flock/P20，156/156步骤、74/74生产TU通过。父会话重新核实75项证据、499个build文件、439源码hash/mode以及core的实际ELF RUNPATH；core仅有Nix store路径。app保留CMake安装padding的空RUNPATH分量，未运行/安装，不能宣称运行路径安全验收。
- **SDK2已冻结**：通过本代 `scripts/actor_build.py snapshot-sdk` 安装到 `integration-02/install`，不是live部署。89份installed文件与manifest全量核验、只读；core SHA256为 `7eb7be8a38761ab526308c4a6b8ec0e30121841d2af682d08b36843f48d53bb7`，与本代build字节一致，snapshot manifest SHA256为 `25e8cfb5af8d31f246e460940dccc99c13246216b2d71f5656de14cabfd46609`。安装exports确认common→imported core、QQ/TG→common；ELF确认11个公开函数实际导出，私有codec/binding/reader头未安装。证据在 `integration-02/snapshot-evidence/verified-snapshot.json`；符号/exports检查不是消费者运行验收。
- **Message Store和模板构建通过**：`cbdd2892` 只用冻结SDK/actor_build.py，显式Debug/trace OFF/P20，9个步骤、两个原生产TU完成，core是imported且没有core编译target。父会话核实2573个build文件、全量证据、两个DSO哈希、89份SDK文件及原build core/源码/索引不变；receipt `review-inputs/sdk2-store-template-build.accepted.json`。SDK provider版本2.0.0和actor schema3元数据通过构建期审计，尚未证明loader/factory的实际拒绝行为。两个构建DSO同样含空RUNPATH分量，运行路径验收OPEN；无加载、执行、安装或部署。
- 16.1按隔离构建/既有CMake特性检查与package audit、严格OpenSpec/whitespace检查关闭；没有新增/改写单元测试，没有把构建当成运行验收。该阶段任务进度11/75；后续完整标准范围追加见任务17。16.2仍等其余三个actor与probe完整迁移/构建及schema门禁验收；16.3/16.4运行与数据验证仍开。
- **业务候选已停止**：workflow `5e3fe2e5` 已完成，Chat三文件仅send/member与确认回执寻址，ExHentai四文件仅普通文字/配置引用/resolver注入，均不是完整actor。父会话核实50/138份当前文件、patch和原index；记录 `sdk2-partial-actors.pre-review-verified.json`。两次初始configure因父会话workspace使用绝对source路径失败，原文件已保留并改为相对POSIX路径；重试configure通过，真实P20构建均在未迁移的旧TG头/QQ消息接口处失败，改动函数体未获编译验证。未恢复私有头、删除媒体功能或使用临时编译绕过。
- Chat源增量由 `c8e7b8d5` 审查通过（仅source）；保留旧图片sidecar可读性的SDK迁移边界仍缺，不能把fingerprint当媒体权限、猜原归档/time或仅靠存活Store行造成旧记录失效。成员工具使用共同字段、未知保持未知已确认；prompt/授权不变。ExHentai `ea6a8077` 发现P1：TG负群号不能用含糊chat:解析；父会话改显式group:，`32a3e1f1` 复核关闭。两份source-only receipts为 `chat-llm.sdk2-send-member.source-accepted.json` / `exhentai.sdk2-text.source-accepted.json`；无actor完成勾选。
- Bridge核查 `366c2e21` 已证明typed forward查询及TG富媒体实现存在，早先缺forward查询的猜测撤销。QQ非图片read/send的具体缺口和locator来源信息丢失仍需处理；普通Audio与Voice不能照旧混同。`3adec6eb` 的两份SDK接口/原生媒体方案只是建议，不是实施或兼容行为批准；尤其 `obcx2:media`/8192编码、新provider-self节点及预检接口均未实施，也未授权改写冻结SDK2。
- 父会话先更新ExHentai/probe四份CMake/manifest：main 0.1→0.2，已有probe 0.2→0.3，SDK>=2,<3及input schema3；ABI/manifest schema仍2，原index不变。前像、增量patch、后像在 `review-inputs/exhentai-sdk2-metadata-preimages`，`ea6a8077` 未发现metadata问题。这仅为源迁移/构建提供metadata，不是actor验收，也未发布registry。
- **最新用户优先级：先完成OneBot11标准，所有非标准扩展新增/改造暂停；标准完成后才询问非标准方案。** 不再要求选择LLOneBot/NapCat，不引入provider选择配置。已有扩展保留，不删功能或换成缓存作者；ExHentai原provider-self/精确预检需求先挂起。workflow `953bd520` 已完成官方标准核查：20种不同消息段type、38个公开API、hidden/suffix形式及事件，按native/runtime/typed接口分列；原报告21种的计数错误已按官方顶层type重新核实为20，见 `onebot11-standard-segment-count.json`，未删任何变体。证据固定官方commit `d4456ee7`，仅读取公开规范，未联系bot/探测。用户明确选择“全部移植”，管理/凭据/服务/完整事件等现在均是已批准范围，不再以旧任务范围排除；proposal/design与新增standard spec、任务17已承接。
- 标准forward reader `2f196584` / `2ceb154d` 修正singular message/node.data，旧plural分支行为保留；父会话核实429文件、54编译记录及原index，记录 `onebot-adapter.standard-forward-read.accepted.json`。标准Voice/Video writer `7da32cfe` / review `cc117768` 同样通过限定source审查及父会话429/54核验；`onebot-adapter.standard-voice-video.accepted.json`。新来源为Bytes/最终LocalFile/有界core HttpSource，MediaRef尚未支持、格式约束保持unknown、普通Audio/File仍明确不支持，未扩大非标准forward发送。两池各51/54、全部26生产输入通过，3个旧fixture错误原样保留；不是新SDK/full-link/runtime验收。
- 用户要求5–6路并发，workflow `09dc245d` 已完成6路：1实现+5完整模块契约方案。五方案仍需父会话逐项读取/收敛并分配隔离writer，不能据完成通知批准源码。父会话已明确标准provider-delegated意图与common HttpSource分离、同一cache/coordinator下的具名显式标准login查询、独立installation-scoped query selectors与detached snapshots；决定记录 `review-inputs/qq-standard-parent-decisions.md`，未静默授权新codec上限、路径registry、默认值或非标准接口。

## Handoff rules

每条 lane 保存相对本阶段捕获的 dirty 前像的精确增量 diff/hash、新文件列表、执行命令/结果、未完成项及接口说明；不把 HEAD 或过时 snapshot-index diff 当作当前增量。不得把基线已有改动作为本 lane 新工作再覆盖原仓库。集成前比较原路径与 snapshot 前像；发现并发修改则三方整合或停止该文件，不覆盖。

父会话保留验收、任务勾选和 scope 决策。子 agent 不启动其他子 agent，不提交，不修改主仓库规划状态，不触碰 local_config 或运行中的库。异步完成通知是下一轮继续工作的触发点。
