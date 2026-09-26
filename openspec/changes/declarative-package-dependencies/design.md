## Context

本变更解决的是“能否根据 TOML 检查完整依赖”，不是单纯让 linker 找到一个 `.so`。用户已决定：**不兼容旧格式，全部维护中的 actor 一次迁移；显式来源＋锁文件，不做 registry 自动选版本。** 规划已获批准，现开始跨仓实施；不修改运行配置或执行部署。

目前的事实：

| 环节 | 现状与缺口 |
| --- | --- |
| `actors.toml` | `parse_actor_packages.py` 只接收 actor path/repository/revision；loader 逐项 add_subdirectory，不解析普通库图 |
| `actor.toml` | packages 是第三方名称列表，actors 是 ID/range 列表；校验 range 语法不等于验证实际依赖版本 |
| 第三方准备 | `gen_vcpkg_manifest.py` 收集第三方名称；远程 actor 元数据依赖先 configure 获取源码，顺序存在自举问题 |
| CMake | `obcx_add_actor(... DEPS ...)` 接受任意已存在 target，不核对 TOML；内部 bridge_core/exhentai_fetch_core 也可能传递隐藏依赖 |
| Runtime | 已能扫描并 staging 包内私有 ELF 闭包、版本化 SONAME/DT_NEEDED；这不是源码依赖解析器，不能识别静态/header-only 源依赖 |
| 发布 | `package_actor_release.py` 写死两个 actor，archive 主要收 actor DSO 和元数据，尚不保证完整私有库闭包 |
| Registry | actor-only schema/index，存在根目录和独立仓库中的 validator 镜像，必须避免新版出现三份不同规则 |
| Mapper | bridge 内部的 `PathManager` 可复用，但字符串前缀、越界后原样返回、词法规范化不等于安全文件访问 |

核心原则：**声明图、实际构建图、发布文件闭包是三种证据，不互相冒充。**

## Current implementation slice — user supersession

用户最新明确：整个项目仍处于开发阶段，先不考虑独立 SDK 二进制/头文件发布及外部消费场景。**当前先在 OBCX core 工作区完成 TOML → resolver/lock → CMake 实际图校验 → mapper → 两个 actor 消费者。** 以下独立 SDK、正式发布闭包、registry 协调发行等设计保留为后续方向，不再作为当前实现的前置条件；不宣称后置内容已完成。

本地包显式使用 development/working-tree 语义，修改实现代码不要求重新提交 Git 或为本仓库固定一个“包含锁文件自身”的 commit。根 packages.toml/packages.lock 管理子目录中的 actor/library；开发生成物位于包源码目录之外，不要求另建工作区或仓库。后续确实实施独立构建/发布时，再解决相应的同仓锁与来源身份问题，不能拿它阻塞当前 core 改造。

用户随后明确：第三方环境的固定由现有 flake.nix/flake.lock 负责。已撤回增加 flake 输出/额外依赖清单的尝试，不把另造一层 Nix 清单或自动生成目录/摘要作为当前工作的前提；本变更重点仍是自有包声明、版本和真实链接关系。

本地元数据/schema/validator 的一致性迁移、保留 ID 检查、第三方真实版本/targets 核验、显式配置和构建回归仍是当前要求；发布后置不等于允许手工 CMake 隐藏依赖或引入旧格式兼容层。已有 SDK 与 runtime staging 能力不删除、不退化。

## Goals / Non-Goals

**Goals:**

1. actor 和普通库均具有独立身份、版本、显式来源和规范元数据。
2. 配置执行包 CMake 前发现缺失/冲突/环/错误 kind，诊断包含完整依赖链。
3. 缺锁、过期锁或未声明依赖不能靠机器“恰好装了库”、环境路径或 manifest 顺序蒙混通过。
4. static/shared/header-only 全覆盖；每个模式明确配置、安装和发布验证边界。
5. 一套解析结果服务 monorepo、独立 SDK 构建、第三方清单、registry 和发布。
6. 保留 actor ABI V2 及私有 DSO 热更新安全性，不把库当 actor 或进程服务。
7. 以 mapper 抽取和两个真实消费方完成闭环验收。

**Non-Goals:**

