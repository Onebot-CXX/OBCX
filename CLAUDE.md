# OBCX （CPP20 机器人库）

## 项目要求

- 使用 boost::beast作为网络库，asio作为调度器，spdlog进行log输出，sqlte3作为数据库
- 实现以下平台机器人，实现一个通用接口（各平台继承后实现，也可以实现自己的平台特定接口）：
  - QQ：基于 onebot 11协议
  - telegram：基于telegram官方bot api
- 对于不同平台的连接协议，提供如下方法:
  - QQ:
    1. websocket
    2. http
  - Telegram:
    1. http
- 实现一个代理模块，能够为beast连接提供代理
- 实现一个数据库模块，提供基本的数据存储

## 注意事项

- 如果需要生成构建项目，执行以下命令 `cmake -B build -GNinja`
- 执行以下命令来编译项目 `cmake --build build`
- 在进行任何终端下载命令前（curl，git clone, wget等），请确保你已经执行了命令`proxy_on`，来正确设置代理
- 所有机器人的具体实现在 `examples` 目录下,不在 `src` 目录下
- 每当你完成任务，需要构建一下以确保没有语法错误

### Telegram Topic消息处理重要注意事项

- **关键概念**：在Telegram Group中，如果消息包含`message_thread_id`，说明这是Topic消息
- **回复判断逻辑**：
  - 当`reply_to_message.message_id == message_thread_id`时，表示消息是发送到Topic中，**不是回复**
  - 当`reply_to_message.message_id != message_thread_id`时，才是真正的回复其他消息
- **核心代码逻辑**：`has_genuine_reply = (reply_msg_id != thread_id)`
- **重构时必须保持**：这个逻辑在任何重构或修改中都必须严格保持，不能改变
- **测试验证**：修改Topic相关逻辑后，必须验证回复消息的正确识别和处理

### 消息回复跨平台映射逻辑重要注意事项

- **核心原理**：所有回复消息都需要转发，关键是要正确处理四种回复情况的消息ID映射，确保转发后的消息能够引用正确的对应平台的消息ID

- **四种回复情况及处理逻辑**：

  1. **TG回复TG原生消息** → 转发到QQ时：

     - 先查找该TG消息是否曾转发到QQ过 (`get_target_message_id("telegram", 被回复TG消息ID, "qq")`)
     - 如果找到QQ消息ID，在转发时引用该QQ消息；如果没找到，显示回复提示

  2. **TG回复QQ转发消息** → 转发到QQ时：

     - 查找该TG消息是否来源于QQ (`get_source_message_id("telegram", 被回复TG消息ID, "qq")`)
     - 如果找到QQ原始消息ID，在转发时引用该QQ消息ID

  3. **QQ回复QQ原生消息** → 转发到TG时：

     - 先查找该QQ消息是否曾转发到TG过 (`get_target_message_id("qq", 被回复QQ消息ID, "telegram")`)
     - 如果找到TG消息ID，在转发时引用该TG消息；如果没找到，显示回复提示

  4. **QQ回复TG转发消息** → 转发到TG时：
     - 查找该QQ消息是否来源于TG (`get_source_message_id("qq", 被回复QQ消息ID, "telegram")`)
     - 如果找到TG原始消息ID，在转发时引用该TG消息ID

- **数据库查询顺序**：

  - 先查 `get_target_message_id()` - 查找消息是否已转发到目标平台
  - 再查 `get_source_message_id()` - 查找消息是否来源于目标平台
  - 这个顺序确保正确处理所有四种回复情况

- **字段名统一要求**：所有reply segment都必须使用`data["id"]`字段存储消息ID，不能使用`message_id`或其他字段名

- **重要提醒**：这四种情况涵盖了所有可能的回复场景，修改相关逻辑时必须确保四种情况都能正确处理，实现真正的跨平台回复体验

### 插件系统架构与热重载

#### 核心组件

- **PluginManager** (`src/common/plugin_manager.cpp`): 管理插件的加载、卸载、初始化和关闭
- **IPlugin** (`include/interfaces/plugin.hpp`): 插件接口基类，定义生命周期方法
- **EventDispatcher** (`include/core/event_dispatcher.hpp`): 事件分发器，管理事件回调
- **DatabaseManager** (`examples/plugins/dependency/bridge_bot/database_manager.hpp`): 单例数据库管理器

