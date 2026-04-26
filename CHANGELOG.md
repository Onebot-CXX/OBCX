# Changelog

本文档记录OBCX项目的所有重要变更。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

---

## [Unreleased] (2026-04-21)

### 新增

- **Core TUI**: 新增基于 FTXUI 的终端界面，提供分屏日志与命令交互视图
- **CLI**: 支持 `--no-tui` 参数，可在调试或非交互环境下回退到标准输出日志
- **Plugin System**: 支持通过 `plugins.toml` / `plugin.toml` 管理本地与远程插件，并按启用插件生成 `vcpkg.json`
- **Nix**: 新增 `flake.nix` 开发环境支持，便于在 Nix Shell 中构建项目

### 修复

- **HTTP Client**: 正确设置 Telegram 请求超时，修复长轮询超时配置不生效的问题
- **Bridge Plugin**: 移除 PathManager 中的硬编码路径，修复媒体与资源路径解析问题
- **Build**: 适配新版 Boost，移除对 `boost::system` 的依赖以修复编译问题

### 重构

- **Plugin Examples**: 将仓库内示例插件迁移到独立子仓库，主仓库聚焦核心框架与插件接口
- **Plugin System**: 移除无用的二进制下载程序，简化插件获取与加载流程
- **Plugin Config**: 插件路径无需再手动配置，简化启动与部署配置

### 性能优化

- **TUI**: 修复 3 个主要 CPU 热点，替换高开销 `std::regex` 并对日志渲染进行虚拟化

### 文档

- **README**: 更新构建、插件与依赖说明

### 依赖

- **Nix Flake**: 更新 `flake.nix`，同步开发环境配置

---

## [Unreleased] (2026-01-25)

### 新增

- **HTTP Client**: 完善HTTP客户端超时机制，支持请求超时控制
- **Test**: 添加HTTP客户端超时测试 `http_client_timeout_test.cpp`

### 重构

- **HTTP Client**: 大幅重构HTTP客户端实现，优化代码结构和错误处理
- **Bot Interface**: 简化机器人接口设计，移除冗余方法
- **Core**: 精简QQBot和TGBot核心实现

### 修复

- **Chat LLM**: 添加响应过滤器，去除LLM回复中的user_id前缀（如 "6545430341: content"）
- **Test Chat LLM**: 修复chat_llm测试中引用已移除的connect_ws和connect_http方法导致的编译错误

### 优化

- **Bridge Plugin**: 增强QQ到Telegram和Telegram到QQ的桥接功能
- **Retry Queue**: 改进重试队列管理器，提升消息转发可靠性
- **Proxy HTTP**: 优化代理HTTP客户端实现

### 国际化

- 添加HTTP相关日志消息的中英文支持

---

## [Unreleased] (2026-01-17)

### 新增

- **Plugin Chat**: 新增 `chat_llm` 示例插件，提供群聊 `/chat <text>` 命令，调用 OpenAI 兼容接口并将回复发送回群

### 修复

- **Chat LLM**: 修复配置被默认值覆盖导致群聊消息无法入库
- **Chat LLM**: 修复消息 role 判断，使用 is_bot 字段而非 self_id 比较
- **Chat LLM**: 修复 `/chat` 命令不入历史记录的问题
- **Chat LLM**: 调整消息插入顺序（查询数据库→插入命令→发送LLM请求）
- **Chat LLM**: 修复插入数据库的命令内容包含 `/chat` 前缀

### 测试

- **Chat LLM**: 添加 `/chat` 命令流程测试与集成测试，用于验证插件加载与消息发送路径

---

## [0.x.x] - 2026-01-15

### 新增

- **Plugin Manager**: 支持通过环境变量或控制台设置日志级别

### 修复

- **Plugin Bridge**: 修复UTC与本地时间不匹配导致的显示名称缓存失效
- **Plugin Bridge**: 正确更新用户名称
- **Plugin Bridge**: 修复用户信息数据库无用更新问题
- **Plugin Bridge**: 修复用户信息数据库更新逻辑

### 重构

- **Plugin Bridge**: 澄清source命名
- **Plugin Bridge**: 将数据库管理器拆分为多个小文件
- **Plugin Bridge**: 更新依赖结构

### 性能优化

- **Plugin Bridge**: Telegram群组消息转发限制为最多10条

### 表情支持

- **Plugin Bridge**: 支持QQ mface图片emoji表情

### 其他

- 更新vcpkg配置格式 && ctest
- 修复clang-tidy提示

---

## [0.x.x] - 2025-12

### 新增

- **i18n**: 添加国际化本地化支持 (使用C++26 `#embed`)
- **Plugin Logger**: 插件日志显示插件名称
- **CLI**: 支持从stdin接受输入命令
- **Config**: 支持通过stdin命令重新加载配置
- **TG Bot Torrent**: 支持从Telegram下载torrent/magnet链接

### 修复

- **Plugin Bridge**: 修复多条图片在单条消息中无法全部转发的问题
- **Plugin Bridge**: 多条转发图片现在以群组方式转发
- **TG2QQ**: 确保更新数据库以映射tg msg_id到新的qq msg_id
- **Database**: 修复reload问题，使用单例数据库管理器
- **Exit Loop**: 修复愚蠢的while循环，使用condition_variable等待退出信号
- **Locale MO Parser**: 使用boost回调解析.mo文件
- **WebSocket Client**: 修复缺失的i18n日志

### 重构

- 重构插件结构
- 重构示例插件

---

## [0.0.2] - 2025-09

### 修复

- **OneBot11 WebSocket**: 修复未被唤醒的等待队列

---

## [0.0.1] - 2025-06

### 初始版本

- Initial commit
- 创建.gitignore

---

## 变更类型说明

- **新增**: 新功能
- **修复**: Bug修复
- **重构**: 代码重构（不影响功能）
- **性能优化**: 性能改进
- **其他**: 其他不重要的变更
