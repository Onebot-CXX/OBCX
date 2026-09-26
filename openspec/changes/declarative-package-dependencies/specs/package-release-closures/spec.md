> 后置阶段：用户明确项目仍处于开发期。此文件的新发布/分发闭环保留为后续要求，当前不据此阻塞 core 工作区与 mapper 实施，也不宣称已经完成。既有 runtime staging 安全检查和回归仍须保留。

## ADDED Requirements

### Requirement: Release packaging is driven by the selected package inventory

发布 SHALL 从 resolved graph、安装 inventory 和 build receipt 获取包列表与文件，不写死 actor 名称。每个文件 SHALL 有相对路径、所属包及内容摘要；包元数据与已锁定元数据 SHALL 一致。只有实际构建并验证的平台 SHALL 生成对应发布资产。

#### Scenario: A new actor or library enters the workspace
- **WHEN** 新包已进入所选图并完成安装验收
- **THEN** 发布工具根据 inventory 识别它，不需要修改写死的 bridge/message_store 列表

#### Scenario: An advertised platform was not verified
- **WHEN** 元数据宣称某个平台但本次没有对应的构建/验证证据
- **THEN** 不生成该平台下载资产，不复制另一个平台产物冒充

### Requirement: Actor deployment archives contain complete private shared closures

Shared library 的 actor-private 闭包 SHALL 递归包含全部必要 DSO，并与声明图及 ELF DT_NEEDED 核验。缺失或未声明私有依赖、逃逸 package root 的文件/符号链接、冲突 SONAME、指向源码/构建目录的 RPATH/RUNPATH SHALL 阻止发布。有私有闭包的裸 actor DSO MUST NOT 被标为完整可部署包。

#### Scenario: An indirect private library is missing
- **WHEN** actor→A.so→B.so，但 install archive 缺少 B.so
- **THEN** 发布验证失败，不等用户 dlopen 才发现缺失

#### Scenario: A binary contains an unexpected private dependency
- **WHEN** ELF 指向 TOML 允许闭包以外的私有 DSO
- **THEN** 拒绝发布并显示不一致边，不靠 ldd 在开发机恰好找到它放行

### Requirement: Library developer packages retain their usage requirements

Library 开发包 SHALL 包含对应 headers、CMake config/export、package metadata、receipt 及其形态所需 archive/DSO，保留 public/interface/transitive 依赖。Static/header-only 在 actor 运行部署中无需独立动态加载，但 SHALL 保留源码依赖 provenance。

#### Scenario: A static consumer is deployed
- **WHEN** actor 静态链接 mapper 并通过构建审计
- **THEN** actor runtime 不要求 mapper archive 随运行包加载，但 receipt 仍列出 mapper 来源与版本

#### Scenario: A library is installed outside its original source tree
- **WHEN** 下游使用干净安装的开发包
- **THEN** export target 及传递 usage 可解析，不访问原源码/构建目录

### Requirement: Private dynamic dependencies retain generation isolation

Runtime SHALL 继续对包内私有依赖按内容版本化动态链接身份、改写并核验 DT_NEEDED、随 actor generation 保活。Library 更新 MUST NOT 通过单独覆盖全局 `.so` 来绕过完整包切换；普通 library MUST NOT 被当作 actor 实例加载。

#### Scenario: An old invocation overlaps a library upgrade
- **WHEN** 旧 actor 调用仍悬停，新 actor 与新版私有库已准备并切换
- **THEN** 旧调用完成时只使用旧依赖，新调用只使用新依赖，旧库不会提前卸载

#### Scenario: An operator rolls back a faulty package
- **WHEN** 新库行为错误而需要回滚
- **THEN** 恢复完整已验证 artifact set 和匹配 actor 配置，不在仍运行的 generation 下只替换公共库文件

### Requirement: Process-owned dependencies remain outside actor-private substitution

SDK、C++ runtime 和已登记 process-owned 依赖 SHALL 按既有身份/ABI约束处理，MUST NOT 为使 reload 通过而伪装成 actor-private 多版本依赖。跨库对象和函数指针的 ABI 边界 SHALL 在契约及测试中明确。

#### Scenario: An update changes a process-owned runtime identity
- **WHEN** candidate 需要与当前进程不兼容的 SDK/runtime 身份
- **THEN** 拒绝热更新并明确需要配套进程部署，不私自加载第二份 SDK 规避检查

### Requirement: Registry metadata is shared with build validation

Registry SHALL 使用与 SDK tooling 同版本的规范 schema/validator，带 kind 区分 actor/library，来源固定且有摘要。第一版索引 SHALL 用于发布/检查显式版本，不自动为 workspace 求解版本。旧 actors-only schema/index SHALL 在 cutover 一次迁移，不维持手抄 validator 分叉。

#### Scenario: A library entry has an actor factory field
- **WHEN** kind=library 的 entry 声明 actor entrypoint 或错误 artifact 类型
- **THEN** registry 使用同一规范校验拒绝，与本地构建判定一致

#### Scenario: Registry and installed SDK use incompatible metadata tooling
- **WHEN** registry 使用的 tooling 版本不能理解 package schema
- **THEN** 明确失败并要求配套升级，不把未知字段当可忽略扩展

### Requirement: Releases are tested from an isolated installed environment

发布验收 SHALL 从空目录与前缀进行，清理 loader 环境，安装完整包集并执行 actor 调用、shared closure reload 和回滚测试。日志/报告 SHALL 记录 source lock、artifact inventory、工具链与结果，不依赖本机源码 checkout 或未声明动态库。

#### Scenario: A release only works with the developer loader path
- **WHEN** 清除 LD_LIBRARY_PATH 等环境后 actor 无法加载
- **THEN** 发布验收失败，必须修复闭包/安装布局，不以开发机测试通过替代
