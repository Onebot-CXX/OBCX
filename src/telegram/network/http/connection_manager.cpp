#include "../../../../include/telegram/network/connection_manager.hpp"
#include "common/logger.hpp"
#include "telegram/adapter/protocol_adapter.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <nlohmann/json.hpp>

using obcx::network::ProxyConfig;

namespace obcx::network {

using json = nlohmann::json;

TelegramConnectionManager::TelegramConnectionManager(
    asio::io_context &ioc, adapter::telegram::ProtocolAdapter &adapter)
    : ioc_(ioc), adapter_(adapter), poll_timer_(ioc) {
  OBCX_I18N_INFO(common::LogMessageKey::CONNECTION_ESTABLISHED,
                 "TelegramConnectionManager", "initialized");
}

void TelegramConnectionManager::connect(
    const common::ConnectionConfig &config) {
  config_ = config;

  // 检查是否需要使用代理
  if (!config_.proxy_host.empty() && config_.proxy_port > 0) {
    // 使用代理HTTP客户端
    ProxyConfig proxy_config;
    proxy_config.host = config_.proxy_host;
    proxy_config.port = config_.proxy_port;

    // 设置代理类型
    if (config_.proxy_type == "socks5") {
      proxy_config.type = ProxyType::SOCKS5;
    } else if (config_.proxy_type == "https") {
      proxy_config.type = ProxyType::HTTPS;
    } else {
      proxy_config.type = ProxyType::HTTP; // 默认HTTP
    }

    if (!config_.proxy_username.empty()) {
      proxy_config.username = config_.proxy_username;
    }
    if (!config_.proxy_password.empty()) {
      proxy_config.password = config_.proxy_password;
    }

    http_client_ =
        std::make_unique<ProxyHttpClient>(ioc_, proxy_config, config_);
    OBCX_I18N_INFO(common::LogMessageKey::PROXY_CONNECTION_ESTABLISHED,
                   config_.proxy_type, config_.proxy_host, config_.proxy_port,
                   config_.host, config_.port);
  } else {
    // 使用普通HTTP客户端
    http_client_ = std::make_unique<HttpClient>(ioc_, config_);
    OBCX_I18N_INFO(common::LogMessageKey::CONNECTION_ESTABLISHED, config_.host,
                   config_.port);
  }

  is_connected_ = true;
  start_polling();
}

void TelegramConnectionManager::disconnect() {
  stop_polling();
  is_connected_ = false;

  if (http_client_) {
    http_client_->close();
    http_client_.reset();
  }

  OBCX_I18N_INFO(common::LogMessageKey::CONNECTION_CLOSED);
}

auto TelegramConnectionManager::is_connected() const -> bool {
  return is_connected_.load();
}

auto TelegramConnectionManager::send_action_and_wait_async(
    std::string action_payload, uint64_t echo_id)
    -> asio::awaitable<std::string> {

  if (!http_client_) {
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::HTTP_CLIENT_NOT_INIT));
  }

  try {
    // 解析action_payload以获取方法名和参数
    auto payload_json = json::parse(action_payload);
    OBCX_I18N_DEBUG(common::LogMessageKey::SENDING_ACTION, action_payload);
    std::string method = payload_json.value("method", "");

    // 设置请求头
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    headers["User-Agent"] = "OBCX/1.0";

    if (!config_.access_token.empty()) {
      headers["Authorization"] = "Bearer " + config_.access_token;
    }

    // 构建Telegram API路径
    std::string api_path = "/bot" + config_.access_token + "/" + method;

    // 获取请求体（去除method字段）
    payload_json.erase("method");
    payload_json.erase("echo"); // Telegram API不支持echo字段
    std::string body = payload_json.dump();

    // 发送POST请求到Telegram API
    HttpResponse response = http_client_->post_sync(api_path, body, headers);

    if (!response.is_success()) {
      throw std::runtime_error(common::I18nLogMessages::format_message(
          common::LogMessageKey::HTTP_REQUEST_FAILED_STATUS,
          std::to_string(response.status_code)));
    }

    co_return response.body;

  } catch (const std::exception &e) {
    OBCX_I18N_ERROR(common::LogMessageKey::API_REQUEST_FAILED, e.what());
    throw;
  }
}

void TelegramConnectionManager::set_event_callback(EventCallback callback) {
  event_callback_ = std::move(callback);
}

auto TelegramConnectionManager::get_connection_type() const -> std::string {
  return "Telegram_HTTP";
}

auto TelegramConnectionManager::download_file(std::string file_id)
    -> asio::awaitable<std::string> {
  if (!http_client_) {
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::HTTP_CLIENT_NOT_INIT));
  }

  try {
    // 设置请求头
    std::map<std::string, std::string> headers;
    headers["User-Agent"] = "OBCX/1.0";

    if (!config_.access_token.empty()) {
      headers["Authorization"] = "Bearer " + config_.access_token;
    }

    // 构建getFile请求参数
    json params = {{"file_id", file_id}};

    // 构建getFile端点
    std::string get_file_path = "/bot" + config_.access_token + "/getFile";
    std::string body = params.dump();

    // 设置Content-Type头
    headers["Content-Type"] = "application/json";

    // 发送getFile请求
    HttpResponse response =
        http_client_->post_sync(get_file_path, body, headers);

    if (response.is_success() && !response.body.empty()) {
      // 解析响应以获取文件路径
      auto response_json = json::parse(response.body);

      if (response_json.contains("result") &&
          response_json["result"].contains("file_path")) {
        std::string file_path = response_json["result"]["file_path"];
        // 构建文件下载URL
        std::string download_url = "https://api.telegram.org/file/bot" +
                                   config_.access_token + "/" + file_path;
        co_return download_url;
      } else {
        throw std::runtime_error(common::I18nLogMessages::get_message(
            common::LogMessageKey::TELEGRAM_GETFILE_NO_PATH));
      }
    } else {
      throw std::runtime_error(common::I18nLogMessages::format_message(
          common::LogMessageKey::TELEGRAM_GETFILE_FAILED_STATUS,
          std::to_string(response.status_code)));
    }

  } catch (const std::exception &e) {
    OBCX_I18N_ERROR(common::LogMessageKey::DOWNLOAD_FILE_FAILED, e.what());
    throw;
  }
}