#### 插件生命周期

1. **加载阶段** (`load_plugin`):
   - 通过 `dlopen()` 加载 `.so` 文件
   - 获取 `obcx_create_plugin` 和 `obcx_destroy_plugin` 符号
   - 创建插件实例

2. **初始化阶段** (`initialize_plugin`):
   - 调用插件的 `initialize()` 方法
   - 插件在此阶段注册事件回调 (`bot->on_event<EventType>(...)`)
   - 初始化数据库、Handler等资源

3. **运行阶段**:
   - 事件通过 `EventDispatcher` 分发给已注册的回调

4. **关闭阶段** (`shutdown_plugin`):
   - 调用插件的 `shutdown()` 方法
   - **必须正确清理资源**（详见下方注意事项）

5. **卸载阶段** (`unload_plugin`):
   - 调用 `obcx_destroy_plugin` 销毁插件实例
   - 通过 `dlclose()` 卸载 `.so` 文件

#### CLI Reload 命令

在框架运行时，可以通过stdin输入 `reload` 命令热重载所有插件：

```
reload    - 热重载所有插件
exit/quit - 退出框架
```

#### Reload 执行流程（严格顺序）

```cpp
// Step 1: 清除所有bot的事件处理器（防止悬空函数指针）
for (auto &bot : bots) {
    bot->clear_event_handlers();
}

// Step 2: 关闭所有插件（调用每个插件的shutdown()）
plugin_manager.shutdown_all_plugins();

// Step 3: 卸载所有插件（dlclose .so文件）
plugin_manager.unload_all_plugins();

// Step 4: 重新加载配置文件
config_loader.reload_config();

// Step 5: 更新bot配置
bot_configs = config_loader.get_bot_configs();

// Step 6: 重新加载和初始化插件
for (const auto &config : bot_configs) {
    for (const auto &plugin_name : config.plugins) {
        plugin_manager.load_plugin(plugin_name);
        plugin_manager.initialize_plugin(plugin_name);
    }
}
```

#### 插件 shutdown() 方法必须遵守的规则

**关键**：插件的 `shutdown()` 方法必须正确清理所有资源，否则会导致崩溃：

```cpp
void MyPlugin::shutdown() {
    // 1. 清除缓存的bot指针（它们在reload后会失效）
    cached_bot_ptr_ = nullptr;

    // 2. 停止异步任务管理器（如RetryQueueManager）
    if (retry_manager_) {
        retry_manager_->stop();  // 取消所有pending的异步操作
        retry_manager_.reset();
    }

    // 3. 释放Handler（它们可能持有数据库引用）
    handler_.reset();

    // 4. 释放数据库引用（单例模式下只置空，不要reset）
    db_manager_ = nullptr;  // 单例由DatabaseManager::reset_instance()管理
}
```

#### 为什么必须清除事件处理器

当插件被 `dlclose()` 后，其代码从内存中卸载。如果 `EventDispatcher` 仍持有指向已卸载代码的函数指针，调用这些回调会导致**段错误**。

解决方案：在卸载插件前调用 `bot->clear_event_handlers()` 清除所有回调。

#### DatabaseManager 单例模式

`DatabaseManager` 使用单例模式，多个插件共享同一实例：

```cpp
// 获取单例（首次调用需要提供db_path）
db_manager_ = DatabaseManager::instance(config_.database_file);

// 重置单例（在所有插件都unload后调用）
DatabaseManager::reset_instance();
```

**注意**：插件的 `shutdown()` 中不要 `reset()` db_manager_，只需置空指针。单例的生命周期由框架管理。

#### 避免 Static 局部变量问题

**不要**在插件代码中使用 static 局部变量存储 io_context 等资源：

```cpp
// 错误示例 - static变量在reload后不会重新初始化
if (config_.enable_retry_queue) {
    static boost::asio::io_context retry_io_context;  // 危险！
    retry_manager_ = std::make_shared<RetryQueueManager>(db_manager_, retry_io_context);
}
```

这种模式会导致reload后资源状态不一致。

