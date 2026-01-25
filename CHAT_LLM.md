# Chat LLM Plugin 业务逻辑文档

## 插件概述

**插件名称**: `chat_llm`
**版本**: `2.0.0`
**描述**: 提供 `/chat` 命令用于与大语言模型(LLM)交互
**支持平台**: QQ、Telegram

## 核心功能

通过 `/chat <内容>` 命令在群组中与 LLM 进行对话，调用 OpenAI 兼容的 API 获取回复。

### 消息存储

插件自动保存群组文本消息到 SQLite 数据库，受配置控制：
- **白名单控制**：仅对配置允许的群组采集消息
- **TTL 清理**：自动删除超过保留期限的消息（默认 1 天）
- **用户消息**: 所有群组成员发送的文本消息
- **机器人回复**: LLM 生成的回复消息

存储的消息用于提供对话上下文，使 LLM 能够理解之前的对话内容。

### 模块化架构

插件采用模块化设计，各组件职责分离：

- **Runtime**: 共享运行时状态，管理生命周期和停止信号
- **CommandParser**: 解析消息事件，提取命令和用户输入
- **MessageRepository**: SQLite 数据库操作（v2 schema，修复 Telegram 跨群冲突）
- **PromptBuilder**: 构建 LLM 上下文，自动去重和过滤
- **LlmClient**: OpenAI 兼容 HTTP 客户端，支持超时和错误处理

## 配置项

在 `config.toml` 的 `[plugins.chat_llm.config]` 节点下配置：

| 配置项 | 必需 | 说明 |
|-------|------|------|
| `model_url` | 是 | OpenAI兼容API的完整URL (如 `https://api.openai.com/v1/chat/completions`) |
| `model_name` | 是 | 模型标识符 (如 `"gpt-4o-mini"`) |
| `api_key` | 是 | API认证密钥 |
| `prompt_path` | 是 | 系统提示文件路径 (相对于仓库根目录) |
| `history_db_path` | 是 | 历史消息数据库路径 (相对于仓库根目录，用于存储和读取消息历史) |
| `collect_enabled` | 否 | 是否启用消息采集 (默认: false) |
| `collect_allowed_groups` | 否 | 允许采集的群组ID列表 (默认: 空=所有群组) |
| `history_ttl_days` | 否 | 历史消息保留天数，超过保留期的消息会被自动清理 (默认: 1) |
| `max_reply_chars` | 否 | 最大回复长度 (默认: 500) |
| `history_limit` | 否 | 历史消息数量限制 (默认: 10) |

## 命令

| 命令名 | 参数 | 说明 |
|-------|------|------|
| `/chat` | `<内容>` | 与LLM对话 |

## 业务流程

### 1. 插件初始化流程

```
initialize()
  ├─ 确定基础目录 (从配置文件路径获取仓库根目录)
  ├─ 加载配置项 (load_configuration)
  │   └─ 检查必填项：model_url, model_name, api_key, prompt_path, history_db_path
  │   └─ 读取可选项：collect_enabled, collect_allowed_groups, history_ttl_days, max_reply_chars, history_limit
  ├─ 解析URL (parse_url)
  │   └─ 提取 scheme、host、port、path
  ├─ 加载系统提示 (load_system_prompt)
  │   └─ 从文件读取系统提示文本
  ├─ 初始化组件
  │   ├─ CommandParser: 命令解析器
  │   ├─ PromptBuilder: 上下文构建器（系统提示 + 历史去重）
  │   ├─ MessageRepository: 数据库仓库（schema v2，TTL 清理）
  │   └─ Runtime: 共享运行时状态（生命周期安全）
  └─ 注册事件回调
      ├─ QQ Bot: 注册 MessageEvent 回调（使用 weak_ptr<Runtime>）
      └─ Telegram Bot: 注册 MessageEvent 回调（使用 weak_ptr<Runtime>）
```

### 2. 消息处理流程