- 不做 registry 搜索/自动版本选择、SAT 求解、多版本同 ID 共存或新的通用包管理器。
- 不提供 v1 reader、隐式格式升级、旧文件名 alias 或长期双 schema。
- 不声称 TOML 能约束恶意 CMake、发现任意脚本嵌入的对象或证明 C++ ABI 兼容；构建源码仍是受信代码。
- 不让 resolver 在运行时联网，不自动重启服务，不让普通库承载跨 actor 全局可变配置。
- 不在这次包系统里完成 ExHentai 画廊业务/文件清理状态机，不修改 LLOneBot。

## Decisions

### 1. 三份文件，三种职责

- **`package.toml`**：包作者声明我是谁、导出什么、依赖哪些 ID/版本/目标。schema v2，actor/library 分支有不同必填项。
- **`packages.toml`**：workspace 选择 roots，并为所有可能进入选定闭包的包 ID 显式绑定来源；同时选择系统依赖和 SDK 的 provider binding、目标平台和构建 profile。不靠目录名判断包种类。
- **`packages.lock`**：工具生成的确定性来源/版本/边/元数据锁；frozen 检查不会自动更新它。

另生成两类证据，不能和源锁混为一谈：

- configure 的 `resolved-packages.json`：选定平台/profile 的精确节点、依赖边、源码目录和合法 CMake targets。
- build/install 的 `package-build-receipt.json`：实际 compiler/ABI 信息、第三方版本与 provenance、link/usage 闭包及产物摘要。二进制哈希属于此回执，不靠修改源码锁来假装二进制可重现。

静态库和 header-only 没有 DT_NEEDED，但仍必须出现于源锁、解析图和消费方 build receipt。

### 2. 新元数据模型

所有配置项均显式提供；不需要的依赖列表写 `[]`，unknown fields 拒绝。示例均是**结构片段**，不是省略字段也能运行的配置。

```toml
schema_version = 2

[package]
id = "obcx.path-mapping"
name = "path_mapping"
version = "0.1.0"
kind = "library"

[artifact]
kind = "static-library"             # shared-library / header-only 也支持
name = "obcx_path_mapping"
target = "obcx_path_mapping"        # 实际 CMake target
export_target = "obcx::path_mapping"
platforms = ["linux-x86_64", "linux-arm64"]

[dependencies]
libraries = []
actors = []
system = []

[test_dependencies]
libraries = []
system = []                        # 真正使用 gtest 时必须填入声明
```

actor 同样使用 `[package]`，`kind = "actor"`，另有必填 `[actor]` 的 `name/abi/entrypoint/input_contract_schema`；其 artifact 必须是 shared-library。普通库不得填 actor entrypoint/ABI 字段；library 的 `actors` 必须为空。

```toml
[dependencies]
libraries = [
  { id = "obcx.path-mapping", version = ">=0.1.0,<0.2.0", target = "obcx::path_mapping", visibility = "private" }
]
actors = [
  { id = "onebot-cxx.message-store", version = ">=0.2.0,<0.3.0" }
]
system = [
  { id = "obcx-sdk", version = ">=1.1.0,<2.0.0", targets = ["obcx::obcx_core"], visibility = "private" }
]
```

分类与规则：

- `libraries`：构建/链接/usage requirements 边，version range、目标和 `private/public/interface` 必填。header-only 可以依赖其他 header-only/interface，不能有不可表达的 private binary implementation。
- `actors`：部署/逻辑依赖，不链接另一个 actor DSO。只能 actor → actor；校验版本与闭包，但**不会自动生成运行时 actor 实例或覆盖 `[actors.*].requires`**。
- `system`：外部 provider 提供的第三方或 SDK 依赖，ID、版本约束、授权 imported targets、visibility 必填。actor 必须显式声明 `obcx-sdk`，helper 不再偷偷注入未声明依赖。
- `test_dependencies`：tests profile 才进入构建闭包，不能混入生产 artifact 依赖；每个包均显式提供空或非空列表。
- publication 与 compatibility/toolchain 要求同样有明确 schema。actor 保留 OBCX/ABI V2 兼容性校验；纯 library 不因“没有 actor ABI”就免除平台/compiler/stdlib/PIC 检查。
- 一个 package v2 导出一个主 artifact target；内部实现/test/object targets 可有多个，但需要登记所有权。第一版不做多 artifact variant 选择，若需 static/shared 两种发行物应使用明确的不同包 ID。

