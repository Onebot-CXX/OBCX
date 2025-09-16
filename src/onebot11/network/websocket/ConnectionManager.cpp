#include "onebot11/network/websocket/ConnectionManager.hpp"
#include "common/Logger.hpp"
#include "onebot11/adapter/ProtocolAdapter.hpp"

#include <atomic>
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
  is_connected_.store(false, std::memory_order_release);

  // 清理所有pending请求，避免析构时访问已销毁的对象
  {
    std::lock_guard lock(pending_requests_mutex_);
    for (auto &[echo_id, request] : pending_requests_) {
      if (request) {
        // 取消所有timer
        request->timeout_timer.cancel();
        // 如果有rejecter，调用它来清理
        if (request->rejecter) {
          try {
            request->rejecter(std::make_exception_ptr(
                std::runtime_error("Connection disconnected")));
          } catch (...) {
            // 忽略rejecter中的异常
          }
        }
      }
    }
    pending_requests_.clear();
    OBCX_DEBUG("已清理所有pending requests，总数: 0");
  }

  // 关闭WebSocket连接
  if (ws_client_) {
    asio::co_spawn(ioc_, ws_client_->close(), asio::detached);
  }

  // 取消重连timer
  reconnect_timer_.cancel();
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

  // 编译时选择超时机制类型
  // true: 事件驱动超时 - 性能更好，响应更快，不轮询
  // false: 轮询等待超时 - 更稳定，但会定期唤醒协程检查状态
  constexpr bool USE_EVENT_DRIVEN_TIMEOUT = false;

  if constexpr (USE_EVENT_DRIVEN_TIMEOUT) {
    // === 事件驱动超时机制 ===

    // 使用shared_ptr来管理状态，确保生命周期正确
    struct RequestState {
      std::string result;
      std::exception_ptr error_ptr = nullptr;
      bool is_timeout = false;
      std::mutex state_mutex;
    };
    std::atomic_bool skip_wait{false};
    auto state = std::make_shared<RequestState>();
    auto request = std::make_shared<PendingRequest>(ioc_);

    // 设置30秒超时
    request->timeout_timer.expires_after(std::chrono::seconds(30));

    // 使用信号量来实现事件驱动的等待
    auto completion_semaphore = std::make_shared<asio::steady_timer>(ioc_);
    completion_semaphore->expires_at(
        std::chrono::steady_clock::time_point::max());

    // 设置resolver - 纯事件驱动，完成时唤醒协程
    request->resolver = [state, echo_id, completion_semaphore,
                         &skip_wait](std::string response) {
      OBCX_DEBUG("Resolver调用，echo: {}, response: {}", echo_id, response);
      {
        std::lock_guard<std::mutex> lock(state->state_mutex);
        state->result = std::move(response);
      }
      if (0 == completion_semaphore->cancel()) {
        skip_wait.store(true, std::memory_order_release);
      }
      OBCX_DEBUG("响应已设置并唤醒协程，echo: {}", echo_id);
    };

    request->rejecter = [state, completion_semaphore,
                         &skip_wait](std::exception_ptr ex) {
      {
        std::lock_guard<std::mutex> lock(state->state_mutex);
        state->error_ptr = std::move(ex);
      }
      if (0 == completion_semaphore->cancel()) {
        skip_wait.store(true, std::memory_order_release);
      }
    };

    OBCX_DEBUG("Event-driven resolver已设置，echo: {}", echo_id);

    // 添加到pending requests
    {
      std::lock_guard lock(pending_requests_mutex_);
      pending_requests_[echo_id] = request;
      OBCX_DEBUG("添加pending request，echo: {}, 总数: {}", echo_id,
                 pending_requests_.size());
    }

    try {
      // 发送WebSocket消息
      co_await asio::co_spawn(
          send_strand_,
          [this, action_payload]() -> asio::awaitable<void> {
            co_await ws_client_->send(action_payload);
          },
          asio::use_awaitable);

      OBCX_DEBUG("WebSocket消息已发送，echo: {}", echo_id);

      // 启动超时检查 - 超时时唤醒协程
      asio::co_spawn(
          ioc_,
          [request, state, echo_id, &skip_wait,
           completion_semaphore]() -> asio::awaitable<void> {
            try {
              co_await request->timeout_timer.async_wait(asio::use_awaitable);
              // 超时发生
              OBCX_DEBUG("检测到超时，echo: {}", echo_id);
              {
                std::lock_guard<std::mutex> lock(state->state_mutex);
                state->is_timeout = true;
              }
              if (0 == completion_semaphore->cancel()) {
                skip_wait.store(true, std::memory_order_release);
              }
            } catch (const boost::system::system_error &e) {
              if (e.code() != asio::error::operation_aborted) {
                OBCX_ERROR("超时任务异常，echo: {}, error: {}", echo_id,
                           e.what());
              }
            }
          },
          asio::detached);

      OBCX_DEBUG("开始事件驱动等待响应，echo: {}", echo_id);

      // 等待completion_semaphore被取消（响应到达或超时）
      if (!skip_wait.load(std::memory_order_acquire)) {
        try {
          co_await completion_semaphore->async_wait(asio::use_awaitable);
          OBCX_ERROR("Completion semaphore正常到期，这不应该发生，echo: {}",
                     echo_id);
        } catch (const boost::system::system_error &e) {
          if (e.code() == asio::error::operation_aborted) {
            OBCX_DEBUG("事件驱动等待完成，echo: {}", echo_id);
          } else {
            throw;
          }
        }
      }

      request->timeout_timer.cancel();

      {
        std::lock_guard lock(pending_requests_mutex_);
        pending_requests_.erase(echo_id);
        OBCX_DEBUG("清理pending request，echo: {}, 剩余总数: {}", echo_id,
                   pending_requests_.size());
      }

      {
        std::lock_guard<std::mutex> lock(state->state_mutex);
        if (state->error_ptr) {
          std::rethrow_exception(state->error_ptr);
        }

        if (state->is_timeout || state->result.empty()) {
          OBCX_ERROR("API请求超时，echo: {}", echo_id);
          throw std::runtime_error("API请求超时");
        }

        OBCX_DEBUG("事件驱动API请求成功完成，echo: {}, result length: {}",
                   echo_id, state->result.length());
        co_return state->result;
      }

    } catch (...) {
      request->timeout_timer.cancel();
      {
        std::lock_guard lock(pending_requests_mutex_);
        pending_requests_.erase(echo_id);
      }
      throw;
    }

  } else {
    // === 轮询等待超时机制 ===

    // 使用shared_ptr来管理状态，确保生命周期正确
    struct RequestState {
      std::string result;
      std::atomic<bool> response_received{false};
      std::exception_ptr error_ptr = nullptr;
      std::mutex state_mutex;
    };

    auto state = std::make_shared<RequestState>();
    auto request = std::make_shared<PendingRequest>(ioc_);

    // 设置30秒超时
    request->timeout_timer.expires_after(std::chrono::seconds(30));

    // 设置resolver - 使用shared_ptr确保安全访问
    request->resolver = [state, echo_id](std::string response) {
      OBCX_DEBUG("Resolver调用，echo: {}, response: {}", echo_id, response);
      std::lock_guard<std::mutex> lock(state->state_mutex);
      state->result = std::move(response);
      state->response_received.store(true, std::memory_order_release);
      OBCX_DEBUG("响应已设置，echo: {}", echo_id);
    };

    request->rejecter = [state](std::exception_ptr ex) {
      std::lock_guard<std::mutex> lock(state->state_mutex);
      state->error_ptr = std::move(ex);
      state->response_received.store(true, std::memory_order_release);
    };

    OBCX_DEBUG("Polling resolver已设置，echo: {}", echo_id);

    // 添加到pending requests
    {
      std::lock_guard lock(pending_requests_mutex_);
      pending_requests_[echo_id] = request;
      OBCX_DEBUG("添加pending request，echo: {}, 总数: {}", echo_id,
                 pending_requests_.size());
    }

    try {
      // 发送WebSocket消息
      co_await asio::co_spawn(
          send_strand_,
          [this, action_payload]() -> asio::awaitable<void> {
            co_await ws_client_->send(action_payload);
          },
          asio::use_awaitable);

      OBCX_DEBUG("WebSocket消息已发送，echo: {}", echo_id);

      // 启动超时检查
      asio::co_spawn(
          ioc_,
          [request, state, echo_id]() -> asio::awaitable<void> {
            try {
              co_await request->timeout_timer.async_wait(asio::use_awaitable);
              // 超时发生
              OBCX_DEBUG("检测到超时，echo: {}", echo_id);
              std::lock_guard<std::mutex> lock(state->state_mutex);
              if (!state->response_received.load(std::memory_order_acquire)) {
                state->response_received.store(true, std::memory_order_release);
              }
            } catch (const boost::system::system_error &e) {
              if (e.code() != asio::error::operation_aborted) {
                OBCX_ERROR("超时任务异常，echo: {}, error: {}", echo_id,
                           e.what());
              }
            }
          },
          asio::detached);

      // 轮询等待响应或超时
      OBCX_DEBUG("开始轮询等待响应，echo: {}", echo_id);

      while (!state->response_received.load(std::memory_order_acquire)) {
        // 使用短暂的sleep避免busy waiting
        asio::steady_timer wait_timer(ioc_);
        wait_timer.expires_after(std::chrono::milliseconds(10));

        try {
          co_await wait_timer.async_wait(asio::use_awaitable);
        } catch (const boost::system::system_error &e) {
          if (e.code() != asio::error::operation_aborted) {
            throw;
          }
        }
      }

      // 取消超时timer
      request->timeout_timer.cancel();

      // 清理请求
      {
        std::lock_guard lock(pending_requests_mutex_);
        pending_requests_.erase(echo_id);
        OBCX_DEBUG("清理pending request，echo: {}, 剩余总数: {}", echo_id,
                   pending_requests_.size());
      }

      // 检查结果
      {
        std::lock_guard<std::mutex> lock(state->state_mutex);
        if (state->error_ptr) {
          std::rethrow_exception(state->error_ptr);
        }

        if (state->result.empty()) {
          OBCX_ERROR("API请求超时，echo: {}", echo_id);
          throw std::runtime_error("API请求超时");
        }

        OBCX_DEBUG("轮询API请求成功完成，echo: {}, result length: {}", echo_id,
                   state->result.length());
        co_return state->result;
      }

    } catch (...) {
      // 清理
      request->timeout_timer.cancel();
      {
        std::lock_guard lock(pending_requests_mutex_);
        pending_requests_.erase(echo_id);
      }
      throw;
    }
  }
}

auto WebSocketConnectionManager::is_connected() const -> bool {
  return is_connected_.load(std::memory_order_acquire);
}

} // namespace obcx::network
