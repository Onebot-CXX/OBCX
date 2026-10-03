> 阶段说明：当前先验收 core 工作区构建与实际依赖审计。独立 installed-SDK 的新增消费闭环和发布回执验收按用户决定后置；保留已有 SDK 能力及回归测试，不把后置场景当作当前阻塞。

## ADDED Requirements

### Requirement: Build preparation consumes one resolved dependency graph

源码准备、第三方需求导出、CMake、独立 SDK 构建和发布 SHALL 消费同一 resolved graph/lock，不各自维护不同依赖列表。CMake configure MUST NOT 获取未准备源码、自动重锁或解析 registry。未选任何 actor 的 SDK-only 构建 SHALL 使用显式空 roots。

#### Scenario: A remote actor has third-party requirements
- **WHEN** 获取远程包的已锁定元数据后发现 system dependencies
- **THEN** 在执行包 CMake 之前导出需求供 provider 准备，不要求先 configure 才知道要安装什么

### Requirement: System dependencies and SDK bindings are declared and verified

第三方与 SDK 依赖 SHALL 有逻辑 ID、版本约束、授权 targets、visibility 和显式 provider binding。构建 SHALL 核验实际 package 版本/targets 及 provider provenance；不能报告版本时 SHALL 要求可核验 provider receipt。系统 MUST NOT 因机器上存在任意同名库就视为满足依赖，也不得猜测逻辑 ID 与 vcpkg/Nix/CMake package 名的关系。

#### Scenario: A package is installed but undeclared
- **WHEN** actor 链接系统已安装 target T，而 TOML 和其合法传递闭包均未声明它
- **THEN** 实际依赖审计失败，不能因为 find_package 成功而放行

#### Scenario: A provider exposes the wrong version or target
- **WHEN** provider 只提供不满足约束的版本或未授权 target
- **THEN** configure 失败并显示期望/实际，不以成功查找到文件替代版本检查

#### Scenario: An actor omits the SDK dependency
- **WHEN** actor 元数据没有显式声明 obcx-sdk
- **THEN** 拒绝构建，不由 actor helper 隐式补 SDK 依赖

### Requirement: Package-owned targets and exports have verified ownership

每个生产、内部和测试 CMake target SHALL 有可追踪 package owner。每包主 artifact target/type/export alias SHALL 与元数据一致；全局 export alias MUST 唯一。Library 的 static/shared/header-only 形态 SHALL 分别验证；static archive 进入 actor DSO SHALL 满足 PIC 和兼容工具链约束。

#### Scenario: A package exports a different target kind
- **WHEN** 元数据声明 static-library 但创建 shared target，或声明的 export target 不存在
- **THEN** 拒绝该包，不靠 artifact 文件后缀推断正确性

#### Scenario: An internal target hides another package dependency
- **WHEN** actor→内部 core target→外部 library L，而没有允许的 L 声明
- **THEN** 递归审计显示完整 target 链并失败，不仅检查 actor helper 的直接 DEPS

### Requirement: Actual link and usage graphs must match declarations

构建 SHALL 在配置/生成结束核对声明图与实际 link/usage 图，覆盖 alias、INTERFACE、OBJECT、静态 LINK_ONLY 传递边和当前配置的 generator expressions。直接跨包 target 边 SHALL 有直接声明，public/interface 传递闭包 SHALL 按 visibility 传播。未声明 imported target、外部绝对 archive/include/link 路径、裸库链接和无法核验的表达式 MUST 明确拒绝；仅按已登记的 toolchain/system provenance 处理固有运行库。

#### Scenario: An actor bypasses the dependency helper
- **WHEN** 包另用 target_link_libraries 添加未声明外部 target
- **THEN** 常规构建的 deferred audit 拒绝，不只在可选 lint 中警告

#### Scenario: A public header depends on a private-only library
- **WHEN** 已安装消费者需要未传播的库头文件或 target 才能编译公开接口
- **THEN** 清洁消费测试失败，必须修正 visibility，不能依赖源码树 include 泄漏

#### Scenario: A library is header-only
- **WHEN** 消费者没有 ELF DT_NEEDED 边，但使用 header-only 包
- **THEN** 仍检查版本、owner、include/usage 与传递闭包，并记录到构建回执

### Requirement: Tests cannot introduce undeclared production dependencies

测试依赖 SHALL 仅在显式 tests profile 下为测试 targets 提供。生产 artifact 的链接和 usage 闭包 MUST NOT 因构建测试而带入仅测试声明的库。

#### Scenario: A test library leaks into the actor artifact
- **WHEN** actor 生产 target 间接链接只在 test_dependencies 声明的库
- **THEN** 审计失败，不把 tests profile 当作整个包的通用依赖豁免

### Requirement: Build receipts cover static and binary provenance

每个产物 SHALL 记录关联 source lock/metadata、完整源码依赖图、实际 provider/compiler/stdlib/ABI 信息及安装文件摘要。Static/header-only 依赖 SHALL 保留身份和来源，即使最终 actor ELF 不含它们的文件名。系统 MUST NOT 将 link 成功或相同 SemVer 声称为已经证明 C++ ABI 兼容。

#### Scenario: A static library implementation changes
- **WHEN** 依赖声明未变但静态库源码或工具链改变并重建消费者
- **THEN** 新 receipt 可区分来源/构建身份与产物，发布不会沿用旧二进制证明

### Requirement: Installed SDK supports the same checks as workspace builds

安装 SDK SHALL 提供同版本 parser/schema/helpers，使独立包使用相同图和审计规则。清洁环境构建 MUST 不依赖 OBCX 源码目录、未声明全局目标或残留安装文件；未知旧 schema SHALL 明确拒绝。

#### Scenario: A standalone actor consumes an installed ordinary library
- **WHEN** 从空前缀安装 SDK 与已声明 library 开发包，再构建 actor
- **THEN** 版本/target/visibility/closure 检查与 workspace 模式等价，actor 可安装和调用

### Requirement: Build checks state their trust boundary

受支持构建形式 SHALL fail closed，configure 隐式抓取依赖 SHALL 被拒绝；发布环境 SHALL 禁止未授权网络与源码树外依赖。文档 MUST 明确包 CMake/custom commands 仍是受信代码，MUST NOT 将 target 审计宣称为恶意构建脚本沙箱或任意代码依赖的完备证明。

#### Scenario: A package requests an untracked download
- **WHEN** 包通过 FetchContent/ExternalProject 或 custom command 尝试获取未准备来源
- **THEN** 受控构建拒绝该动作并要求显式依赖来源，不接受网络成功作为来源证明
