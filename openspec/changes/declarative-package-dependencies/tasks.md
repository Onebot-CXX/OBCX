## 当前执行范围（用户最新决定）

项目仍在开发阶段，先完成 **core 工作区内的声明、解析、实际构建检查和 mapper 复用**。暂不推进独立 SDK 二进制/头文件发布、外部 SDK 消费闭环或协调发版，也不以这些问题阻塞当前实施。

- 当前顺序：1–6 的本地开发部分 → 9 的 mapper/两个消费者 → 10 的本地回归与文档。
- **后置**：5.8；第 7 节的新发布/分发闭环；2.3/4.4/8 中面向发行物的工具 pin、installed-sdk 扩展验收和远程发行流程；3.7 的正式发布来源策略。
- 本地 cutover 必需的 schema、validator、元数据、fixtures 和脚本引用迁移仍须协调完成，不能以“发布后置”为由新增 v1 兼容 reader 或留下活跃的相互矛盾规则。既有 SDK 功能与 stager 安全回归保留，不为缩小范围删除或静默跳过测试。
- 1.4 先冻结开发构建需要的契约；发布回执/独立目录布局稍后再定，不要求当前工作区 clean commit 或另建仓库。development 模式仍须显式传入，不新增配置默认值。
- 涉及两个阶段的任务可以分步实施，但全部验收前保持未勾选；后置不等于完成，不据此归档整个变更。
- 用户进一步明确：第三方环境由现有 flake.nix/flake.lock 固定；已回滚额外 flake 改动，不再引入第三方目录/摘要清单生成要求。当前重点是自有包声明、版本与真实链接关系，不能把重复 Nix 的工作作为 mapper 的前置条件。
- 用户要求尽可能减少不必要的测试：优先关键合同/实际构建路径，不给同一规则重复添加单元和集成测试；小步修改跑针对性测试，阶段完成再跑全量。既有有效回归不随意删除。

## 1. 范围、清点与协议冻结

- [x] 1.1 确认规划最终归属、core/registry/bridge/library 等受影响仓库的实施权限及 core 主规格同步位置；不自动把跨仓 delta 同步为 ExHentai 私有主规格。
- [x] 1.2 清点全部 actor.toml、actors.toml、schema/validator 镜像、SDK 安装入口、registry entries、CMake/CI/docs/fixtures 和发布脚本消费者，形成逐项 cutover 清单，包含 chat_llm、probe 和 template。
- [x] 1.3 确定独立 registry 与根目录副本的 source-of-truth 和同版本 tooling 发行方式；记录协调发布/回滚所需仓库与版本边界。
- [ ] 1.4 冻结 package/workspace/source-lock/resolved-graph/build-receipt 字段，明确所有适用字段必填、未知字段拒绝、单 ID 单版本、受支持版本语法、platform/profile/mode/network 语义；提供完整有效及无效 fixtures。

## 2. 唯一元数据实现与旧格式拒绝

- [x] 2.1 实现 schema v2 package.toml actor/library 判别、主 artifact/export target、typed dependencies、test_dependencies、compatibility/publication 校验；库不允许 actor ABI/factory，actor 必须明确 SDK 依赖。
- [x] 2.2 实现 packages.toml roots/sources/provider bindings 校验，path 相对 manifest、Git 固定 commit/subdir、重复 ID/保留 ID/未知字段/缺失必填项拒绝。
- [ ] 2.3 实现可重用 parser/规范化数据 API 与 CLI，集中 field/path diagnostics；SDK/registry 通过 pinned tooling 使用同一实现，不复制 Python 校验器。
- [x] 2.4 添加旧 actor.toml/actors.toml、schema v1、缺省字段和多余字段的拒绝测试；不实现兼容 reader 或通用自动迁移器。

## 3. 显式来源解析与锁文件

