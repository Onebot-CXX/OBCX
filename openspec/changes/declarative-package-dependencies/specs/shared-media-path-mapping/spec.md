## ADDED Requirements

### Requirement: Path mapping is an independently declared ordinary library

路径映射 SHALL 在独立 Git 仓库中维护，具有独立 package ID/version/metadata/export target 的静态 PIC 普通库，由 bridge 与 ExHentai 在 TOML 显式直接依赖。Core MUST NOT 跟踪 mapper 源码或保留其实现副本；workspace SHALL 显式绑定来源，不隐式拉取。库 SHALL 无 actor factory、无全局可变配置、不依赖 OBCX actor SDK/logger；MUST NOT 通过跨 actor 源码 include 或链接对方内部 archive 共享。

#### Scenario: Both actors consume the mapper
- **WHEN** 同一 workspace 选择 bridge 和 ExHentai，二者声明兼容的 mapper 约束
- **THEN** resolver 检查同一个 mapper 来源/版本，两个 actor 各自静态链接，不需要 runtime mapper actor 或单独 mapper DSO

#### Scenario: Development uses an independent local repository
- **WHEN** 开发工作区从 `local_library/obcx-path-mapping` 选择 mapper
- **THEN** 该目录有自己的 Git 仓库身份，core 不跟踪其文件或 gitlink，构建按显式来源/锁消费，无需先创建提交或配置远程地址

#### Scenario: The mapper source binding is removed
- **WHEN** 仍有 actor 声明 mapper，但 workspace 缺其 source
- **THEN** 构建前报告缺失依赖，不退回某个 actor 自带复制版本

### Requirement: Mapping roots and target identity are explicit

调用方 SHALL 显式提供 mapping/installation key、host_root 和 peer_root，根路径必须合法、绝对、非空。映射 SHALL 与目标身份关联，不得按最近调用或全局默认选择。未知目标 MUST 失败，不自动选择另一个映射或返回原输入路径。

#### Scenario: Two installations have different peer roots
- **WHEN** 同一 host 文件分别映射给两个明确目标
- **THEN** 返回各自配置的 peer 路径，不串用 installation 配置

#### Scenario: A mapping is missing
- **WHEN** 请求目标没有显式映射
- **THEN** 返回结构化错误，不推断容器目录、不隐式传宿主机路径

### Requirement: Path containment uses component boundaries

Mapper SHALL 规范化并按路径分量核验 host containment，拒绝 `..` 逃逸、根目录前缀碰撞和非法路径输入。Peer 路径 SHALL 仅做词法处理，MUST NOT 对容器/远端命名空间路径使用宿主机 filesystem canonical/stat 来判定有效性。

#### Scenario: A sibling has a matching string prefix
- **WHEN** host_root 为 `/data/media`，输入为 `/data/media-other/a.png`
- **THEN** 拒绝映射，不接受字符串 starts_with 的假包含关系

#### Scenario: Normalization escapes the root
- **WHEN** 输入规范化后落在 host_root 外
- **THEN** 明确失败，不能警告后返回原路径

### Requirement: Lexical mapping does not claim filesystem safety

接口和文档 SHALL 区分 lexical mapping、已存在文件的真实 containment 检查、以及新文件安全创建流程。调用方对实际文件访问 SHALL 采用可防 symlink 逃逸的受控 root 流程；MUST NOT 将一次 canonical 检查宣称为消除了后续打开的 TOCTOU 风险。

#### Scenario: A symlink points outside the shared root
- **WHEN** 待发送已存在文件通过 symlink 指向 root 外
- **THEN** 安全文件访问校验拒绝，不能仅凭词法路径位于 root 内就交给 provider

### Requirement: File URIs are correctly encoded

映射输出 file URI 时 SHALL 按 URI 规则编码绝对 peer 路径，正确处理空格、百分号、井号与 Unicode，MUST NOT 通过多加斜杠或不转义字符的字符串拼接生成歧义 URI。

#### Scenario: A filename contains reserved characters
- **WHEN** peer 文件名包含空格、`#` 或 `%`
- **THEN** URI 解码后准确对应原路径，不产生 fragment 或错误转义

### Requirement: Mapping does not replace transport or file ownership

库 SHALL 明确仅转换受控路径，不创建挂载、不搬运文件、不下载媒体、不自动改权限、不发送消息或删除文件。消费方 SHALL 单独负责实际共享可读性、媒体验证和在途文件生命周期；不确定发送后 MUST NOT 因 URI 已生成就立刻删除文件。

#### Scenario: A mapped file is not visible in the provider namespace
- **WHEN** 两边没有实际共享该文件或 provider 无读取权限
- **THEN** 文档和消费方验证明确指出挂载/权限前提未满足，不宣称 mapper 已完成文件传输

#### Scenario: A send outcome is uncertain
- **WHEN** provider 可能仍在读取一个已映射文件
- **THEN** mapper 不主动清理，文件 owner 按独立明确的保留策略处理，不由无状态库猜测超时