```
process_message(bot, event)
  ├─ 检查: Runtime 是否已停止
  │   └─ 停止则直接返回
  ├─ 检查: 是否为群组消息
  ├─ 解析命令 (CommandParser::parse)
  │   ├─ 提取文本内容
  │   ├─ 检查是否以 "/chat" 开头
  │   └─ 返回 ParsedCommand{type, text, platform, group_id, user_id, message_id}
  ├─ 保存用户消息到数据库 (异步)
  │   ├─ MessageRepository::append_message()
  │   ├─ 检查白名单: collect_enabled + collect_allowed_groups
  │   ├─ 标记 is_command = (type == chat)
  │   └─ 通过 bot.run_heavy_task() 在后台执行
  ├─ 检查: 是否为 /chat 命令
  │   └─ 非命令消息则返回
  ├─ 检查: 输入是否为空
  │   └─ 空输入则返回 "用法: /chat <内容>"
  ├─ 并发控制: Runtime 提供的配置（当前版本使用简单忙检查）
  │   └─ 忙碌则静默丢弃请求
  ├─ 启动 LLM 请求（看门狗超时保护）
  │   ├─ 构建上下文 (PromptBuilder::build)
  │   │   ├─ 系统提示 + 历史消息 + 当前用户消息
  │   │   └─ 自动去重（如果最后一条历史与当前相同则跳过）
  │   ├─ 调用 LlmClient::chat_completion()
  │   │   └─ 使用独立 io_context 创建 HttpClient（避免并发冲突）
  │   └─ 等待: 任务完成 或 超时
  ├─ 情况A: 任务完成
  │   ├─ 过滤LLM响应: 去除 user_id 前缀 (filter_llm_response)
  │   │   └─ 移除开头的 "数字: " 格式（如 "6545430341: content" → "content"）
  │   ├─ 检查响应长度
  │   │   └─ 超过限制 → 记录日志并发送提示
  │   ├─ 发送回复 (send_response)
  │   └─ 保存机器人回复到数据库 (异步，is_bot=1, is_command=0)
  └─ 情况B: 超时 → 静默丢弃请求
```

### 3. LLM API 调用流程

```
LlmClient::chat_completion(messages_json)
  ├─ 创建 HTTP Client (独立 io_context)
  ├─ 构建请求体 (JSON)
  │   ├─ 添加 model 参数
  │   ├─ 设置 stream: false
  │   └─ 添加 messages 数组
  ├─ 发送 POST 请求
  │   ├─ Headers:
  │   │   ├─ Authorization: Bearer <api_key>
  │   │   ├─ Content-Type: application/json
  │   │   ├─ Accept: application/json
  │   │   └─ Accept-Encoding: identity
  │   └─ Body: JSON
  ├─ 解析响应
  │   ├─ 检查 HTTP 状态码
  │   ├─ 解析 JSON
  │   ├─ 检查 error 字段
  │   └─ 提取 choices[0].message.content
  └─ 返回 LlmResponse{success, content, error_message, status_code, response_size}
```

### 4. 历史消息获取流程

```
MessageRepository::fetch_context(group_id, platform, limit, before_timestamp_ms)
  ├─ 检查: 数据库是否已初始化
  ├─ 打开数据库 (SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX)
  ├─ 准备 SQL 语句
  │   └─ SELECT user_id, content, timestamp_ms, is_bot
  │       FROM messages_v2
  │       WHERE platform = ? AND group_id = ? AND timestamp_ms < ? AND is_command = 0
  │       ORDER BY timestamp_ms DESC
  │       LIMIT ?
  ├─ 绑定参数
  ├─ 执行查询
  ├─ 跳过空内容消息
  └─ 反转结果（最旧在前，供 LLM 上下文使用）
```

### 5. 消息存储流程

```
MessageRepository::append_message(record, collect_enabled, allowed_groups)
  ├─ 检查白名单
  │   └─ collect_enabled=false 或 group_id 不在 allowed_groups 中则跳过
  ├─ 检查: content 是否为空
  │   └─ 空内容则跳过
  ├─ 打开数据库 (SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX)
  ├─ 准备 INSERT OR IGNORE 语句
  │   └─ INSERT OR IGNORE INTO messages_v2
  │       (platform, group_id, message_id, user_id, content, timestamp_ms, is_bot, is_command)
  │       VALUES (?, ?, ?, ?, ?, ?, ?, ?)
  ├─ 绑定参数
  ├─ 执行插入
  ├─ 关闭数据库
  └─ 捕获异常并记录日志
```

### 6. TTL 清理流程