不把新版的普通库塞进旧 `dependencies.packages = ["foo"]`：该列表没有 source/version/target/visibility 信息，无法满足本需求。

### 3. Workspace roots 与显式 sources

示意：

```toml
schema_version = 2

[workspace]
roots = ["vollate.bridge", "vollate.exhentai-fetch"]
platform = "linux-x86_64"
profile = "tests"                   # production / tests

[[sources]]
id = "vollate.bridge"
kind = "path"
path = "local_actor/obcx-message-bridge"

[[sources]]
id = "vollate.exhentai-fetch"
kind = "path"
path = "local_actor/obcx-exhentai-fetch"

[[sources]]
id = "onebot-cxx.message-store"
kind = "path"
path = "local_actor/obcx-message-store"

[[sources]]
id = "obcx.path-mapping"
kind = "path"
path = "local_library/obcx-path-mapping"
```

远程绑定换成 `kind = "git"`，必填 `repository`、完整 `commit`、明确 `subdir`（根目录也写 `"."`）。不接受 HEAD、浮动 branch/tag。可另提供显式 pin 工具把人为选择的 ref 展示为 commit，但 lock/frozen 绝不自行跟随 ref。

path 相对 workspace 文件，不相对当前 shell/CMake 工作目录。Git subdir 必须在 checkout 内，禁止绝对路径与 `..` 逃逸；同一个 repo/commit 的多个 subdir 可以共用只读 fetch 缓存，但包 ID/元数据各自核验。

sources 是候选绑定，不等于全部构建：roots 的传递闭包决定所选包。允许未使用的绑定并列入说明，不下载、不执行它们；闭包中缺 source 是错误，不回退到 PATH/CMAKE_PREFIX_PATH 或 registry。同一 ID 两个 bindings 不允许。

### 4. 解析和锁文件：只验证候选，不替用户选版本

流程：

```text
读取新版 workspace
 → 加载 root 元数据（只读取数据，不运行包 CMake）
 → 按显式 source 递归取得依赖元数据
 → 检查 kind / identity / version range / platform / profile
 → 对所有入边约束求交并核对选定版本
 → 检查 actor 图、library 图与总构建顺序
 → 生成/检查 source lock
 → 输出拓扑排序与解释链
```

一个 ID 一个选定版本；菱形图共用同一个 node。冲突时显示 A→lib 的约束和 B→lib 的约束以及实际锁定版本，不下载其他版本。版本比较必须是真实 SemVer，不以现有 regex 校验替代：第一版明确支持 `=/>=/<=/>/<` 的逗号交集；不支持的 `^/~/*/||` 直接报错，维护中的元数据一次改成支持语法。pre-release 仅在约束显式包含 pre-release 时参与，build metadata 不影响排序，但不同 source 身份仍不可混用。

源锁记录：schema/tool 版本、规范化 workspace 内容摘要、所选平台/profile、每个 ID/kind/version、source binding、Git commit/subdir/tree 身份、package.toml 摘要、依赖边、provider binding/外部 lock 摘要。排序稳定、无时间戳与用户机器绝对路径；下载缓存路径不进入可提交锁。

本地开发与发布须显式选择模式：

- **development**：path 包锁定元数据/依赖，不冻结每次代码编辑；build receipt 记录当次源内容摘要和 dirty 状态。修改依赖、版本或 workspace binding 必须重锁。
- **release**：path 包也必须有可重建来源。Git 包要求明确 commit/subdir、工作树无修改且无被构建的未跟踪源码；非 Git path 包要求完整源归档及摘要。发布回执记录源归档/commit，不能仅记录本机目录。
- **frozen**：只消费已有锁，缺失或 drift 就失败；不得自动改锁。online/offline 和 development/release 均显式输入，无隐式模式。
- offline 使用本地完整源码与已校验缓存；缺项说明哪个 ID/commit/digest 缺失，不偷偷联网。

远程源码是受信输入但需要 HTTPS、固定 commit 和内容验证；缓存临时目录下载、校验后原子发布；checkout/cached 文件不由消费方修改。Git submodules/LFS 第一版若不具备独立固定来源记录则明确拒绝，不执行隐式下载。锁写入必须原子且同一 build dir 的并发解析互斥。

