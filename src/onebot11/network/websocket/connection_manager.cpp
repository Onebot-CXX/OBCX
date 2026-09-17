#include "onebot11/network/websocket/connection_manager.hpp"
#include "common/logger.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"

#include <atomic>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/bind/bind.hpp>
#include <utility>

namespace obcx::network {

WebSocketConnectionManager::WebSocketConnectionManager(
    asio::io_context &ioc, adapter::onebot11::ProtocolAdapter &adapter,
    detail::ActionDeadlineFactory deadline_factory)
    : ioc_(ioc), adapter_(adapter), reconnect_timer_(ioc),
      send_strand_(asio::make_strand(ioc)),
      action_requests_(std::make_shared<detail::ActionRequestTracker>(
          ioc.get_executor(), std::move(deadline_factory))) {}

WebSocketConnectionManager::~WebSocketConnectionManager() {
  // Release our own resources while the referenced io_context is still alive
  // (BotInstallation guarantees this destruction order). Without this explicit
  // cleanup, send_strand_ — which holds a shared_ptr into the io_context's
  // strand_executor_service — would be destroyed after the service was gone,
  // crashing inside _Sp_counted_ptr_inplace::_M_destroy.
  try {
    shutdown();
  } catch (const std::exception &error) {
    OBCX_ERROR("Failed to shut down WebSocket connection manager: {}",
               error.what());
  } catch (...) {
    OBCX_ERROR("Failed to shut down WebSocket connection manager");
  }
  // shutdown() posts a detached close() but doesn't drop the client.
  // Reset it here so its socket/stream destructors run against a live
  // executor service.
  ws_client_.reset();
}

void WebSocketConnectionManager::set_event_callback(EventCallback callback) {
  event_callback_ = std::move(callback);
}

void WebSocketConnectionManager::connect(
    const common::ConnectionConfig &config) {
  // Per-API-request timeout (for action echo responses) is distinct from the
  // TCP connect timeout. Using connect_timeout (5s default) here previously
  // caused duplicate QQ deliveries: first-time media sends can take ~8s for
  // llonebot to ack, which tripped the timeout, fired a retry, and then the
  // original response also arrived successfully on the server.
  action_timeout_ = config.action_timeout;
  connect_ws(config.host, config.port, config.access_token);
}

void WebSocketConnectionManager::disconnect() { shutdown(); }

void WebSocketConnectionManager::shutdown() {
  const bool was_running = is_running_.exchange(false);
  is_connected_.store(false, std::memory_order_release);

  action_requests_->cancel_all();
  OBCX_DEBUG("Cleared all pending OneBot actions");
  if (!was_running) {
    return;
  }
  asio::post(send_strand_, [this] {
    reconnect_timer_.cancel();
    if (ws_client_) {
      asio::co_spawn(
          send_strand_,
          [client = ws_client_]() -> asio::awaitable<void> {
            co_await client->close();
          },
          asio::detached);
    }
  });
}

auto WebSocketConnectionManager::get_connection_type() const -> std::string {
  return "WebSocket";
}

void WebSocketConnectionManager::connect_ws(std::string host, uint16_t port,
                                            std::string access_token) {
  if (is_running_) {
    OBCX_WARN("ConnectionManager already has a running connection.");
    return;
  }
  host_ = std::move(host);
  port_ = port;
  access_token_ = std::move(access_token);
  is_running_ = true;

  do_connect();
}

void WebSocketConnectionManager::do_connect() {
  asio::post(send_strand_, [this]() -> void {
    if (!is_running_) {
      return;
    }
    ws_client_ = std::make_shared<WebsocketClient>(ioc_);
    OBCX_INFO("Attempting to connect to ws://{}:{}", host_, port_);

    asio::co_spawn(send_strand_,
                   ws_client_->run(host_, std::to_string(port_), access_token_,
                                   [this](const beast::error_code &ec,
                                          const std::string &message) -> void {
                                     this->on_ws_message(ec, message);
                                   }),
                   asio::detached);
  });
}

void WebSocketConnectionManager::on_ws_message(const beast::error_code &ec,
                                               const std::string &message) {
  if (ec) {
    OBCX_ERROR("Connection disconnected, error: {}", ec.message());
    is_connected_.store(false, std::memory_order_release);
    action_requests_->fail_all(
        std::make_exception_ptr(boost::system::system_error{ec}));
    schedule_reconnect();
    return;
  }

  if (message.empty()) {
    OBCX_INFO("WebSocket connection established");
    {
      is_connected_.store(true, std::memory_order_release);
    }
    reconnect_timer_.cancel();
    return;
  }

  OBCX_TRACE("WebSocket message received: bytes={}", message.size());

  try {
    nlohmann::json j = nlohmann::json::parse(message);

    if (j.contains("echo") && j.contains("retcode")) {
      uint64_t echo = j["echo"];

      if (action_requests_->respond(echo, message)) {
        OBCX_DEBUG("OneBot action response handled: echo={}", echo);
        return;
      }
      OBCX_WARN("Received OneBot action response with unknown echo: {}", echo);
    }
  } catch (const nlohmann::json::exception &e) {
    OBCX_WARN("Failed to parse WebSocket message JSON: {}", e.what());
  }

  auto event_opt = adapter_.parse_event(message);
  if (event_opt) {
    if (event_callback_) {
      event_callback_(event_opt.value());
    }
  } else {
    OBCX_DEBUG("Received invalid event JSON");
  }
}

void WebSocketConnectionManager::schedule_reconnect() {
  if (!is_running_) {
    return;
  }
  reconnect_timer_.expires_after(std::chrono::seconds(5));
  OBCX_INFO("Reconnection scheduled in {}ms", 5000);
  reconnect_timer_.async_wait(asio::bind_executor(
      send_strand_, [this](const beast::error_code &ec) -> void {
        if (ec) {
          if (ec != asio::error::operation_aborted) {
            OBCX_ERROR("Reconnect timer error: {}", ec.message());
          }
          return;
        }
        do_connect();
      }));
}

auto WebSocketConnectionManager::send_action_and_wait_async(
    std::string action_payload, uint64_t echo_id)
    -> asio::awaitable<std::string> {
  if (!ws_client_) {
    throw std::runtime_error("No available WebSocket client");
  }

  auto request = action_requests_->start(echo_id, action_timeout_);
  try {
    co_await asio::co_spawn(
        send_strand_,
        [this, action_payload =
                   std::move(action_payload)]() -> asio::awaitable<void> {
          co_await ws_client_->send(action_payload);
        },
        asio::use_awaitable);
    OBCX_DEBUG("OneBot action sent: echo={}", echo_id);
  } catch (...) {
    static_cast<void>(
        action_requests_->fail(echo_id, std::current_exception()));
  }

  auto outcome = co_await request->wait();
  switch (outcome.status) {
  case detail::ActionTerminalStatus::Response:
    OBCX_DEBUG("OneBot action completed: echo={} response_bytes={}", echo_id,
               outcome.response.size());
    co_return std::move(outcome.response);
  case detail::ActionTerminalStatus::Timeout:
    OBCX_ERROR("OneBot action timed out: echo={}", echo_id);
    throw std::runtime_error("API request timeout");
  case detail::ActionTerminalStatus::TransportFailure:
  case detail::ActionTerminalStatus::Cancelled:
    if (outcome.failure) {
      std::rethrow_exception(outcome.failure);
    }
    throw boost::system::system_error{asio::error::operation_aborted};
  }
  throw std::runtime_error("OneBot action completed with an invalid state");
}

auto WebSocketConnectionManager::is_connected() const -> bool {
  return is_connected_.load(std::memory_order_acquire);
}

} // namespace obcx::network