auto TelegramConnectionManager::download_file_content(
    std::string_view download_url) -> asio::awaitable<std::string> {
  if (!http_client_) {
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::HTTP_CLIENT_NOT_INIT));
  }

  try {
    // 解析下载URL以提取路径部分
    std::string url_str(download_url);
    size_t protocol_pos = url_str.find("://");
    if (protocol_pos == std::string::npos) {
      throw std::runtime_error(common::I18nLogMessages::get_message(
          common::LogMessageKey::DOWNLOAD_URL_INVALID_FORMAT));
    }

    size_t host_start = protocol_pos + 3;
    size_t path_start = url_str.find("/", host_start);
    if (path_start == std::string::npos) {
      throw std::runtime_error(common::I18nLogMessages::get_message(
          common::LogMessageKey::DOWNLOAD_URL_NO_PATH));
    }

    std::string path = url_str.substr(path_start);

    // 使用空的头部映射，让HttpClient的prepare_request设置完整的浏览器头部
    std::map<std::string, std::string> headers;

    // 直接使用GET请求下载文件内容
    HttpResponse response = http_client_->get_sync(path, headers);

    if (response.is_success()) {
      co_return response.body;
    } else {
      throw std::runtime_error(common::I18nLogMessages::format_message(
          common::LogMessageKey::FILE_DOWNLOAD_FAILED_STATUS,
          std::to_string(response.status_code)));
    }

  } catch (const std::exception &e) {
    OBCX_I18N_ERROR(common::LogMessageKey::DOWNLOAD_FILE_CONTENT_FAILED,
                    e.what());
    throw;
  }
}

void TelegramConnectionManager::start_polling() {
  if (is_polling_.exchange(true) == false) {
    // 启动轮询协程
    asio::co_spawn(ioc_, poll_updates(), asio::detached);
    OBCX_I18N_INFO(common::LogMessageKey::START_POLLING,
                   poll_interval_.count());
  }
}

void TelegramConnectionManager::stop_polling() {
  is_polling_ = false;
  poll_timer_.cancel();
  OBCX_I18N_INFO(common::LogMessageKey::STOP_POLLING);
}

auto TelegramConnectionManager::poll_updates() -> asio::awaitable<void> {
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

      // 构建getUpdates请求参数
      json params = {
          {"offset", update_offset_}, {"limit", 100}, {"timeout", 30}};

      // 轮询更新端点
      std::string updates_path = "/bot" + config_.access_token + "/getUpdates";
      std::string body = params.dump();

      // 设置Content-Type头
      headers["Content-Type"] = "application/json";

      HttpResponse response =
          http_client_->post_sync(updates_path, body, headers);

      if (response.is_success() && !response.body.empty()) {
        process_updates(response.body);
      }

    } catch (const std::exception &e) {
      OBCX_I18N_WARN(common::LogMessageKey::POLLING_FAILED, e.what());
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

  OBCX_I18N_DEBUG(common::LogMessageKey::POLLING_COROUTINE_EXIT);
}

void TelegramConnectionManager::process_updates(std::string_view updates_json) {
  try {
    auto json_data = json::parse(updates_json);
    OBCX_I18N_DEBUG(common::LogMessageKey::RECEIVED_UPDATES, updates_json);

    // 检查是否有result字段
    if (json_data.contains("result") && json_data["result"].is_array()) {
      auto result_array = json_data["result"];
      OBCX_I18N_DEBUG(common::LogMessageKey::PROCESSING_UPDATES,
                      result_array.size());

      // 更新offset为最新的update_id + 1
      if (!result_array.empty()) {
        auto last_update = result_array.back();
        if (last_update.contains("update_id")) {
          update_offset_ = last_update["update_id"].get<int>() + 1;
        }
      }

      // 处理每个更新
      for (const auto &update_json : result_array) {
        std::string single_update = update_json.dump();
        OBCX_I18N_DEBUG(common::LogMessageKey::PROCESSING_UPDATE,
                        single_update);
        auto event_opt = adapter_.parse_event(single_update);
        if (event_opt && event_callback_) {
          OBCX_I18N_DEBUG(common::LogMessageKey::DISPATCHING_EVENT);
          event_callback_(event_opt.value());
        } else if (!event_opt) {
          OBCX_I18N_DEBUG(common::LogMessageKey::FAILED_PARSE_EVENT);
        } else {
          OBCX_I18N_DEBUG(common::LogMessageKey::EVENT_CALLBACK_NOT_SET);
        }
      }
    }
  } catch (const std::exception &e) {
    OBCX_I18N_WARN(common::LogMessageKey::TELEGRAMBOT_UPDATE_PARSE_ERROR,
                   e.what());
  }
}

} // namespace obcx::network