- [x] 3.1 实现 roots 的传递 metadata walk 与 kind/identity/target 冲突检查；未使用 binding 不获取，闭包缺来源给出完整链，解析不运行包 CMake。
- [x] 3.2 实现精确 SemVer 比较、逗号交集和 pre-release 规则；拒绝未支持 range，测试兼容/冲突菱形、单 ID 双来源、版本不符与 build metadata 行为。
- [x] 3.3 实现 actor/library/profile 依赖图、环检测和稳定拓扑顺序；测试 source 顺序无关、测试依赖隔离及 library→actor 非法边。
- [x] 3.4 实现 deterministic source lock 与 frozen/check：metadata/workspace/provider drift、缺锁、原子写入和并发互斥；验证 equivalent ordering 不改变锁内容。
- [x] 3.5 实现显式 Git source prepare、共享缓存、commit/subdir/tree 校验、临时目录原子发布及路径逃逸拒绝；拒绝隐式 submodule/LFS 抓取。
- [x] 3.6 实现 offline 和显式联网策略：完整 cache 可用、缺 cache/篡改 cache 明确失败、不借 configure 联网；不接 registry 自动选版本。
- [ ] 3.7 区分 development path 的元数据锁与 build receipt 源摘要，release 的固定 Git/源归档及 dirty 检查；测试本地代码编辑不假装等同旧产物。
- [x] 3.8 实现 resolve/check/explain 输出和机器可读 resolved-packages.json；所有错误包含字段及依赖链，不泄漏 credentials，不写用户绝对路径进可提交锁。

## 4. 系统依赖与 SDK provider 接入

- [x] 4.1 定义 system binding/provenance adapter 接口，实现 CMake config/module/pkg-config package/target/version 核验和无版本时的显式 receipt 检查，不猜 provider 名称。
- [x] 4.2 将现有 vcpkg 需求生成改为消费预先解析的完整闭包，绑定 pinned baseline/ports；移除“先 configure 拉远程 actor 才能收集依赖”的要求。
- [x] 4.3 明确 Nix/core provider 基线与 actor 声明的边界，记录 flake/provenance 锚点，支持已有环境核验而非编写新的系统包安装器。
- [ ] 4.4 实现显式 workspace-sdk/installed-sdk binding、保留 ID 防冒充与版本/target 核验；明确 SDK/process-owned/runtime 不是普通 actor 私有库。

## 5. CMake 图生成与实际依赖审计

- [x] 5.1 新增 package graph 导入和 library helper，actor helper 改读 v2；按拓扑配置已准备源码，不在 configure 下载或重锁；支持显式空 roots 的 SDK-only 构建。
- [x] 5.2 实现 package owner、主 artifact/export alias 与内部 targets 登记，校验类型/PIC/目标重复；支持 bridge_core/exhentai_fetch_core 等内部实现 target，不把它们误作外部包。
- [x] 5.3 根据 TOML libraries/system visibility 建立主 target 与内部实现的使用关系；删除第二份未核验的外部 DEPS 清单，测试 public/private/interface 传播。
- [x] 5.4 实现 deferred + File API 依赖审计，覆盖 alias/INTERFACE/OBJECT/LINK_ONLY 和当前配置 generator expressions；未支持形式明确报错，不静默跳过。
- [x] 5.5 拒绝未声明跨包边、无 owner imported targets、包外 archive/include/link 路径和裸链接，允许且仅允许已声明 provider/toolchain 内部闭包；测试“经内部 target 隐藏依赖”路径。
- [ ] 5.6 隔离测试依赖与生产 artifact，检测仅测试库泄漏；加入清洁安装消费测试发现 public header 对 private dependency 的泄漏。
- [ ] 5.7 生成 build receipt，记录 static/header-only 依赖、实际 source/toolchain/provider/ABI 身份；明确 target 审计不是恶意 CMake 沙箱，CI 禁止 configure 未授权网络/下载。
- [ ] 5.8 安装 SDK 的 parser/schema/helpers 和库 export 支持；在无 core 源码路径的干净环境验证 actor+普通库独立构建、安装、加载和调用。

## 6. 所有维护包的一次性 cutover

- [x] 6.1 根据清单将根构建选择改为 packages.toml/packages.lock，所有 actor/template/fixtures 改成 v2 package.toml；保持 runtime actor 配置/ABI V2 语义不变，不把库注入 runtime requires。
- [x] 6.2 更新根/各子仓库 CMake、测试入口、Nix/vcpkg 使用步骤和 CI，显式配置新选项；不顺便新增默认值或覆盖其他在途改动。旧容器/发行协调入口明确拒绝执行而非伪装 v2；新发行实现仍后置，chat_llm/template 二进制尚未实际验收。
- [x] 6.3 删除旧格式 parser/schema/helper alias/文档活跃入口，运行仓库和已安装 SDK surface 审计；旧文件名仅允许出现在明确历史或 breaking-change 说明。既有 SDK smoke 和 stager 回归通过，详见 implementation.md。

