/**
 * @file test_websocket_queue.cpp
 * @brief 测试WebSocket写入队列机制
 *
 * 本测试验证在弱网环境下，WebSocket客户端的写入队列机制
 * 能够正确处理并发写入请求，避免Beast内部的竞争问题。
 */

#include "common/Logger.hpp"
#include "network/WebsocketClient.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <iostream>
#include <thread>

using namespace obcx::network;
using namespace std::chrono_literals;

// 模拟弱网环境的WebSocket服务器
class MockWebSocketServer {
public:
  MockWebSocketServer(asio::io_context &ioc, uint16_t port)
      : acceptor_(ioc, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {
    start_accept();
  }

private:
  void start_accept() {
    auto socket =
        std::make_shared<asio::ip::tcp::socket>(acceptor_.get_executor());
    acceptor_.async_accept(*socket,
                           [this, socket](boost::system::error_code ec) {
                             if (!ec) {
                               handle_connection(socket);
                             }
                             start_accept();
                           });
  }

  void handle_connection(const std::shared_ptr<asio::ip::tcp::socket> &socket) {
    // 模拟弱网环境：快速接收，缓慢发送
    auto buffer = std::make_shared<std::vector<char>>(1024);
    socket->async_read_some(
        asio::buffer(*buffer),
        [this, socket, buffer](boost::system::error_code ec,
                               std::size_t bytes_transferred) {
          if (!ec) {
            // 模拟网络延迟
            auto timer =
                std::make_shared<asio::steady_timer>(socket->get_executor());
            timer->expires_after(100ms); // 100ms延迟模拟弱网
            timer->async_wait([socket, buffer,
                               bytes_transferred](boost::system::error_code) {
              // 发送响应
              asio::async_write(*socket, asio::buffer("OK", 2), asio::detached);
            });
          }
          handle_connection(socket);
        });
  }

  asio::ip::tcp::acceptor acceptor_;
};

// 测试并发写入
auto test_concurrent_writes(WebsocketClient &client) -> asio::awaitable<void> {
  OBCX_INFO("开始测试并发写入...");

  // 创建多个并发写入任务
  std::vector<std::shared_ptr<asio::steady_timer>> timers;

  for (int i = 0; i < 10; ++i) {
    auto timer = std::make_shared<asio::steady_timer>(client.get_executor());
    timers.push_back(timer);

    // 启动异步任务
    asio::co_spawn(
        client.get_executor(),
        [&client, i]() -> asio::awaitable<void> {
          std::string message = "消息 " + std::to_string(i);
          OBCX_INFO("发送消息: {}", message);

          try {
            co_await client.send(message);
            OBCX_INFO("消息 {} 发送成功", i);
          } catch (const std::exception &e) {
            OBCX_ERROR("消息 {} 发送失败: {}", i, e.what());
          }
        },
        asio::detached);
  }

  // 等待所有任务完成（给一些时间）
  co_await asio::steady_timer(co_await asio::this_coro::executor, 2s)
      .async_wait(asio::use_awaitable);

  OBCX_INFO("所有并发写入测试完成");
}

// 测试弱网环境下的写入
auto test_weak_network_writes(WebsocketClient &client)
    -> asio::awaitable<void> {
  OBCX_INFO("开始测试弱网环境下的写入...");

  // 模拟弱网环境：连续快速发送
  for (int i = 0; i < 20; ++i) {
    std::string message = "弱网测试消息 " + std::to_string(i);
    OBCX_INFO("发送弱网测试消息: {}", message);

    try {
      co_await client.send(message);
      OBCX_INFO("弱网测试消息 {} 发送成功", i);

      // 短暂等待，模拟业务逻辑
      co_await asio::steady_timer(co_await asio::this_coro::executor, 10ms)
          .async_wait(asio::use_awaitable);
    } catch (const std::exception &e) {
      OBCX_ERROR("弱网测试消息 {} 发送失败: {}", i, e.what());
    }
  }

  OBCX_INFO("弱网环境写入测试完成");
}

auto main() -> int {
  try {
    asio::io_context ioc;

    // 启动模拟服务器
    MockWebSocketServer server(ioc, 8080);
    OBCX_INFO("模拟WebSocket服务器已启动在端口 8080");

    // 创建WebSocket客户端
    auto client = std::make_shared<WebsocketClient>(ioc);

    // 启动连接
    asio::co_spawn(
        ioc,
        [client]() -> asio::awaitable<void> {
          try {
            co_await client->run(
                "localhost", "8080", "",
                [](const beast::error_code &ec, const std::string &msg) {
                  if (ec) {
                    OBCX_ERROR("WebSocket错误: {}", ec.message());
                  } else if (!msg.empty()) {
                    OBCX_INFO("收到消息: {}", msg);
                  }
                });
          } catch (const std::exception &e) {
            OBCX_ERROR("WebSocket运行错误: {}", e.what());
          }
        },
        asio::detached);

    // 等待连接建立
    std::this_thread::sleep_for(500ms);

    // 运行测试
    asio::co_spawn(
        ioc,
        [client]() -> asio::awaitable<void> {
          // 等待连接完全建立
          co_await asio::steady_timer(co_await asio::this_coro::executor, 1s)
              .async_wait(asio::use_awaitable);

          // 测试并发写入
          co_await test_concurrent_writes(*client);

          // 测试弱网环境写入
          co_await test_weak_network_writes(*client);

          // 关闭连接
          co_await client->close();

          OBCX_INFO("所有测试完成");
        },
        asio::detached);

    // 运行IO循环
    ioc.run();

  } catch (const std::exception &e) {
    OBCX_CRITICAL("测试程序异常: {}", e.what());
    return 1;
  }

  return 0;
}
