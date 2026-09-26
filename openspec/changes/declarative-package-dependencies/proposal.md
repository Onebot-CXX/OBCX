## Why

现有 TOML 只能列出 actor 与第三方包名，不能对自有普通库完成来源、版本、传递依赖和实际链接检查；把 mapper 改为静态库只能绕过运行时 `.so`，并没有解决声明式依赖缺口。需要建立从 TOML、锁文件、CMake 到发布文件闭包可核验的一条链路，再以共享路径库验证它。

## 当前实施阶段

用户最新要求：项目仍在开发阶段，先不考虑独立 SDK 发布和外部消费。当前交付是 **core 工作区内的包声明、依赖解析/锁、实际 CMake 图校验及 mapper 的两个真实消费者**。以下正式发布、独立 SDK 扩展和 registry 协调发行保留为后续方向，不阻塞当前工作；不删除已有 SDK 能力，也不放松本地格式迁移、依赖校验或显式配置要求。后置任务保持未完成。

## What Changes

- **BREAKING**：构建选择从 `actors.toml` 切换为 `packages.toml`；包元数据统一为 schema v2 的 `package.toml`，支持 actor 和 library。一次性迁移所有受维护 actor、模板、fixtures、SDK、registry，不保留旧格式 reader、隐式字段补值或双入口。
- 明确区分 runtime actor 依赖、普通库链接依赖、第三方系统依赖、SDK 和工具链；static/shared/header-only 都必须可声明、解析和检查，普通库不冒充 actor。
- 来源由 workspace 显式绑定本地目录或远程 Git commit，增加 `packages.lock`；解析完整传递图、检查版本交集/缺失/环/身份冲突。一个工作区每个包 ID 一个版本，不从 registry 自动选版本、下载替代版本或跟随分支。
- 将配置前解析、来源准备、第三方依赖准备、CMake 配置、链接核验、发布核验拆成明确阶段，解除当前“先 configure 获取远程 actor，再生成第三方依赖清单”的顺序问题。
- CMake 从解析结果建立依赖和公开目标，配置/生成后核对真实 target graph，检查隐藏链接和私自抓取依赖；static/header-only 同样有来源、版本、传递闭包和构建记录。
- shared library 随 actor 发布完整私有闭包，继续复用 `ActorPackageStager` 的版本化 SONAME/DT_NEEDED 机制；不把任意全局共享 `.so` 当成可独立热替换资源。
- 发布与 registry 支持带 kind 的包记录，发布脚本不再写死 bridge/message_store，不把有私有依赖的单个 actor `.so` 宣称为完整部署包；只描述已构建并验证的平台。
- 以独立静态 PIC 的路径映射库作为首个消费者，bridge 和 ExHentai 在 TOML 显式依赖。库只负责受控映射和 URI 生成，不负责下载、文件删除或跨 actor 全局状态。
- ExHentai 合集后续改用共享目录文件引用；真实挂载、写入发布、发送中保留与不确定结果清理须另在画廊变更明确，不能把字符串映射视作文件传输。

## Capabilities

### New Capabilities

- `declarative-package-resolution`: 显式来源、完整依赖图、严格版本约束、锁文件、离线/frozen 和可追溯解析结果。
- `declared-build-dependencies`: 声明与真实 CMake 依赖一致性、第三方/SDK 绑定、独立 SDK 构建和构建可追溯性。
- `package-release-closures`: 安装/发布闭包、包索引、ABI 边界、动态依赖热更新与回滚。
- `shared-media-path-mapping`: 无状态公共路径库、目录安全边界、目标 installation 映射与 file URI。

### Modified Capabilities

- `actor-package-ecosystem`: 将父仓库现有 actor-only 包元数据/registry 契约替换为 actor 与 library 的统一声明模型，保持 ABI V2 actor 运行时边界。

此项 delta 的主规格归属 OBCX 根仓库 `openspec/specs/actor-package-ecosystem/spec.md`。实施前用户已明确批准：本包系统变更迁到根仓库，并授权 core/SDK/registry/维护 actor/library 跨仓实施；画廊变更继续留在 ExHentai 子仓库。不得将 core delta 同步成 ExHentai 私有主规格或自动归档。独立 registry 为发布源，根目录仅保留固定 conformance 快照。

## Impact

- Core：`schemas/actor-package.schema.json`、`cmake/actor_metadata.py`、`parse_actor_packages.py`、`gen_vcpkg_manifest.py`、`OBCXActor*.cmake`、SDK 安装/export、根 CMake 和构建入口。
- 发布：`scripts/package_actor_release.py`、`verify_actor_release.py`、rollback rehearsal、registry schema/generator/CI，以及当前根 `actor-registry/` 和独立 registry 仓库中的镜像代码。
- 所有维护中的包：bridge、message-store、ExHentai、probe、actor template、已选择的 chat_llm/fixtures 等，必须先清点再一次切换，不能只改当前两个 actor。
- 新库：按用户确认在 `local_library/obcx-path-mapping/` 建立独立 Git 仓库，core 不跟踪源码、不添加 submodule；具有自己的 `package.toml`、CMake targets、测试和安装契约。workspace 显式绑定来源，不依赖 actor SDK 或全局 logger。
- 画廊变更：依赖本包系统的可用阶段，另补共享文件生命周期和 file URI DTO；本规划不执行上线、QQ 测试、服务重启或修改连接凭据。
- 不引入配置默认值，不实现 registry 自动版本求解，不更换包管理器，不声称能通过元数据约束恶意 CMake 或自动证明 C++ ABI 兼容。
