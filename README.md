# OBCX

> 本文档尚未完善，仍是半成品

OBCX 是一个基于 C++26(或C++20 + C23) 的跨平台机器人框架，支持 QQ（基于 OneBot 11 协议）和 Telegram（基于官方 Bot API）平台。框架采用插件化架构，支持热重载，使用现代 C++ 协程实现异步操作。

## 特性

- **多平台支持**：QQ（OneBot 11）、Telegram（Bot API）
- **多种连接方式**：WebSocket、HTTP
- **插件系统**：支持动态加载/卸载和热重载
- **代理支持**：HTTP/SOCKS 代理
- **异步架构**：基于 Boost.Asio 和 C++20 协程
- **国际化**：支持多语言（中文、英文）

## 系统要求

### 编译器

- unix-like环境下的GNU/LLVM编译器
- 需要支持 C++23/C++26 标准

### 依赖工具

- CMake 3.20+
- Ninja（推荐）或 Make
- vcpkg（包管理器）
- gettext（可选，用于国际化）

## 安装依赖

### 1. 安装 vcpkg

```bash
# 克隆 vcpkg
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg

# 引导安装
./bootstrap-vcpkg.sh  # Linux/macOS
# 或
.\bootstrap-vcpkg.bat  # Windows

# 设置环境变量
export VCPKG_ROOT=/path/to/vcpkg
export PATH=$VCPKG_ROOT:$PATH
```

### 2. 安装项目依赖

项目使用 vcpkg manifest 模式。`vcpkg.json` 由脚本根据启用的插件自动生成：

```bash
# 生成 vcpkg.json（合并核心依赖 + 插件依赖）
python3 cmake/gen_vcpkg_manifest.py plugins.toml

# 查看所有需要的包（适用于非 vcpkg 用户）
python3 cmake/gen_vcpkg_manifest.py plugins.toml --list
```

核心依赖声明在 `vcpkg-base.json`，各插件的额外依赖声明在各自的 `plugin.toml` 的 `[build].vcpkg_deps` 字段中。
生成 `vcpkg.json` 后，依赖会在 CMake 配置时自动安装，或者手动执行 `vcpkg install`。

> **不使用 vcpkg？** 运行 `python3 cmake/gen_vcpkg_manifest.py plugins.toml --list` 查看需要安装的包列表，然后通过系统包管理器或 Nix 安装即可。

### 3. 安装 LLOneBot（QQ 支持）

LLOneBot 是 QQ 的 OneBot 11 协议实现，用于连接 QQ。

1. 安装 QQNT（QQ 新版本）
2. 安装 LiteLoaderQQNT：<https://github.com/LiteLoaderQQNT/LiteLoaderQQNT>
3. 安装 LLOneBot 插件：<https://github.com/LLOneBot/LLOneBot>

配置 LLOneBot：

- 启用正向 WebSocket，设置端口（如 3001）
- 或启用 HTTP 服务

### 4. 创建 Telegram Bot

1. 在 Telegram 中找到 @BotFather
2. 发送 `/newbot` 创建新机器人
3. 获取 Bot Token

## 编译

### 配置项目

```bash
# 使用 vcpkg 工具链
cmake -B build -GNinja \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

# 或者如果依赖已安装
cmake -B build -GNinja
```

### 编译选项

| 选项                 | 默认值 | 说明                         |
| -------------------- | ------ | ---------------------------- |
| `ENABLE_DEBUG_TRACE` | OFF    | 启用带文件名和行号的调试日志 |

```bash
# 启用调试追踪
cmake -B build -GNinja -DENABLE_DEBUG_TRACE=ON
```

### 编译

```bash
# 编译
cmake --build build

# 编译 Release 版本
cmake -B build -GNinja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 运行测试

```bash
cd build
ctest
```

## 使用

### 配置文件

创建 TOML 配置文件（参考 `examples/plugins/example_plug_config.toml`）：

```toml
[global]
locale = "zh_CN.UTF8"

# QQ Bot 配置
[bots.qq_bot]
type = "qq"
enabled = true
plugins = ["qq_to_tg"]