```
cleanup_ttl(ttl_days)
  ├─ 计算截止时间: now - (ttl_days * 24 * 60 * 60 * 1000) ms
  ├─ 执行 DELETE 语句
  │   └─ DELETE FROM messages_v2 WHERE timestamp_ms < ?
  ├─ 绑定截止时间参数
  ├─ 执行删除
  └─ 返回删除的行数
```

## 并发控制与生命周期

### 生命周期安全
- 使用 `std::shared_ptr<Runtime>` 作为共享运行时状态
- 事件回调中捕获 `weak_ptr<Runtime>`，协程开始时尝试 `lock()`
- 如果 `lock()` 失败或 `is_stopping()` 为 true，直接返回
- `shutdown()` 时设置 `stopping=true`，取消定时器，阻止后续操作

### 超时保护
- 使用 `Runtime::llm_watchdog` 配置超时时间（默认 120 秒）
- 超时后静默丢弃请求，不发送任何响应

### 异步执行
- 使用 `bot.run_heavy_task()` 在后台线程池执行 LLM API 调用
- 使用 `bot.run_heavy_task()` 在后台线程池执行数据库写入

## 消息格式

### 用户消息格式
```
user_id: user_text
```

### LLM 请求体格式
```json
{
  "model": "gpt-4o-mini",
  "stream": false,
  "messages": [
    {"role": "system", "content": "系统提示文本"},
    {"role": "user", "content": "user1: hello"},
    {"role": "assistant", "content": "bot_id: hi there"},
    {"role": "user", "content": "user2: test"}
  ]
}
```

## 历史数据库表结构（Schema v2）

插件自动创建并管理 `messages_v2` 表，结构如下：

| 字段名 | 类型 | 约束 | 说明 |
|-------|------|------|------|
| `platform` | TEXT | NOT NULL, PRIMARY KEY (1/3) | 平台名称 (qq/telegram) |
| `group_id` | TEXT | NOT NULL, PRIMARY KEY (2/3) | 群组ID |
| `message_id` | TEXT | NOT NULL, PRIMARY KEY (3/3) | 平台消息ID 或 local-bot-{timestamp}-{group_id} |
| `user_id` | TEXT | NOT NULL | 用户ID (机器人消息使用 self_id) |
| `content` | TEXT | NOT NULL | 消息文本内容 |
| `timestamp_ms` | INTEGER | NOT NULL | Unix 时间戳（毫秒） |
| `is_bot` | INTEGER | NOT NULL | 是否为机器人消息 (1=是, 0=否) |
| `is_command` | INTEGER | NOT NULL | 是否为命令消息 (1=是, 0=否) |

**索引**:
- `idx_context`: `(platform, group_id, timestamp_ms DESC)` - 优化历史消息查询性能

**去重策略**: 
- 使用 `(platform, group_id, message_id)` 作为联合主键
- 写入时使用 `INSERT OR IGNORE` 防止重复插入

**关键变更（v2）**:
- 修复 Telegram 跨群 `message_id` 冲突：主键增加 `group_id`
- 支持命令消息过滤：新增 `is_command` 字段
- 支持 TTL 清理：`timestamp_ms` 字段用于自动删除过期消息

## 技术依赖

- **nlohmann_json**: JSON 序列化/反序列化
- **unofficial-sqlite3**: SQLite3 数据库访问
- **boost::asio**: 异步 I/O 和协程支持
- **regex**: URL 解析

## 关键类和方法

### plugins::chat_llm::Runtime
- `Runtime(executor, config)`: 构造函数
- `stop()`: 停止运行时，设置停止信号
- `is_stopping()`: 检查是否正在停止
- `get_config()`: 获取配置
- `schedule_cleanup_task(task)`: 调度 TTL 清理任务

### plugins::chat_llm::CommandParser
- `parse(bot, event) -> ParsedCommand`: 解析消息事件
- `extract_text(msg) -> std::string`: 提取文本内容
- `detect_platform(bot) -> std::string`: 检测平台

### plugins::chat_llm::MessageRepository
- `MessageRepository(db_path)`: 构造函数
- `initialize() -> bool`: 初始化数据库和 schema v2
- `append_message(record, collect_enabled, allowed_groups) -> bool`: 保存消息（支持白名单过滤）
- `fetch_context(query) -> std::vector<HistoryItem>`: 获取上下文
- `cleanup_ttl(ttl_days) -> int`: TTL 清理

