#include "onebot11/network/websocket/connection_manager.hpp"
#include "common/logger.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"

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
  action_timeout_ = config.connect_timeout;
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
                std::runtime_error(common::I18nLogMessages::get_message(
                    common::LogMessageKey::
                        ONEBOT11_WS_CONNECTION_DISCONNECTED_EXCEPTION))));
          } catch (...) {
            // 忽略rejecter中的异常
            // FIXME: Log the exception
          }
        }
      }
    }
    pending_requests_.clear();
    OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_PENDING_CLEARED);
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
    OBCX_I18N_WARN(common::LogMessageKey::ONEBOT11_WS_ALREADY_RUNNING);
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
    OBCX_I18N_INFO(common::LogMessageKey::WEBSOCKET_ATTEMPTING_CONNECTION,
                   host_, port_);

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
  OBCX_I18N_TRACE(common::LogMessageKey::ONEBOT11_WS_MSG_RECEIVED, message);
  if (ec) {
    // 连接已断开或发生错误
    OBCX_I18N_ERROR(common::LogMessageKey::ONEBOT11_WS_DISCONNECTED_ERROR,
                    ec.message());
    {
      is_connected_.store(false, std::memory_order_release);
    }
    schedule_reconnect();
    return;
  }

  if (message.empty()) {
    OBCX_I18N_INFO(common::LogMessageKey::WEBSOCKET_CONNECTION_ESTABLISHED);
    {
      is_connected_.store(true, std::memory_order_release);
    }
    reconnect_timer_.cancel();
    return;
  }

  OBCX_I18N_TRACE(common::LogMessageKey::ONEBOT11_WS_RAW_MSG, message);

  try {
    nlohmann::json j = nlohmann::json::parse(message);

    if (j.contains("echo") && j.contains("retcode")) {
      uint64_t echo = j["echo"];

      std::lock_guard lock(pending_requests_mutex_);
      OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_FIND_PENDING, echo,
                      pending_requests_.size());
      auto it = pending_requests_.find(echo);
      if (it != pending_requests_.end()) {
        auto request = it->second;
        pending_requests_.erase(it); // 立即移除

        // 取消超时定时器
        request->need_wait.store(false, std::memory_order_release);
        request->timeout_timer.cancel();

        if constexpr (USE_COROUTINE_ASYNC_WAIT) {
          // 协程模式：调用 completion handler
          if (request->completion_handler) {
            OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_CALL_COMPLETION,
                            echo);
            request->completion_handler(boost::system::error_code{}, message);
          } else {
            OBCX_I18N_ERROR(common::LogMessageKey::ONEBOT11_WS_COMPLETION_NULL,
                            echo);
          }
        } else {
          // 轮询模式：调用 resolver
          if (request->resolver) {
            OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_CALL_RESOLVER,
                            echo);
            request->resolver(message);
          } else {
            OBCX_I18N_ERROR(common::LogMessageKey::ONEBOT11_WS_RESOLVER_NULL,
                            echo);
          }
        }
        OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_API_RESPONSE_HANDLED,
                        echo);
        return;
      }
      OBCX_I18N_WARN(common::LogMessageKey::ONEBOT11_WS_UNKNOWN_API_RESPONSE,
                     echo);
      // 打印所有pending requests的echo IDs用于调试
#ifdef __OBCX_DEBUG_COMPILATION
      std::stringstream pending_echos;
      bool first = true;
      for (const auto &[id, req] : pending_requests_) {
        if (!first) {
          pending_echos << ", ";
        }
        first = false;
        pending_echos << id;
      }
      OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_CURRENT_PENDING,
                      pending_echos.str());
#endif
    }
  } catch (const nlohmann::json::exception &e) {
    OBCX_I18N_WARN(common::LogMessageKey::ONEBOT11_WS_JSON_PARSE_FAILED,
                   e.what());
  }

  auto event_opt = adapter_.parse_event(message);
  if (event_opt) {
    if (event_callback_) {
      event_callback_(event_opt.value());
    }
  } else {
    OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_INVALID_EVENT);
  }
}