[bots.qq_bot.connection]
type = "websocket"
host = "127.0.0.1"
port = 3001
access_token = ""
use_ssl = false
timeout = 30000
heartbeat_interval = 5000

# Telegram Bot 配置
[bots.telegram_bot]
type = "telegram"
enabled = true
plugins = ["tg_to_qq"]

[bots.telegram_bot.connection]
type = "http"
host = "api.telegram.org"
port = 443
access_token = "YOUR_BOT_TOKEN"
use_ssl = true
timeout = 30000

# 代理配置（可选）
proxy_host = "127.0.0.1"
proxy_port = 10086
proxy_type = "http"

# 插件配置
[plugins.qq_to_tg]
enabled = true
callbacks = ["on_message", "on_notice"]

[plugins.qq_to_tg.config]
database_file = "bridge_bot.db"
enable_retry_queue = true

# 群组映射
[group_mappings]

[[group_mappings.group_to_group]]
telegram_group_id = "YOUR_TG_GROUP_ID"
qq_group_id = "YOUR_QQ_GROUP_ID"
mode = "group_to_group"
show_qq_to_tg_sender = true
show_tg_to_qq_sender = true
```

### 运行

```bash
<pathto/obcx>/obcx --config config.toml
```

### CLI 命令

运行时支持以下命令：

- `reload` - 热重载所有插件
- `exit` / `quit` - 退出程序

## 插件系统

OBCX 采用动态库（`.so`/`.dylib`）实现插件系统，支持运行时加载、卸载和热重载。

### 插件加载机制

插件系统基于 POSIX `dlopen`/`dlsym`/`dlclose` API 实现：

1. **加载阶段**：`PluginManager` 通过 `dlopen()` 加载插件动态库
2. **符号解析**：使用 `dlsym()` 获取插件导出的工厂函数：
   - `obcx_create_plugin()` - 创建插件实例
   - `obcx_destroy_plugin()` - 销毁插件实例
3. **实例管理**：通过 `SafePluginWrapper` RAII 包装器管理插件生命周期

```
┌─────────────────────────────────────────────────────────────┐
│                      PluginManager                          │
├─────────────────────────────────────────────────────────────┤
│  load_plugin()                                              │
│    ├── dlopen(plugin.so)          // 加载动态库             │
│    ├── dlsym("obcx_create_plugin") // 获取创建函数          │
│    ├── dlsym("obcx_destroy_plugin")// 获取销毁函数          │
│    └── create_plugin()             // 创建插件实例          │
│                                                             │
│  initialize_plugin()                                        │
│    └── plugin->initialize()        // 注册事件回调          │
│                                                             │
│  shutdown_plugin()                                          │
│    └── plugin->shutdown()          // 清理资源              │
│                                                             │
│  unload_plugin()                                            │
│    ├── destroy_plugin(ptr)         // 销毁实例              │
│    └── dlclose(handle)             // 卸载动态库            │
└─────────────────────────────────────────────────────────────┘
```

### 插件生命周期

| 阶段   | 方法              | 说明                            |
| ------ | ----------------- | ------------------------------- |
| 加载   | `load_plugin()`   | 加载 `.so` 文件，获取导出符号   |
| 初始化 | `initialize()`    | 注册事件回调，初始化资源        |
| 运行   | -                 | 事件通过 `EventDispatcher` 分发 |
| 关闭   | `shutdown()`      | 清理资源，取消异步任务          |
| 卸载   | `unload_plugin()` | 销毁实例，`dlclose` 卸载库      |

### 热重载

运行时输入 `reload` 命令可热重载所有插件：

```cpp
// 1. 清除所有 bot 的事件处理器（防止悬空指针）
for (auto& bot : bots) {
    bot->clear_event_handlers();
}

// 2. 关闭所有插件
plugin_manager.shutdown_all_plugins();

// 3. 卸载所有插件
plugin_manager.unload_all_plugins();

