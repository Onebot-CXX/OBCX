#include "onebot11/network/http/connection_manager.hpp"
#include "common/logger.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <nlohmann/json.hpp>

namespace obcx::network {

using json = nlohmann::json;

HttpConnectionManager::HttpConnectionManager(
    asio::io_context &ioc, adapter::onebot11::ProtocolAdapter &adapter)
    : ioc_(ioc), adapter_(adapter), poll_timer_(ioc) {
  OBCX_INFO("HttpConnectionManager initialized");
}

HttpConnectionManager::~HttpConnectionManager() {
  // Release our own resources (poll_timer_, http_client_) while the
  // referenced installation io_context is still alive. BotInstallation owns
  // transports ahead of its executor and guarantees this destruction order.
  try {
    shutdown();
  } catch (const std::exception &error) {
    OBCX_ERROR("Failed to shut down HTTP connection manager: {}", error.what());
  } catch (...) {
    OBCX_ERROR("Failed to shut down HTTP connection manager");
  }
}

void HttpConnectionManager::connect(const common::ConnectionConfig &config) {
  config_ = config;

  http_client_ = std::make_unique<HttpClient>(ioc_, config_);

  is_connected_ = true;
  start_polling();

  OBCX_INFO("HTTP connection established to {}:{}", config_.host, config_.port);
}

void HttpConnectionManager::disconnect() { shutdown(); }

void HttpConnectionManager::shutdown() {
  stop_polling();
  is_connected_ = false;

  if (http_client_) {
    http_client_->close();
    http_client_.reset();
  }

  OBCX_INFO("HTTP connection disconnected");
}

auto HttpConnectionManager::is_connected() const -> bool {
  return is_connected_.load();
}

auto HttpConnectionManager::send_action_and_wait_async(
    std::string action_payload, uint64_t echo_id)
    -> asio::awaitable<std::string> {

  if (!http_client_) {
    throw std::runtime_error("HTTP client not initialized");
  }

  try {
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    headers["User-Agent"] = "OBCX/1.0";

    if (!config_.access_token.empty()) {
      headers["Authorization"] = "Bearer " + config_.access_token;
    }

    std::string api_path = "/api"; // OneBot11 standard action endpoint
    auto response =
        co_await http_client_->post(api_path, action_payload, headers);

    if (!response.is_success()) {
      throw std::runtime_error(fmt::format(
          "HTTP request failed: {}", std::to_string(response.status_code)));
    }

    co_return response.body;

  } catch (const std::exception &e) {
    OBCX_ERROR("HTTP API request failed: {}", e.what());
    throw;
  }
}

void HttpConnectionManager::set_event_callback(EventCallback callback) {
  event_callback_ = std::move(callback);
}

auto HttpConnectionManager::get_connection_type() const -> std::string {
  return "HTTP";
}

void HttpConnectionManager::set_poll_interval(
    const std::chrono::milliseconds interval) {
  if (is_polling_) {
    throw std::logic_error(
        "cannot change OneBot HTTP poll interval while running");
  }
  poll_interval_ = interval;
}

void HttpConnectionManager::start_polling() {
  if (is_polling_.exchange(true) == false) {
    asio::co_spawn(ioc_, poll_events(), asio::detached);
    OBCX_INFO("Start HTTP event polling, interval: {}ms",
              poll_interval_.count());
  }
}

void HttpConnectionManager::stop_polling() {
  is_polling_ = false;
  poll_timer_.cancel();
  OBCX_INFO("Stop HTTP event polling");
}

auto HttpConnectionManager::poll_events() -> asio::awaitable<void> {
  while (is_polling_) {
    try {
      if (!http_client_) {
        break;
      }

      std::map<std::string, std::string> headers;
      headers["User-Agent"] = "OBCX/1.0";

      if (!config_.access_token.empty()) {
        headers["Authorization"] = "Bearer " + config_.access_token;
      }

      std::string_view events_path =
          "/get_latest_events"; // OneBot11 events endpoint
      auto response = co_await http_client_->get(events_path, headers);

      if (response.is_success() && !response.body.empty()) {
        process_events(response.body);
      }

    } catch (const std::exception &e) {
      OBCX_WARN("Event polling failed: {}", e.what());
    }

    poll_timer_.expires_after(poll_interval_);
    try {
      co_await poll_timer_.async_wait(asio::use_awaitable);
    } catch (const boost::system::system_error &e) {
      if (e.code() == asio::error::operation_aborted) {
        break;
      }
    }
  }

  OBCX_DEBUG("HTTP event polling coroutine exited");
}

void HttpConnectionManager::process_events(std::string_view events_json) {
  try {
    auto json_data = json::parse(events_json);

    if (json_data.is_object()) {
      auto event_opt = adapter_.parse_event(std::string(events_json));
      if (event_opt && event_callback_) {
        event_callback_(event_opt.value());
      }
    } else if (json_data.is_array()) {
      for (const auto &event_json : json_data) {
        std::string single_event = event_json.dump();
        auto event_opt = adapter_.parse_event(single_event);
        if (event_opt && event_callback_) {
          event_callback_(event_opt.value());
        }
      }
    }

  } catch (const json::exception &e) {
    OBCX_WARN("Failed to parse event JSON: {}", e.what());
  }
}

} // namespace obcx::network