### 5. 构建之前就准备第三方，不通过 configure 触发下载

新增统一工具入口（下列是拟新增命令，不是当前可执行命令）：

```text
package-tool lock/resolve   解析 package 元数据及 source lock
package-tool prepare       获取明确锁定的来源，导出所需 system deps
package-tool check         frozen 身份/图/来源检查
CMake configure            只消费 resolved graph，不触网/改锁
CMake generate + audit     核对实际 target 图
build / install
package-tool verify        核对 receipts、ELF 与安装文件闭包
package-tool explain ID    显示为何需要它、谁要求哪个版本
```

provider bindings 由 workspace 显式提供：逻辑 system ID 映射到 CMake package、find 模式（config/module/pkg-config）、合法 targets 和 provider provenance。vcpkg 的 package 名/port 与 pinned baseline，Nix 的 flake input/lock，或已安装 environment prefix/snapshot 都不是由名字猜出来。

工具不替代 Nix/vcpkg：从同一图导出第三方需求，交相应 provider 准备环境，然后 CMake 查找**已声明**的 package 并核对实际版本/targets。不能报告版本的依赖必须有显式可核验的 provider receipt；不因 find_package 成功就假定版本符合。跨 provider 的版本字符串需各 adapter 明确比较规则，不把所有系统包版本硬套 C++ 包的 SemVer。

SDK 使用保留 system ID `obcx-sdk`：workspace 构建可显式绑定到本树 SDK target/版本；独立构建绑定到已安装 SDK manifest。它不是可被任意 actor 包冒充的 source，也不属于 actor 私有库。SDK 自身依赖及编译器隐式运行库来自受审核的 provider/toolchain receipt，不要求每个 actor 重复声明全部 stdlib/libc。

根 Nix shell 现有手写依赖清单与 vcpkg-base 的 core 部分需明确划为 SDK/provider 基线；不能同时出现另一份“actor 依赖真实列表”。离线 core/SDK-only 构建也使用显式空 roots，不再由“没有 actors.toml”隐式选择模式。

### 6. CMake 必须消费声明，并检查实际依赖

拟新增 `obcx_add_library` 与 package graph 导入 helper；保留 actor ABI 导出 helper，但改为读取新版元数据。

- Resolver 为每个包建立 source root、owner ID、主 target 与 export alias。依赖包先配置，所有 source 路径来自 locked graph，不按 TOML 排列顺序碰运气。
- helper 根据 libraries/system 的 visibility 建立主 target 使用关系；CMake 不另维护一份外部 DEPS 真相。内部 static/object targets 可通过显式 package helper 登记并绑定该包声明的依赖。
- 包内自建 targets（如 bridge_core）登记为同一 owner，不要求假装成独立发布包；但递归遍历它们的 link/usage 边，防止通过内部 target 隐藏外部依赖。
- package 导出的 namespace target 全局唯一，不允许别的包/系统导入覆盖。库 kind 与实际 target 类型必须匹配；STATIC 进入 actor DSO 必须 PIC，INTERFACE 不能声称有 `.so`。
- 配置/生成结束做 deferred audit，并使用 CMake File API 和生成后的 link/usage 信息交叉核验。追踪 alias、INTERFACE、OBJECT、静态库私有 `$<LINK_ONLY:...>`、配置限定 generator expressions；不能只检查 `obcx_add_actor` 的一行参数。
- 对已选 platform/config 的未知跨包边、无 owner imported target、包外绝对 archive/include/link 目录、裸 `-lxxx` 或未解析表达式严格拒绝并指出 target/来源；编译器隐式库及声明 system provider 内部闭包仅按已登记 provenance 放行，不做任意系统库白名单兜底。
- 直接使用另一个包的 target 必须有直接声明；依赖导出的合法 public/interface 传递闭包可以传播。公开头文件或 PUBLIC usage 引用了 private dependency 时，安装后的无源码消费测试必须失败并提示修正 visibility。
- 开启 tests profile 时 test targets 的闭包可包含 test_dependencies，但禁止其反向进入生产 artifact。production profile 不获取纯 test 包。
- 常规构建同样执行审计；发布 CI 额外检查未声明 FetchContent/ExternalProject/外部下载（受控构建环境禁止 configure 联网）。不是仅在 lint 模式“建议”检查。

