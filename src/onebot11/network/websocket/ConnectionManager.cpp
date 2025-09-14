#include "onebot11/network/websocket/ConnectionManager.hpp"
#include "common/Logger.hpp"
#include "onebot11/adapter/ProtocolAdapter.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/bind/bind.hpp>
#include <utility>

namespace obcx::network {

WebSocketConnectionManager::WebSocketConnectionManager(
    asio::io_context &ioc, adapter::onebot11::ProtocolAdapter &adapter)
    : ioc_(ioc), adapter_(adapter), reconnect_timer_(ioc),
      send_strand_(asio::make_strand(ioc)), port_(0) {}

void WebSocketConnectionManager::set_event_callback(EventCallback callback) {
  event_callback_ = std::move(callback);
}

void WebSocketConnectionManager::connect(
    const common::ConnectionConfig &config) {
  connect_ws(config.host, config.port, config.access_token);
}

void WebSocketConnectionManager::disconnect() {
  is_running_ = false;
  if (ws_client_) {
    asio::co_spawn(ioc_, ws_client_->close(), asio::detached);
  }
}

auto WebSocketConnectionManager::get_connection_type() const -> std::string {
  return "WebSocket";
}

void WebSocketConnectionManager::connect_ws(std::string host, uint16_t port,
                                            std::string access_token) {
  if (is_running_) {
    OBCX_WARN("ConnectionManager 已经有一个连接正在运行。");
    return;
  }
  host_ = std::move(host);
  port_ = port;
  access_token_ = std::move(access_token);
  is_running_ = true;

  do_connect();
}

void WebSocketConnectionManager::do_connect() {
  asio::post(send_strand_, [this]() {
    ws_client_ = std::make_shared<WebsocketClient>(ioc_);
    OBCX_INFO("正在尝试连接到 ws://{}:{}", host_, port_);

    asio::co_spawn(send_strand_,
                   ws_client_->run(host_, std::to_string(port_), access_token_,
                                   [this](const beast::error_code &ec,
                                          const std::string &message) {
                                     this->on_ws_message(ec, message);
                                   }),
                   asio::detached);
  });
}

void WebSocketConnectionManager::on_ws_message(const beast::error_code &ec,
                                               const std::string &message) {
  OBCX_TRACE("Receive ws server message: {}", message);
  if (ec) {
    // 连接已断开或发生错误
    OBCX_ERROR("连接断开，错误: {}", ec.message());
    {
      is_connected_.store(false, std::memory_order_release);
    }
    schedule_reconnect();
    return;
  }

  if (message.empty()) {
    OBCX_INFO("WebSocket 连接已建立");
    {
      is_connected_.store(true, std::memory_order_release);
    }
    reconnect_timer_.cancel();
    return;
  }

  OBCX_DEBUG("收到原始消息: {}", message);

  try {
    nlohmann::json j = nlohmann::json::parse(message);

    if (j.contains("echo") && j.contains("retcode")) {
      uint64_t echo = j["echo"];

      std::lock_guard lock(pending_requests_mutex_);
      OBCX_DEBUG("查找pending request，echo: {}, 总数: {}", echo,
                 pending_requests_.size());
      auto it = pending_requests_.find(echo);
      if (it != pending_requests_.end()) {
        // 找到对应的请求，调用resolver回调函数
        if (it->second->resolver) {
          OBCX_DEBUG("调用resolver，echo: {}", echo);
          it->second->resolver(message);
        } else {
          OBCX_ERROR("Resolver为空！echo: {}", echo);
        }
        OBCX_DEBUG("已处理API响应，echo: {}", echo);
        return;
      } else {
        OBCX_WARN("收到未知的API响应，echo: {}", echo);
        // 打印所有pending requests的echo IDs用于调试
        std::string pending_echos;
        for (const auto &[id, req] : pending_requests_) {
          if (!pending_echos.empty())
            pending_echos += ", ";
          pending_echos += std::to_string(id);
        }
        OBCX_DEBUG("当前pending requests: [{}]", pending_echos);
      }
    }
  } catch (const nlohmann::json::exception &e) {
    OBCX_WARN("JSON解析失败: {}", e.what());
  }

  auto event_opt = adapter_.parse_event(message);
  if (event_opt) {
    if (event_callback_) {
      event_callback_(event_opt.value());
    }
  } else {
    OBCX_DEBUG("收到的消息不是一个有效事件: {}", message);
  }
}

void WebSocketConnectionManager::schedule_reconnect() {
  reconnect_timer_.expires_after(std::chrono::seconds(5));
  OBCX_INFO("将在5秒后尝试重新连接...");
  reconnect_timer_.async_wait([this](const beast::error_code &ec) {
    if (ec) {
      if (ec != asio::error::operation_aborted) {
        OBCX_ERROR("重连计时器错误: {}", ec.message());
      }
      return;
    }
    do_connect();
  });
}

auto WebSocketConnectionManager::send_action_and_wait_async(
    std::string action_payload, uint64_t echo_id)
    -> asio::awaitable<std::string> {
  if (!ws_client_) {
    throw std::runtime_error("没有可用的 WebSocket 客户端");
  }

  std::string result;
  bool response_received = false;
  std::exception_ptr error_ptr = nullptr;

  auto request = std::make_shared<PendingRequest>(ioc_);

  // 创建定时器
  asio::steady_timer response_timer(ioc_);
  asio::steady_timer timeout_timer(ioc_);

  response_timer.expires_at(std::chrono::steady_clock::time_point::max());
  timeout_timer.expires_after(std::chrono::seconds(30));

  // 设置resolver - 必须在添加到pending_requests之前设置
  request->resolver = [&, echo_id](std::string response) {
    OBCX_DEBUG("Resolver调用，echo: {}, response: {}", echo_id, response);
    result = std::move(response);
    response_received = true;
    timeout_timer.cancel();
    response_timer.cancel();
    OBCX_DEBUG("Timer已取消，协程应该立即唤醒，echo: {}", echo_id);
  };
  OBCX_DEBUG("Resolver已设置，echo: {}", echo_id);

  // 现在添加到pending requests - resolver已经设置好了
  {
    std::lock_guard lock(pending_requests_mutex_);
    pending_requests_[echo_id] = request;
    OBCX_DEBUG("添加pending request，echo: {}, 总数: {}", echo_id,
               pending_requests_.size());
  }

  try {
    // 发送WebSocket消息 - 现在resolver已经准备好处理快速响应了
    co_await asio::co_spawn(
        send_strand_,
        [this, action_payload]() -> asio::awaitable<void> {
          try {
            co_await ws_client_->send(action_payload);
          } catch (...) {
            throw;
          }
        },
        asio::use_awaitable);

    request->rejecter = [&](std::exception_ptr ex) {
      error_ptr = std::move(ex);
      timeout_timer.cancel();
      response_timer.cancel();
    };

    auto timeout_task = [&]() -> asio::awaitable<void> {
      try {
        co_await timeout_timer.async_wait(asio::use_awaitable);
        response_timer.cancel();
      } catch (const boost::system::system_error &e) {
        if (e.code() == asio::error::operation_aborted) {
        } else {
          throw;
        }
      }
    };

    // 并发启动超时任务
    asio::co_spawn(ioc_, timeout_task(), asio::detached);

    // 等待响应timer被取消（无论是响应到达还是超时）
    OBCX_DEBUG("开始等待响应，echo: {}", echo_id);

    // 使用一个循环来轮询，避免调度器延迟
    while (!response_received && !error_ptr) {
      try {
        // 设置短超时以避免阻塞太久
        auto short_timeout = std::chrono::milliseconds(100);
        asio::steady_timer short_timer(ioc_);
        short_timer.expires_after(short_timeout);

        co_await short_timer.async_wait(asio::use_awaitable);
        OBCX_DEBUG("轮询检查，echo: {}, response_received: {}", echo_id,
                   response_received);

        // 检查是否超时
        if (!response_received &&
            std::chrono::steady_clock::now() > timeout_timer.expiry()) {
          OBCX_DEBUG("检测到超时，echo: {}", echo_id);
          break;
        }
      } catch (const boost::system::system_error &e) {
        if (e.code() != asio::error::operation_aborted) {
          OBCX_ERROR("轮询等待异常，echo: {}, error: {}", echo_id, e.what());
          throw;
        }
      }
    }

    if (response_received) {
      OBCX_DEBUG("响应已收到，echo: {}", echo_id);
    } else {
      OBCX_DEBUG("响应等待结束（超时或错误），echo: {}", echo_id);
    }

    // 确保取消所有timer
    timeout_timer.cancel();
    response_timer.cancel();

    // 清理请求
    {
      std::lock_guard lock(pending_requests_mutex_);
      pending_requests_.erase(echo_id);
      OBCX_DEBUG("清理pending request，echo: {}, 剩余总数: {}", echo_id,
                 pending_requests_.size());
    }

    // 检查结果
    if (error_ptr) {
      std::rethrow_exception(error_ptr);
    }

    if (!response_received) {
      // 如果没有收到响应，说明是超时了
      OBCX_ERROR("API请求超时，echo: {}, response_received: {}", echo_id,
                 response_received);
      throw std::runtime_error("API请求超时");
    }

    OBCX_DEBUG("API请求成功完成，echo: {}, result length: {}", echo_id,
               result.length());
    co_return result;

  } catch (...) {
    // 清理
    {
      std::lock_guard lock(pending_requests_mutex_);
      pending_requests_.erase(echo_id);
    }
    throw;
  }
}

auto WebSocketConnectionManager::is_connected() const -> bool {
  return is_connected_.load(std::memory_order_acquire);
}

} // namespace obcx::network