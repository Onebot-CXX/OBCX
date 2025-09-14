#include "onebot11/network/http/ConnectionManager.hpp"
#include "common/Logger.hpp"
#include "network/ProxyHttpClient.hpp"
#include "onebot11/adapter/ProtocolAdapter.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <nlohmann/json.hpp>

namespace obcx::network {

using json = nlohmann::json;

HttpConnectionManager::HttpConnectionManager(
    asio::io_context &ioc, adapter::onebot11::ProtocolAdapter &adapter)
    : ioc_(ioc), adapter_(adapter), poll_timer_(ioc) {
  OBCX_INFO("HttpConnectionManager 已初始化");
}

void HttpConnectionManager::connect(const common::ConnectionConfig &config) {
  config_ = config;

  // if (config.proxy_host=="")
  http_client_ = std::make_unique<HttpClient>(ioc_, config_);
  // else
  // http_client_ = std::make_unique<ProxyHttpClient>(ioc_, config_);

  is_connected_ = true;
  start_polling();

  OBCX_INFO("HTTP连接已建立到 {}:{}", config_.host, config_.port);
}

void HttpConnectionManager::disconnect() {
  stop_polling();
  is_connected_ = false;

  if (http_client_) {
    http_client_->close();
    http_client_.reset();
  }

  OBCX_INFO("HTTP连接已断开");
}

auto HttpConnectionManager::is_connected() const -> bool {
  return is_connected_.load();
}

auto HttpConnectionManager::send_action_and_wait_async(
    std::string action_payload, uint64_t echo_id)
    -> asio::awaitable<std::string> {

  if (!http_client_) {
    throw std::runtime_error("HTTP客户端未初始化");
  }

  try {
    // 设置请求头
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    headers["User-Agent"] = "OBCX/1.0";

    if (!config_.access_token.empty()) {
      headers["Authorization"] = "Bearer " + config_.access_token;
    }

    // 发送POST请求到API端点
    // TODO: 重要问题 - 这里使用同步方法会阻塞整个io_context线程！
    // 应该实现真正的异步HTTP客户端，或者将阻塞操作移到线程池中执行
    std::string api_path = "/api"; // OneBot11标准端点
    auto response = http_client_->post_sync(api_path, action_payload, headers);

    if (!response.is_success()) {
      throw std::runtime_error("HTTP请求失败: " +
                               std::to_string(response.status_code));
    }

    co_return response.body;

  } catch (const std::exception &e) {
    OBCX_ERROR("HTTP API请求失败: {}", e.what());
    throw;
  }
}

void HttpConnectionManager::set_event_callback(EventCallback callback) {
  event_callback_ = std::move(callback);
}

auto HttpConnectionManager::get_connection_type() const -> std::string {
  return "HTTP";
}

void HttpConnectionManager::start_polling() {
  if (is_polling_.exchange(true) == false) {
    // 启动轮询协程
    asio::co_spawn(ioc_, poll_events(), asio::detached);
    OBCX_INFO("开始HTTP事件轮询，间隔: {}ms", poll_interval_.count());
  }
}

void HttpConnectionManager::stop_polling() {
  is_polling_ = false;
  poll_timer_.cancel();
  OBCX_INFO("停止HTTP事件轮询");
}

auto HttpConnectionManager::poll_events() -> asio::awaitable<void> {
  while (is_polling_) {
    try {
      if (!http_client_) {
        break;
      }

      // 设置请求头
      std::map<std::string, std::string> headers;
      headers["User-Agent"] = "OBCX/1.0";

      if (!config_.access_token.empty()) {
        headers["Authorization"] = "Bearer " + config_.access_token;
      }

      // 轮询事件端点
      std::string events_path = "/get_latest_events"; // OneBot11事件端点
      auto response = http_client_->get_sync(events_path, headers);

      if (response.is_success() && !response.body.empty()) {
        process_events(response.body);
      }

    } catch (const std::exception &e) {
      OBCX_WARN("事件轮询失败: {}", e.what());
    }

    // 等待下次轮询
    poll_timer_.expires_after(poll_interval_);
    try {
      co_await poll_timer_.async_wait(asio::use_awaitable);
    } catch (const boost::system::system_error &e) {
      if (e.code() == asio::error::operation_aborted) {
        break; // 轮询被取消
      }
    }
  }

  OBCX_DEBUG("HTTP事件轮询协程已退出");
}

void HttpConnectionManager::process_events(std::string_view events_json) {
  try {
    auto json_data = json::parse(events_json);

    // 处理单个事件
    if (json_data.is_object()) {
      auto event_opt = adapter_.parse_event(std::string(events_json));
      if (event_opt && event_callback_) {
        event_callback_(event_opt.value());
      }
    }
    // 处理事件数组
    else if (json_data.is_array()) {
      for (const auto &event_json : json_data) {
        std::string single_event = event_json.dump();
        auto event_opt = adapter_.parse_event(single_event);
        if (event_opt && event_callback_) {
          event_callback_(event_opt.value());
        }
      }
    }

  } catch (const json::exception &e) {
    OBCX_WARN("解析事件JSON失败: {}", e.what());
  }
}

} // namespace obcx::network