边界：File API/target 审计能发现受支持 CMake 构建中的依赖漂移，不是恶意脚本沙箱。包可以故意在 custom command 嵌入任意机器码，因此来源信任、发布审计和隔离构建仍必要。对无法核验的受支持构建形式应报“不支持”，而不是声称已经证明完备。

### 7. 源码库、链接库、运行时库的区别

| 库类型 | 声明/源码锁 | 构建检查 | 发布/部署 |
| --- | --- | --- | --- |
| header-only | 必须 | version、include/usage、传递依赖、owner | 开发包包含 headers/config；actor 部署无独立 DSO，但 receipt 保留依赖 |
| static | 必须 | archive、PIC、ABI/flags、传递链接 | actor 运行不额外部署 archive；开发包需 headers/archive/config/依赖描述 |
| shared | 必须 | target/ABI/SONAME、实际 link | actor 部署包含所有 actor-private DSO 闭包；系统/SDK 库按 process-owned 规则处理 |

“公共源码”不等于“进程里只有一个库实例”。第一版 shared 普通库采用 actor-private 闭包部署；不能让 bridge/exhentai 跨 actor 交换它分配的对象、函数指针或共享可变单例。SDK/process-owned 依赖保持现有身份规则，更新需要进程重启时必须明确提示，不能用多版本 staging 规避 ABI 边界。

纯 mapper 无可变全局状态、不依赖 OBCX logger，选择 **static-library + PIC**，避免不必要的 DSO/ABI 运行时耦合；仍完整走 TOML 的来源/版本/实际构建检查。

### 8. 安装、发布与热更新是闭环的一部分

源锁不等于可部署包。安装过程生成受检查的 artifact inventory，包含每个包的 package.toml、主 artifact、开发接口、普通依赖 ID、文件相对路径和摘要，以及 compiler/stdlib/C++ ABI fingerprint。

图一致性按边的语义检查，不要求三张图字节级相同：static/header-only 没有运行时 DT_NEEDED，shared 经 `--as-needed` 也可能没有实际运行时边。它们仍在声明/构建 receipt 中；发布动态闭包以实际 ELF 边为依据，要求每条边有已声明来源并完整可用，而不是伪造不存在的运行时依赖。

部署布局应保持 runtime actor 名称查找不变，例如 `lib/obcx/actors/bridge.so`，其私有库放在该 package root 下独立相对目录（例如 `.deps/bridge/<package-id>/`），通过 `$ORIGIN` 相对查找。每个 actor archive 自包含其私有闭包，跨 archive 文件所有权和路径碰撞必须检测；不能为了减少复制而让它引用另一个 actor 安装包的可变目录。

- 发布脚本读取解析图和 install inventory，不再写死 actor 名单，也不接受单独找到了一个 `.so` 就认为完整。
- 对 shared actor-private 边递归扫描 ELF，解析到所声明并安装的闭包；缺失、越界、未声明私有 DSO、SONAME 冲突均拒绝。
- RPATH/RUNPATH 必须相对安装布局（例如 `$ORIGIN`），禁止发布产物指回 build/source tree。符号链接目标必须在合法包/依赖闭包内。
- actor install archive 携带全部私有 DSO，形成完整 immutable set；有私有闭包时不把裸 actor DSO 发布为“可直接部署”。静态/header-only 的源码依赖记录仍包含在 receipt/SBOM 类 inventory 中。
- library 开发包可独立发布，包括 CMake export/config、头文件、必要的 archive/DSO、依赖 metadata 与 receipt；其被依赖的 SDK/system 条件显式保留。
- 发布前从空前缀、无源码树引用、清理 loader 环境的条件安装/加载/调用；每个宣称支持的平台必须真正构建验证。
- 继续利用 `ActorPackageStager` 的私有依赖 content-versioned identity、DT_NEEDED 改写和代际保活。新旧两代 invocation 必须分别调用各自依赖版本，退役不早卸载。
- 回滚以完整 artifact set + 对应 actor config 为单位；不单独覆盖一份公共 `.so`。库不拥有 actor_factory，不自动进入 runtime 拓扑。
- 保持库类型的跨 actor 符号安全：私有实现隐藏非公开符号，不导出可变全局对象；不依赖加载顺序或相同 SONAME 的全局符号抢占。验收包括两个 actor 消费不同内容的同名私有库，确认代际绑定，而不只测试文件复制成功。

