/**
 * @file telegram_bot_example.cpp
 * @brief Telegram Bot 示例
 *
 * 本示例展示了如何使用 TelegramBot 类
 */

#include "common/Logger.hpp"
#include "common/MessageType.hpp"
#include "core/TGBot.hpp"
#include "interface/IBot.hpp"

#include <boost/asio/steady_timer.hpp>
#include <fmt/format.h>
#include <iostream>
#include <memory>

using namespace obcx;
namespace asio = boost::asio;

// 统一的消息处理函数，处理群消息和私聊消息
auto message_handler(core::IBot &bot, common::MessageEvent event)
    -> asio::awaitable<void> {
  // 检查是否为群消息
  if (event.group_id.has_value() && event.message_type == "group") {
    // 打印群消息到控制台
    OBCX_INFO("=== Telegram群消息 ===");
    OBCX_INFO("群组ID: {}", event.group_id.value());
    OBCX_INFO("发送者ID: {}", event.user_id);
    OBCX_INFO("消息ID: {}", event.message_id);
    OBCX_INFO("消息内容: {}", event.raw_message);
    OBCX_INFO("==================");
  }
  // 检查是否为私聊消息
  else if (!event.group_id.has_value() && event.message_type == "private") {
    // 打印私聊消息到控制台
    OBCX_INFO("=== Telegram私聊消息 ===");
    OBCX_INFO("用户ID: {}", event.user_id);
    OBCX_INFO("消息ID: {}", event.message_id);
    OBCX_INFO("消息内容: {}", event.raw_message);
    OBCX_INFO("====================");
  }

  // 原有的处理逻辑
  std::string group_info =
      event.group_id.has_value()
          ? fmt::format(" in group {}", event.group_id.value())
          : "";

  OBCX_INFO("Received message from user {} (UID: {}){}: {}", event.user_id,
            event.user_id, group_info, event.raw_message);

  // 回复消息
  common::Message reply = {
      {{.type = {"text"},
        .data = {{"text", fmt::format("Hello! I received your message: {}",
                                      event.raw_message)}}}}};

  if (event.group_id.has_value()) {
    OBCX_INFO("Sending group message to group {}", event.group_id.value());
    co_await bot.send_group_message(event.group_id.value(), reply);
  } else {
    OBCX_INFO("Sending private message to user {}", event.user_id);
    co_await bot.send_private_message(event.user_id, reply);
  }
}

// 定时任务：每5秒向指定用户发送一条"hello"消息
auto send_periodic_message(core::IBot &bot, std::string user_id)
    -> asio::awaitable<void> {
  while (true) {
    try {
      // 发送消息
      common::Message message = {
          {{.type = {"text"},
            .data = {{"text", "Hello from OBCX Telegram Bot!"}}}}};

      OBCX_INFO("Sending periodic message to user {}", user_id);
      co_await bot.send_private_message(user_id, message);

      // 等待5秒
      asio::steady_timer timer(bot.get_task_scheduler().get_io_context(),
                               std::chrono::seconds(5));
      co_await timer.async_wait(asio::use_awaitable);
    } catch (const std::exception &e) {
      OBCX_ERROR("Error in periodic message task: {}", e.what());
      // 等待5秒再重试
      asio::steady_timer timer(bot.get_task_scheduler().get_io_context(),
                               std::chrono::seconds(5));
      // 使用一个简单的等待，避免在catch块中使用co_await
      std::this_thread::sleep_for(std::chrono::seconds(5));
    }
  }
}

auto main() -> int {
  common::Logger::initialize(spdlog::level::debug);

  try {
    std::unique_ptr<core::IBot> bot =
        std::make_unique<core::TGBot>(adapter::telegram::ProtocolAdapter{});

    // 注册消息事件处理器
    bot->on_event<common::MessageEvent>(message_handler);

    std::string host = "api.telegram.org";
    uint16_t port = 443;
    std::string access_token = "sending_to_the_eagle"; // 替换为实际的 bot
                                                       // token

    OBCX_INFO("正在连接到 Telegram Bot API: {}:{}/bot{}", host, port,
              access_token);

    // 设置连接配置，包括HTTP代理（如果需要）
    common::ConnectionConfig config;
    config.host = host;
    config.port = port;
    config.access_token = access_token;

    // 如果需要设置代理，可以取消下面的注释并设置代理信息
    //
    // SOCKS5代理 (推荐，适用于大多数代理如singbox)
    // config.proxy_host = "127.0.0.1";
    // config.proxy_port = 20123;  // SOCKS5端口
    // config.proxy_type = "socks5";
    //
    // HTTP代理 (适用于mixport或HTTP代理)
    // config.proxy_host = "127.0.0.1";
    // config.proxy_port = 20122;  // HTTP/mixport端口
    // config.proxy_type = "http";
    //
    // HTTPS代理 (用于需要加密连接到代理服务器的场景)
    // config.proxy_host = "proxy.example.com";
    // config.proxy_port = 443;
    // config.proxy_type = "https";
    //
    // 代理认证（如果需要）
    // config.proxy_username = "username";
    // config.proxy_password = "password";

    bot->connect(
        network::ConnectionManagerFactory::ConnectionType::TelegramHTTP,
        config);

    // 启动定时任务，每5秒向用户5212106225发送一条hello消息
    // asio::co_spawn(
    //     bot->get_task_scheduler().get_io_context(),
    //     send_periodic_message(*bot, "5212106225"),
    //     asio::detached);

    bot->run();

  } catch (const std::exception &e) {
    OBCX_ERROR("程序异常: {}", e.what());
    return 1;
  }

  OBCX_INFO("程序正常退出");
  return 0;
}