### plugins::chat_llm::PromptBuilder
- `PromptBuilder(system_prompt, max_reply_chars)`: 构造函数
- `build(history, user_id, user_text, self_id) -> std::vector<OpenAiMessage>`: 构建上下文
- `is_response_too_long(response) -> bool`: 检查响应是否过长
- `get_max_reply_chars() -> int`: 获取最大回复长度
- `get_system_prompt() -> const std::string&`: 获取系统提示

### plugins::chat_llm::LlmClient
- `~LlmClient() = default`: 虚析构函数
- `chat_completion(messages_json) -> LlmResponse = 0`: 纯虚接口（默认抛出异常）
  
### plugins::chat_llm::OpenAiCompatClient
- `OpenAiCompatClient(ioc, config)`: 构造函数
- `chat_completion(messages_json) -> LlmResponse override`: 实现聊天补全
- `set_timeout(timeout)`: 设置超时

### ChatLLMPlugin 类

#### 公共方法
- `get_name()`: 返回 "chat_llm"
- `get_version()`: 返回 "2.0.0"
- `get_description()`: 返回插件描述
- `initialize()`: 插件初始化
- `deinitialize()`: 插件反初始化
- `shutdown()`: 插件关闭
- `process_message(bot, event)`: 处理消息事件 (公开供测试)

#### 私有方法
- `load_configuration()`: 加载配置
- `load_system_prompt()`: 加载系统提示
- `parse_url(url)`: 解析 URL
- `filter_llm_response(response)`: 过滤LLM响应，去除user_id前缀
- `send_response(bot, cmd, text)`: 发送回复并保存到数据库

#### 成员变量
- `runtime_`: 共享运行时状态（shared_ptr）
- `repo_`: 消息仓库
- `cmd_parser_`: 命令解析器
- `prompt_builder_`: 上下文构建器
- `llm_client_`: LLM 客户端
- 配置项：model_url_, model_name_, api_key_, prompt_path_, history_db_path_, system_prompt_, base_dir_, url_host_, url_port_, url_path_, url_use_ssl_

## 错误处理

### 配置错误
- 缺少必需配置项: 插件初始化失败
- URL 解析失败: 插件初始化失败
- 系统提示文件不存在: 插件初始化失败

### 运行时错误
- LLM API 错误: 返回错误消息给用户
- HTTP 错误: 返回 HTTP 状态码
- JSON 解析错误: 返回解析失败提示
- 响应过长: 记录日志并发送提示

## 日志

插件使用 `PLUGIN_INFO`, `PLUGIN_DEBUG`, `PLUGIN_WARN`, `PLUGIN_ERROR` 宏记录日志。

### 关键日志点
- 插件生命周期 (构造/析构/初始化/关闭)
- 配置加载
- URL 解析
- 系统提示加载
- 消息处理开始/结束
- LLM 请求开始/完成/失败
- 历史消息获取
- 响应发送
- TTL 清理
- 性能计时 (`[TIMING]` 标记)

### 日志脱敏
- LLM 响应 body 不再打印完整内容
- 只记录响应 size、status_code、截断预览（前 200 字）

## 文件位置

- 头文件:
  - `examples/plugins/chat_llm/chat_llm_plugin.hpp`
  - `examples/plugins/chat_llm/include/chat_llm/runtime.hpp`
  - `examples/plugins/chat_llm/include/chat_llm/command_parser.hpp`
  - `examples/plugins/chat_llm/include/chat_llm/message_repository.hpp`
  - `examples/plugins/chat_llm/include/chat_llm/prompt_builder.hpp`
  - `examples/plugins/chat_llm/include/chat_llm/llm_client.hpp`
- 源文件:
  - `examples/plugins/chat_llm/chat_llm_plugin.cpp`
  - `examples/plugins/chat_llm/src/runtime.cpp`
  - `examples/plugins/chat_llm/src/command_parser.cpp`
  - `examples/plugins/chat_llm/src/message_repository.cpp`
  - `examples/plugins/chat_llm/src/prompt_builder.cpp`
  - `examples/plugins/chat_llm/src/llm_client.cpp`
- 构建文件: `examples/plugins/chat_llm/CMakeLists.txt`