## 7. 安装、分发与私有动态闭包（新发布闭环后置，保留既有安全回归）

- [ ] 7.1 生成按包归属的 install inventory，包含 metadata/receipts/export/header/archive/DSO 与文件摘要；验证相对路径、符号链接边界和源码/构建 RPATH 泄漏。
- [ ] 7.2 改造发布脚本从图和 inventory 发现包，不写死 actor 列表；支持完整 actor install archive 和 library developer package。
- [ ] 7.3 核对 DT_NEEDED 与声明/安装闭包，递归收集 actor-private DSO；缺间接库、未声明库、SONAME 冲突或闭包逃逸拒绝，有私有依赖的裸 DSO 不标为可部署资产。
- [ ] 7.4 复用并扩展 stager 测试：A.so→B.so 闭包，两代同名不同内容库并存，旧悬停调用保活、候选失败清理、完整包回滚及 process-owned 身份变化拒绝热更新。
- [ ] 7.5 更新空前缀发布验证和 rollback rehearsal，清理 loader 环境并禁止源码树依赖；每个平台的发布资产都有真实构建/加载证据。

## 8. Registry 与主规格切换

- [ ] 8.1 迁移 registry entry/schema/index 为 actor/library kind 判别的 package.toml/index/packages.json，改为 pinned canonical tooling，不自动选择版本或伪造平台资产。
- [ ] 8.2 更新维护中的所有 entries、generator/workflow/docs 和 core 中的 conformance 快照，检测 validator 版本漂移，不持续输出旧 actors-only 索引。
- [ ] 8.3 在获准的 core 区域同步 actor-package-ecosystem delta，明确新能力主规格各自归属；不在子仓库误生成 core 主规格或自动归档跨仓变更。

## 9. Mapper 作为首个真实普通库

- [x] 9.1 在独立 Git 仓库 `local_library/obcx-path-mapping/` 建立 static-library + PIC 的 path-mapping 包、导出 target、安装接口和显式测试依赖；core 不跟踪库源码或 gitlink，无 actor factory、SDK/logger 依赖和全局可变状态。
- [x] 9.2 实现显式目标/root 映射和结构化错误，修复组件边界/前缀碰撞/越界后返回原值，禁止在宿主机解析 peer filesystem。
- [x] 9.3 实现正确 file URI 编码并区分 lexical map 与安全 host 文件访问；测试绝对根、空配置、..、symlink、空格/#/%/Unicode 和两个 installation 隔离，不作 TOCTOU 虚假保证。
- [x] 9.4 Bridge 改为 TOML 声明并链接 mapper，保留媒体行为、配置显式性和文件 owner 责任；不跨目录引用 ExHentai，也不从 mapper 删除文件。
- [x] 9.5 ExHentai 声明并消费 mapper，验证缺来源/不匹配版本会在构建前失败；移除复制实现或“手工 add_subdirectory 即可绕过”路径。配置解析已实际调用公共库，生成后链接审计通过；共享文件发送仍属后续画廊实现，不能据此声称已发送文件。
- [ ] 9.6 回到 qq-gallery-forward-batches，先更新 file URI、实际挂载、文件 lease/uncertain 保留、清理与容量规格及任务；保持单合集图文顺序，不替用户选择隐式路径/磁盘预算。

## 10. 总体验收与交付

- [ ] 10.1 运行完整负例矩阵：缺来源/锁/版本冲突/环/kind/未声明隐藏边/测试依赖泄漏/坏缓存/离线缺项/平台不符/不完整 DSO/旧 schema，全链路 fail closed。
- [ ] 10.2 运行 static/shared/header-only 三种 fixture、两个真实 mapper 消费者、干净 SDK 构建、生产与测试 profile 和完整发布/回滚；至少 6 worker，低负载全部核心，记录准确结果。
- [ ] 10.3 完成 breaking migration、来源/锁/provider/调试命令及完整包回滚文档；明确不承诺恶意构建沙箱、ABI 自动证明、文件自动共享或 LLOneBot 修改。
- [ ] 10.4 严格校验 OpenSpec 并复核所有仓库差异范围；提交前根目录 nix fmt，未签名提交提醒补 GPG 签名，未经请求不归档或部署。