### 9. Registry 不求解版本，但不能继续只认识 actor

索引改为 package-kind 判别的 `index/packages.json`，条目用同一 `package.toml`；actor/library 的 artifact 类型、入口与可发布平台按分支校验。第一版 registry 只发布、浏览和校验已固定的包记录，不参与 workspace 自动版本选择。

用户已在阶段 0 明确：独立 `obcx-actor-registry` 为发布源，core `actor-registry/` 仅保留 pinned conformance fixture/快照。新版 validator/schema 由 SDK tooling 的同版本发行物提供，registry pin 版本与摘要，不维护手抄 Python 副本。

用户已拒绝旧格式兼容，因此同一切换中迁移 source entries、schema、generator、workflow、README 与生成索引，不持续输出旧 actors-only index。物理 Git 仓库远程名称无需作为协议前置条件改名；目录/入口的新命名和所有引用应一次对齐。

父仓库 `actor-package-ecosystem` 主规格原有“registry only actor”要求必须明确替换；这不是一次仅改 CMake 的实现细节。

### 10. 公共 mapper 的边界

按用户最新确认，库源码归属独立 Git 仓库 `local_library/obcx-path-mapping/`；core 忽略 `local_library/`，不跟踪这份源码、不使用 gitlink/submodule，也不保留旧目录副本。包 ID `obcx.path-mapping`，导出 `obcx::path_mapping`，测试不依赖机器人连接。工作区只显式绑定来源；开发阶段无需提交、远程地址或发布 pin。bridge 和 ExHentai 在 package.toml 直接声明它，不跨 actor 目录 include 头文件或链接对方 core archive。

库接受显式映射：逻辑目标/installation key、host_root、peer_root（不硬编码容器概念）。两个根目录必须是显式绝对路径，非空；目标不能从最近一次调用或进程全局配置推断。

核心契约：

- 输入是受控 host root 内的路径，输出该目标可读的 peer path 或正确百分号编码的 file URI。
- 按路径分量判断边界，`/data/media-other` 不属于 `/data/media`；词法规范化后 `..` 越界拒绝。
- unknown mapping、越界和非法 URI 字符/格式明确失败，**禁止沿用旧 mapper 的“警告后原样返回”**。
- 容器侧路径仅词法处理，不对容器路径调用宿主机 canonical/stat。
- 安全写入和读取 host 文件要另做真实文件系统检查，拒绝 symlink 逃逸；明确提供已有文件解析校验与尚未创建文件的安全目录流程。只做 lexical mapping 不能自称防 TOCTOU。
- URI 用编码器生成 `file:///...`，测试空格、`#`、`%`、Unicode；不简单拼 `"file:///" + absolute_path`。
- 映射不创建挂载、不传输文件、不修改权限、不自动发消息。库错误不暴露凭据或无关路径。

下载鉴权/URL 信任/图片魔数仍由业务负责。临时文件写入、原子发布、单文件与总缓存限制、正在发送的租约以及 uncertain 后的保留/清理由各业务或另立的媒体存储组件负责；不可偷偷塞进无状态 mapper 并产生全局单例。

### 11. 与画廊合集的衔接

该变更达到“mapper 可由 TOML 被两个 actor 正确消费”的阶段后，修订 `qq-gallery-forward-batches` 的媒体准备设计：下载校验后写共享文件，节点的 `image.file` 使用 peer file URI，仍是一份图片/文字交替的合并转发，现有 OneBot11 action 不变。

必须另明确：

1. 实际目录挂载和权限，以及每个 installation 的映射配置，无默认根路径。
2. `PreparedForward` 保有文件 lease；在明确已读取/完成前不删除，超时/取消/uncertain 后保守保留并有显式回收策略。
3. 清理只删除本业务持有的受控文件，不跨 actor 清理共享根、跟随 symlink 或在同一路径原地覆盖在途文件。
4. 单图/整次准备/总磁盘的显式容量约束；路径引用移除了 base64 膨胀，但并未取消文件、节点和文字 JSON 的预算。
5. 当前 DTO 只接受 base64，需要显式支持受控 file URI，而不是接受任意调用者提供的绝对路径。