void WebSocketConnectionManager::schedule_reconnect() {
  reconnect_timer_.expires_after(std::chrono::seconds(5));
  OBCX_I18N_INFO(common::LogMessageKey::ONEBOT11_WS_RECONNECT_SCHEDULED, 5000);
  reconnect_timer_.async_wait([this](const beast::error_code &ec) {
    if (ec) {
      if (ec != asio::error::operation_aborted) {
        OBCX_I18N_ERROR(
            common::LogMessageKey::ONEBOT11_WS_RECONNECT_TIMER_ERROR,
            ec.message());
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
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::ONEBOT11_WS_NO_CLIENT));
  }

  // if constexpr (USE_COROUTINE_ASYNC_WAIT) {

  OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_USE_COROUTINE);

  std::optional<std::string> response_result;
  std::optional<boost::system::error_code> response_error;
  std::mutex result_mutex;

  auto request = std::make_shared<PendingRequest>(ioc_);

  request->completion_handler =
      [&result_mutex, &response_result, &response_error,
       request](boost::system::error_code ec, std::string response) {
        std::lock_guard lock(result_mutex);
        if (ec) {
          response_error = ec;
        } else {
          response_result = std::move(response);
        }
        request->timeout_timer.cancel();
      };

  request->timeout_timer.expires_after(action_timeout_);

  {
    std::lock_guard lock(pending_requests_mutex_);
    pending_requests_[echo_id] = request;
    OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_ADD_PENDING_COROUTINE,
                    echo_id, pending_requests_.size());
  }

  try {
    co_await asio::co_spawn(
        send_strand_,
        [this, action_payload =
                   std::move(action_payload)]() -> asio::awaitable<void> {
          co_await ws_client_->send(action_payload);
        },
        asio::use_awaitable);

    OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_MSG_SENT_COROUTINE,
                    echo_id);

    if (request->need_wait.load(std::memory_order_acquire)) {
      try {
        co_await request->timeout_timer.async_wait(asio::use_awaitable);
        // 如果走到这里，说明真的超时了
        OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_REQUEST_TIMEOUT,
                        echo_id);
        response_error = asio::error::timed_out;
      } catch (const boost::system::system_error &e) {
        if (e.code() == asio::error::operation_aborted) {
          // timer 被取消，说明收到了响应
          OBCX_I18N_DEBUG(
              common::LogMessageKey::ONEBOT11_WS_RESPONSE_RECEIVED_TIMER,
              echo_id);
        } else {
          throw;
        }
      }
    }

    // 清理请求
    {
      std::lock_guard lock(pending_requests_mutex_);
      pending_requests_.erase(echo_id);
      OBCX_I18N_DEBUG(
          common::LogMessageKey::ONEBOT11_WS_CLEAN_PENDING_COROUTINE, echo_id,
          pending_requests_.size());
    }

    // 检查结果
    {
      std::lock_guard lock(result_mutex);
      if (response_error) {
        if (response_error == asio::error::timed_out) {
          OBCX_I18N_ERROR(
              common::LogMessageKey::ONEBOT11_WS_API_TIMEOUT_COROUTINE,
              echo_id);
          throw std::runtime_error(common::I18nLogMessages::get_message(
              common::LogMessageKey::ONEBOT11_WS_API_TIMEOUT_MSG));
        }
        throw boost::system::system_error(*response_error);
      }

      if (response_result) {
        OBCX_I18N_DEBUG(
            common::LogMessageKey::ONEBOT11_WS_API_SUCCESS_COROUTINE, echo_id,
            response_result->length());
        co_return *response_result;
      }

      throw std::runtime_error(common::I18nLogMessages::get_message(
          common::LogMessageKey::ONEBOT11_WS_UNKNOWN_ERROR));
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

  //} else {
  //// ============ 轮询等待模式（原有逻辑）============
  // OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_USE_POLLING);

  //// 使用shared_ptr来管理状态，确保生命周期正确
  // struct RequestState {
  // std::string result;
  // std::atomic<bool> response_received{false};
  // std::exception_ptr error_ptr = nullptr;
  // std::mutex state_mutex;
  //};

  // auto state = std::make_shared<RequestState>();
  // auto request = std::make_shared<PendingRequest>(ioc_);

  //// 设置30秒超时
  // request->timeout_timer.expires_after(std::chrono::seconds(30));

  //// 设置resolver - 使用shared_ptr确保安全访问
  // request->resolver = [state, echo_id](std::string response) {
  // OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_RESOLVER_CALLED,
  // echo_id);
  // std::lock_guard<std::mutex> lock(state->state_mutex);
  // state->result = std::move(response);
  // state->response_received.store(true, std::memory_order_release);
  // OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_RESPONSE_SET,
  // echo_id);
  //};

  // request->rejecter = [state](const std::exception_ptr &ex) {
  // std::lock_guard<std::mutex> lock(state->state_mutex);
  // state->error_ptr = ex;
  // state->response_received.store(true, std::memory_order_release);
  //};

  // OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_POLLING_RESOLVER_SET,
  // echo_id);

  //// 添加到pending requests
  //{
  // std::lock_guard lock(pending_requests_mutex_);
  // pending_requests_[echo_id] = request;
  // OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_ADD_PENDING_POLLING,
  // echo_id, pending_requests_.size());
  //}

  // try {
  //// 发送WebSocket消息
  // co_await asio::co_spawn(
  // send_strand_,
  //[this, action_payload]() -> asio::awaitable<void> {
  // co_await ws_client_->send(action_payload);
  //},
  // asio::use_awaitable);

  // OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_MSG_SENT_POLLING,
  // echo_id);

  //// 启动超时检查
  // asio::co_spawn(
  // ioc_,
  //[request, state, echo_id]() -> asio::awaitable<void> {
  // try {
  // co_await request->timeout_timer.async_wait(asio::use_awaitable);
  //// 超时发生
  // OBCX_I18N_DEBUG(
  // common::LogMessageKey::ONEBOT11_WS_TIMEOUT_DETECTED, echo_id);
  // std::lock_guard<std::mutex> lock(state->state_mutex);
  // if (!state->response_received.load(std::memory_order_acquire)) {
  // state->response_received.store(true, std::memory_order_release);
  //}
  //} catch (const boost::system::system_error &e) {
  // if (e.code() != asio::error::operation_aborted) {
  // OBCX_I18N_ERROR(
  // common::LogMessageKey::ONEBOT11_WS_TIMEOUT_EXCEPTION,
  // echo_id, e.what());
  //}
  //}
  //},
  // asio::detached);

  //// 轮询等待响应或超时
  // OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_START_POLLING_WAIT,
  // echo_id);

  // while (!state->response_received.load(std::memory_order_acquire)) {
  //// 使用短暂的sleep避免busy waiting
  // asio::steady_timer wait_timer(ioc_);
  // wait_timer.expires_after(std::chrono::milliseconds(10));

  // try {
  // co_await wait_timer.async_wait(asio::use_awaitable);
  //} catch (const boost::system::system_error &e) {
  // if (e.code() != asio::error::operation_aborted) {
  // throw;
  //}
  //}
  //}

  //// 取消超时timer
  // request->timeout_timer.cancel();

  //// 清理请求
  //{
  // std::lock_guard lock(pending_requests_mutex_);
  // pending_requests_.erase(echo_id);
  // OBCX_I18N_DEBUG(
  // common::LogMessageKey::ONEBOT11_WS_CLEAN_PENDING_POLLING, echo_id,
  // pending_requests_.size());
  //}

  //// 检查结果
  //{
  // std::lock_guard<std::mutex> lock(state->state_mutex);
  // if (state->error_ptr) {
  // std::rethrow_exception(state->error_ptr);
  //}

  // if (state->result.empty()) {
  // OBCX_I18N_ERROR(
  // common::LogMessageKey::ONEBOT11_WS_API_TIMEOUT_POLLING, echo_id);
  // throw std::runtime_error(common::I18nLogMessages::get_message(
  // common::LogMessageKey::ONEBOT11_WS_API_TIMEOUT_MSG));
  //}

  // OBCX_I18N_DEBUG(common::LogMessageKey::ONEBOT11_WS_API_SUCCESS_POLLING,
  // echo_id, state->result.length());
  // co_return state->result;
  //}

  //} catch (...) {
  //// 清理
  // request->timeout_timer.cancel();
  //{
  // std::lock_guard lock(pending_requests_mutex_);
  // pending_requests_.erase(echo_id);
  //}
  // throw;
  //}
  //}
}

auto WebSocketConnectionManager::is_connected() const -> bool {
  return is_connected_.load(std::memory_order_acquire);
}

void WebSocketConnectionManager::handle_timeout(uint64_t echo_id) {
  std::lock_guard lock(pending_requests_mutex_);
  auto it = pending_requests_.find(echo_id);
  if (it != pending_requests_.end()) {
    auto request = it->second;
    pending_requests_.erase(it);

    if constexpr (USE_COROUTINE_ASYNC_WAIT) {
      // 协程模式：调用 completion handler 并传递超时错误
      if (request->completion_handler) {
        request->completion_handler(asio::error::timed_out, "");
      }
    }
    // 轮询模式不需要处理，因为已经通过 response_received 标志处理了
  }
}

} // namespace obcx::network