// 4. 重新加载配置和插件
config_loader.reload_config();
for (const auto& plugin_name : config.plugins) {
    plugin_manager.load_plugin(plugin_name);
    plugin_manager.initialize_plugin(plugin_name);
}
```

**注意**：热重载前必须调用 `clear_event_handlers()` 清除事件回调，否则 `dlclose` 后回调函数指针将悬空，导致段错误。

### 开发插件

#### 插件结构

```cpp
#include "interfaces/plugin.hpp"

class MyPlugin : public obcx::interface::IPlugin {
public:
    [[nodiscard]] auto get_name() const -> std::string override {
        return "my_plugin";
    }

    [[nodiscard]] auto get_version() const -> std::string override {
        return "1.0.0";
    }

    [[nodiscard]] auto get_description() const -> std::string override {
        return "My custom plugin";
    }

    auto initialize() -> bool override {
        // 获取所有 bot 并注册事件回调
        auto [lock, bots] = get_bots();
        for (auto& bot : bots) {
            bot->on_event<MessageEvent>([this](auto& bot, auto event)
                -> asio::awaitable<void> {
                // 处理消息
                co_return;
            });
        }
        return true;
    }

    void deinitialize() override {}

    void shutdown() override {
        // 清理资源：停止异步任务、释放数据库连接等
        // 注意：不要在这里清除事件处理器，由框架统一管理
    }
};

// 导出插件工厂函数
OBCX_PLUGIN_EXPORT(MyPlugin)
```

#### 导出宏展开

`OBCX_PLUGIN_EXPORT(MyPlugin)` 宏会生成以下 C 导出函数：

```cpp
extern "C" {
    void* obcx_create_plugin();      // 创建插件实例
    void obcx_destroy_plugin(void*); // 销毁插件实例
    const char* obcx_get_plugin_name();
    const char* obcx_get_plugin_version();
}
```

#### 编译插件

```cmake
add_library(my_plugin SHARED my_plugin.cpp)
target_link_libraries(my_plugin PRIVATE obcx_core)
```

也可以使用模板仓库快速创建独立插件项目：[obcx-plugin-template](https://github.com/Onebot-CXX/obcx-plugin-template)

#### plugin.toml

每个插件应包含一个 `plugin.toml` 文件声明元信息和依赖：

```toml
[plugin]
name = "my_plugin"
version = "0.1.0"
description = "My custom plugin"
authors = ["Your Name"]
license = "MIT"

[compatibility]
obcx_abi_version = 1
obcx_min_version = "1.1.0"

[dependencies]
required_plugins = []

[build]
# 插件需要的额外包（OBCX 核心依赖之外的）
# 用于自动生成 vcpkg.json，也作为非 vcpkg 用户的安装参考
vcpkg_deps = ["nlohmann-json", "sqlite3"]
```

`vcpkg_deps` 字段不绑定 vcpkg——它只是声明包名。`CMakeLists.txt` 中使用标准的 `find_package()` 来发现依赖。

#### shutdown() 注意事项

插件的 `shutdown()` 方法必须正确清理所有资源：

```cpp
void MyPlugin::shutdown() {
    // 1. 清除缓存的 bot 指针（reload 后会失效）
    cached_bot_ptr_ = nullptr;

    // 2. 停止异步任务管理器
    if (retry_manager_) {
        retry_manager_->stop();
        retry_manager_.reset();
    }

    // 3. 释放 Handler
    handler_.reset();

    // 4. 数据库单例只置空，不要 reset
    db_manager_ = nullptr;
}
```

## 项目结构

```
OBCX/
├── include/           # 头文件
│   ├── common/        # 通用工具
│   ├── core/          # 核心组件（QQBot、TGBot）
│   ├── interfaces/    # 接口定义
│   ├── network/       # 网络组件
│   ├── onebot11/      # OneBot 11 协议实现
│   └── telegram/      # Telegram API 实现
├── src/               # 源文件
├── examples/          # 示例和插件
│   └── plugins/       # 插件实现
├── tests/             # 测试
├── locales/           # 国际化文件
├── vcpkg.json         # vcpkg 依赖清单
└── CMakeLists.txt     # CMake 配置
```

## 许可证

请查看 [LICENSE](LICENSE) 文件。
