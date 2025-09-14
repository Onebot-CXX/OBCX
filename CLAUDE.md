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

- 如果需要生成构建项目，执行以下命令 `cmake -B <Your build dir> -DCMAKE_TOOLCHAIN_FILE=/Users/vollate/vcpkg/scripts/buildsystems/vcpkg.cmake -GNinja`
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