因此暂不为旧 base64 路线选择 8/16/32 MiB 业务预算，不自动加入该配置值，也不在本次规划里声称新文件方案已经实现。

## Risks / Trade-offs

- [一次性 breaking 涉及多个仓库] → 先清点，协调 core/SDK/registry/全部 actor 版本；单一 cutover，旧部署用旧完整发布集回滚，不混读旧格式。
- [依赖模型膨胀成通用包管理器] → 第一版 source 由人绑定、单版本、无 registry solver、不管理系统包安装器。
- [只有显式来源也可能隐藏版本冲突] → 检查所有传递入边，错误展示完整链，不只检查 root。
- [CMake 太自由难以完备检查] → 受支持 target 形式 fail-closed，源信任＋隔离 CI；不作恶意构建沙箱承诺。
- [静态库被误认为不需要 dependency metadata] → 仍入源锁、实际 usage 审计、build receipt 和开发包。
- [本地修改影响可重现性] → development 允许代码编辑但记录 dirty；release 要求固定源/归档，frozen 图漂移拒绝。
- [系统依赖各自版本规则不统一] → provider adapter 明确版本/provenance，不自动猜版本，不重新实现 Nix/vcpkg。
- [共享库被错误放到包根之外而变成 process-owned] → inventory/ELF 审计要求私有闭包真实位于部署 package root；重用已有 runtime 拒绝规则。
- [mapper 正确但文件提前清理或根本不共享] → 在画廊后续任务验证真实挂载与 lease，明确路径映射本身不解决生命周期。

## Migration Plan

阶段按依赖顺序实施，**不是同时线上支持两套格式**；可以在分支中逐步完成。本地源码格式一次切换，远程协调发行后置。按用户最新决定，下面阶段 3–4 先做工作区构建及必要的本地消费者迁移，然后优先阶段 6–7；独立 SDK 扩展验收与阶段 5 的新发布闭环稍后再做。已有 SDK/运行时安全测试仍保留。

0. 清点所有元数据/脚本/fixtures/索引消费者，确认跨仓库编辑和主规格同步边界；明确 registry 源与快照关系。
1. 定义 v2 schemas、源/锁/receipt 格式及唯一 parser；编写正反例，确定平台和 profile 语义。
2. 实现 resolver/frozen/offline/diagnostics 与 source preparation，无 CMake 副作用；版本/图测试先过。
3. 接入 SDK/system provider adapters、根构建与 installed SDK；实现 CMake target ownership 和实际依赖审计。
4. 一次迁移全部维护 actor、模板、测试、根清单和构建命令，删除 v1 入口；空 roots SDK-only 和 clean external actor 两条路径都通过。
5. 实现 inventory、完整闭包打包、统一 registry 和空前缀发布验收；验证私有 DSO 两代共存与回滚。
6. 抽取 mapper 并修复路径边界，bridge/exhentai 显式消费；静态实际消费者与 shared/header-only fixtures 都完成验收。
7. 回到画廊变更，先补共享文件准备/租约/容量规格，再实施 file URI 发送，不跳过真实 QQ 验收。

每阶段 done criteria 对应 tasks/spec 场景。编译与测试至少 6 worker，低负载使用全部 CPU；本机目前 20。提交前根目录 `nix fmt`；如未签名提交，提醒补 GPG 签名。实施不自动创建提交。

## Open Questions

不再开放的决定：旧格式不兼容；普通库是一等声明；显式 sources＋lock；无 registry 自动选版本；mapper 采用静态 PIC、无状态、无 actor SDK 依赖。

实施前仍须确认的外部边界：

- 已确认本包系统变更迁到 OBCX 根仓库并授权跨仓实施；core 主规格仍在根仓库，画廊变更仍在子仓库。
- 已确认独立 registry 为发布源、根目录为固定快照；实际远程发布和新 release commit pin 等待之后明确批准。
- 本地维护仓库和消费者清单已记录于 implementation.md；未见源码的外部消费者不宣称已迁移。
- 画廊文件方案的目录、权限、保留时长和容量已在目标 `muf7ej4d-srqpof` 中获用户明确确认，记录于 ExHentai 的 `qq-gallery-forward-batches/shared-files-approved.md`。路径必须由各 actor 从配置读取后传入 mapper，不写死、不增加默认值；共享文件实现和真实 QQ 验收仍未完成。
