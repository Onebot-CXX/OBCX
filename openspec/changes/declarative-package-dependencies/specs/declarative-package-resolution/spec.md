> 阶段说明：当前实施 development 工作区解析、锁和严格依赖检查。正式 release 来源及独立仓库的同仓锁布局稍后验收；不因此要求本地开发先提交代码、另建仓库或引入模式默认值。

## ADDED Requirements

### Requirement: Workspace selection and source bindings are explicit

Workspace SHALL 使用 schema v2 的 `packages.toml`，显式声明 roots、platform、profile、source bindings 和所需 provider bindings。Actor/library 来源 SHALL 由 ID 唯一绑定为 path 或固定 Git commit/subdir。系统 MUST NOT 按目录名猜 kind、隐式启用未选 roots、使用缺省配置或从 registry 自动寻找来源。

#### Scenario: A transitive library has no binding
- **WHEN** root actor 声明依赖库 L，但 workspace 没有 L 的来源
- **THEN** 在执行包 CMake 前失败，诊断显示 actor→L 链，不搜索系统路径或下载候选替代品

#### Scenario: Source list order differs from dependency order
- **WHEN** 库的 source binding 排在消费者之后
- **THEN** 根据依赖图而不是文件顺序配置，结果与 binding 排列顺序无关

#### Scenario: A source is not reachable from selected roots
- **WHEN** workspace 含未进入所选闭包的 source binding
- **THEN** 列为未使用来源但不获取、不执行、不构建它

### Requirement: Package identity and dependency kinds are validated

解析器 SHALL 检查来源中的 package ID/kind/version 与绑定及依赖边一致，拒绝重复 ID、重复导出 target、身份冲突和对保留 SDK ID 的冒充。Library link edge MUST 指向 library；actor logical edge MUST 指向 actor；library MUST NOT 依赖 actor，actor DSO MUST NOT 作为普通链接库。

#### Scenario: A supposed library contains actor metadata
- **WHEN** libraries 边解析到 kind 为 actor 的 package
- **THEN** 报告 kind mismatch，不因它同样生成 `.so` 就接受

#### Scenario: A provider is accidentally bound under another identity
- **WHEN** source 声明 ID 为 L，实际 package.toml 的 ID 为 M
- **THEN** 拒绝该绑定，锁文件不会悄悄改成 M

### Requirement: Version constraints are evaluated over the complete graph

一个解析环境中每个 package ID SHALL 只选择显式来源给出的一个版本，检查所有直接及传递入边约束。系统 SHALL 使用真实 SemVer 顺序与明确的 pre-release 规则，支持 `=/>=/<=/>/<` 的逗号交集，拒绝未实现的 range 语法，MUST NOT 用 regex 匹配替代版本验证或自动另选版本。

#### Scenario: A diamond dependency is compatible
- **WHEN** A、B 均依赖 L，固定版本同时满足两条约束
- **THEN** 图中只有一个 L 节点，所有消费者使用该版本及来源

#### Scenario: A diamond dependency conflicts
- **WHEN** 固定 L 版本无法同时满足 A、B 的约束
- **THEN** 报告 A→L、B→L 的约束和实际版本，不下载其他版本、不容忍双版本

#### Scenario: A pre-release or unsupported range is encountered
- **WHEN** 约束未显式允许 pre-release 却选择 pre-release，或使用未支持的 `^/~/*/||`
- **THEN** 校验失败并解释原因，不按字符串近似比较

### Requirement: Cycles and profiles are resolved before build execution

解析器 SHALL 针对所选 platform/profile 构造完整图，检查自依赖及环并产生稳定拓扑排序。Tests profile 的依赖 SHALL 明确加入；production profile MUST NOT 获取仅测试所需的包。Actor logical dependencies SHALL 检查包存在及版本，但 MUST NOT 自动创建运行时实例或改写 runtime requires。

#### Scenario: A transitive cycle is present
- **WHEN** 所选图存在 A→B→C→A
- **THEN** 执行任意包 CMake 前拒绝，显示完整环

#### Scenario: A production build excludes test helpers
- **WHEN** 包只在 test_dependencies 声明测试库 T，workspace 选择 production
- **THEN** T 不进入构建和生产部署闭包

### Requirement: Source locks are deterministic and frozen checks do not mutate

`packages.lock` SHALL 记录规范化 workspace/profile/platform、精确身份/版本/source/commit/subdir、元数据摘要、依赖边及 provider provenance 锚点；输出 SHALL 稳定排序，无时间戳、下载缓存路径或用户机器绝对路径。Frozen 操作 MUST 在锁缺失或元数据/source/约束漂移时失败，MUST NOT 静默重锁。

#### Scenario: A dependency changes without relocking
- **WHEN** package.toml 增删依赖或改变 version range，仍使用原锁执行 frozen 构建
- **THEN** 在执行包构建前失败，指出发生漂移的包和字段

#### Scenario: Equivalent input order is used twice
- **WHEN** 相同来源、元数据和 profile 仅有声明顺序不同
- **THEN** 生成相同规范化锁内容，不制造无意义差异

### Requirement: Local development and release provenance are distinct

调用方 SHALL 显式选择 development 或 release。Development path 包 SHALL 锁定元数据/依赖并在构建回执记录实际源码摘要和 dirty 状态；Release SHALL 要求可重建 commit/subdir 或固定源归档及摘要，拒绝仅有本机 path 的不可重建发布输入。

#### Scenario: Local code changes without dependency changes
- **WHEN** development 模式只修改实现代码
- **THEN** 不要求人为更新依赖锁，但新 build receipt 记录实际源内容，不把它冒充上次产物

#### Scenario: A release uses uncommitted or unrecorded source
- **WHEN** release 模式消费 Git path 包有修改或构建使用未记录源码，且没有明确固定源归档
- **THEN** 发布拒绝，不把工作目录位置当作来源证明

### Requirement: Network access and caches are controlled

联网策略 SHALL 显式选择 allow/deny；offline/frozen MUST NOT 隐式触网。Git 来源 SHALL 固定完整 commit 和受限 subdir，验证内容后原子发布缓存，拒绝目录逃逸、篡改缓存及未锁定的隐式 submodule/LFS 下载。并发锁更新和源码准备 SHALL 防止半成品被消费。

#### Scenario: Offline cache is incomplete
- **WHEN** offline 模式缺少一个已锁定的远程源码对象
- **THEN** 指出 ID/commit/摘要缺失并停止，不借 CMake FetchContent 联网

#### Scenario: Cached metadata is altered
- **WHEN** 缓存内容与已锁定身份/摘要不符
- **THEN** 在构建前拒绝，不因缓存目录名看似正确就接受

### Requirement: Dependency resolution is inspectable without executing package code

系统 SHALL 提供数据级 check/resolve/explain 能力，在获取已允许的元数据后无需运行包 CMake 就能给出节点、边、来源和失败诊断。缺失、版本冲突、环、kind 错误和锁漂移 SHALL 包含可定位的包路径/字段与依赖链；MUST NOT 输出认证凭据。

#### Scenario: An operator asks why a library is needed
- **WHEN** 对某个 library ID 请求 explain
- **THEN** 显示所有相关 root 依赖链、版本约束、选定来源与版本，不需要编译或启动 actor
