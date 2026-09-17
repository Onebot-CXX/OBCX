#include "telegram/network/connection_manager.hpp"
#include "common/logger.hpp"
#include "network/proxy_http_client.hpp"
#include "telegram/adapter/protocol_adapter.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>

using obcx::network::ProxyConfig;

namespace obcx::network {

using json = nlohmann::json;

namespace {

constexpr std::string_view kMultipartBoundary = "----OBCXBoundary7MA4YWxk";

void append_multipart_field(std::string &body, std::string_view name,
                            std::string_view value) {
  body += "--";
  body += kMultipartBoundary;
  body += "\r\nContent-Disposition: form-data; name=\"";
  body += name;
  body += "\"\r\n\r\n";
  body += value;
  body += "\r\n";
}

auto safe_multipart_filename(std::string filename) -> std::string {
  if (filename.empty()) {
    return "image.bin";
  }
  std::ranges::replace_if(
      filename,
      [](const char ch) { return ch == '\r' || ch == '\n' || ch == '"'; }, '_');
  return filename;
}

void append_multipart_file(std::string &body, std::string_view field_name,
                           const core::TelegramMediaUpload &file) {
  body += "--";
  body += kMultipartBoundary;
  body += "\r\nContent-Disposition: form-data; name=\"";
  body += field_name;
  body += "\"; filename=\"";
  body += safe_multipart_filename(file.filename);
  body += "\"\r\nContent-Type: ";
  body += file.mime_type.empty() ? "application/octet-stream" : file.mime_type;
  body += "\r\n\r\n";
  body += file.data;
  body += "\r\n";
}

auto telegram_http_error(const HttpResponse &response) -> std::runtime_error {
  return std::runtime_error(fmt::format("HTTP request failed: {}: {}",
                                        response.status_code, response.body));
}

} // namespace

auto telegram_api_response_body(const HttpResponse &response) -> std::string {
  if (response.is_success()) {
    return response.body;
  }
  const auto document = json::parse(response.body, nullptr, false);
  if (!document.is_discarded() && document.is_object() &&
      document.contains("ok") && document.at("ok").is_boolean() &&
      !document.at("ok").get<bool>()) {
    return response.body;
  }
  throw telegram_http_error(response);
}

auto build_telegram_media_group_multipart(
    std::string_view chat_id,
    const std::vector<core::TelegramMediaUpload> &media,
    std::string_view caption, std::optional<int64_t> message_thread_id,
    std::optional<std::string> reply_to_message_id)
    -> TelegramMultipartRequest {
  return build_telegram_media_group_multipart_with_entities(
      chat_id, media, caption, message_thread_id,
      std::move(reply_to_message_id), {});
}

auto build_telegram_media_group_multipart_with_entities(
    std::string_view chat_id,
    const std::vector<core::TelegramMediaUpload> &media,
    std::string_view caption, std::optional<int64_t> message_thread_id,
    std::optional<std::string> reply_to_message_id,
    const std::vector<core::TelegramTextEntity> &caption_entities)
    -> TelegramMultipartRequest {
  if (media.size() < 2 || media.size() > 10) {
    throw std::invalid_argument(
        "Telegram media group multipart requires 2 to 10 files");
  }

  nlohmann::json input_media = nlohmann::json::array();
  std::size_t total_data_size = 0;
  for (std::size_t i = 0; i < media.size(); ++i) {
    const auto &file = media[i];
    if (file.data.empty()) {
      throw std::invalid_argument("Telegram multipart media file is empty");
    }
    nlohmann::json item = {
        {"type", file.type.empty() ? "photo" : file.type},
        {"media", fmt::format("attach://media_{}", i)},
    };
    if (i == 0 && !caption.empty()) {
      item["caption"] = caption;
      if (!caption_entities.empty()) {
        item["caption_entities"] = caption_entities;
      }
    }
    input_media.push_back(std::move(item));
    total_data_size += file.data.size();
  }

  TelegramMultipartRequest request;
  request.body.reserve(total_data_size + 2048);
  append_multipart_field(request.body, "chat_id", chat_id);
  if (message_thread_id.has_value()) {
    append_multipart_field(request.body, "message_thread_id",
                           std::to_string(*message_thread_id));
  }
  if (reply_to_message_id.has_value()) {
    append_multipart_field(request.body, "reply_to_message_id",
                           *reply_to_message_id);
  }
  append_multipart_field(request.body, "media", input_media.dump());
  for (std::size_t i = 0; i < media.size(); ++i) {
    append_multipart_file(request.body, fmt::format("media_{}", i), media[i]);
  }
  request.body += "--";
  request.body += kMultipartBoundary;
  request.body += "--\r\n";
  request.content_type = std::string("multipart/form-data; boundary=") +
                         std::string(kMultipartBoundary);
  return request;
}

TelegramConnectionManager::TelegramConnectionManager(
    asio::io_context &ioc, adapter::telegram::ProtocolAdapter &adapter)
    : ioc_(ioc), adapter_(adapter), poll_timer_(asio::make_strand(ioc)) {
  OBCX_INFO("Connection established to {}:{}", "TelegramConnectionManager",
            "initialized");
}

TelegramConnectionManager::~TelegramConnectionManager() {
  // Release our own resources (poll_timer_, http_client_) while the
  // referenced installation io_context is still alive. BotInstallation owns
  // transports ahead of its executor and guarantees this destruction order.
  try {
    shutdown();
  } catch (const std::exception &error) {
    OBCX_ERROR("Failed to shut down Telegram connection manager: {}",
               error.what());
  } catch (...) {
    OBCX_ERROR("Failed to shut down Telegram connection manager");
  }
  http_client_.reset();
}

void TelegramConnectionManager::connect(
    const common::ConnectionConfig &config) {
  config_ = config;

  if (!config_.proxy_host.empty() && config_.proxy_port > 0) {
    ProxyConfig proxy_config;
    proxy_config.host = config_.proxy_host;
    proxy_config.port = config_.proxy_port;

    if (config_.proxy_type == "socks5") {
      proxy_config.type = ProxyType::SOCKS5;
    } else if (config_.proxy_type == "https") {
      proxy_config.type = ProxyType::HTTPS;
    } else {
      proxy_config.type = ProxyType::HTTP;
    }

    if (!config_.proxy_username.empty()) {
      proxy_config.username = config_.proxy_username;
    }
    if (!config_.proxy_password.empty()) {
      proxy_config.password = config_.proxy_password;
    }

    http_client_ =
        std::make_unique<ProxyHttpClient>(ioc_, proxy_config, config_);
    OBCX_INFO(
        "HTTP connection will be established through {} proxy {}:{} to {}:{}",
        config_.proxy_type, config_.proxy_host, config_.proxy_port,
        config_.host, config_.port);
  } else {
    http_client_ = std::make_unique<HttpClient>(ioc_, config_);
    OBCX_INFO("Connection established to {}:{}", config_.host, config_.port);
  }

  is_connected_ = true;
  start_polling();
}

void TelegramConnectionManager::disconnect() { shutdown(); }

void TelegramConnectionManager::shutdown() {
  stop_polling();
  is_connected_ = false;
  if (http_client_) {
    http_client_->close();
  }

  OBCX_INFO("Connection closed");
}

auto TelegramConnectionManager::is_connected() const -> bool {
  return is_connected_.load();
}

auto TelegramConnectionManager::send_action_and_wait_async(
    std::string action_payload, uint64_t echo_id)
    -> asio::awaitable<std::string> {

  if (!http_client_) {
    throw std::runtime_error("HTTP client not initialized");
  }

  try {
    auto payload_json = json::parse(action_payload);
    OBCX_TRACE("Sending action: {}", action_payload);
    std::string method = payload_json.value("method", "");

    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    headers["User-Agent"] = "OBCX/1.0";

    if (!config_.access_token.empty()) {
      headers["Authorization"] = "Bearer " + config_.access_token;
    }

    std::string api_path = "/bot" + config_.access_token + "/" + method;

    payload_json.erase("method");
    // Telegram Bot API does not understand the OneBot-style `echo` field; strip
    // it before sending so the request is accepted.
    payload_json.erase("echo");
    std::string body = payload_json.dump();

    HttpResponse response =
        co_await http_client_->post(api_path, body, headers);

    co_return telegram_api_response_body(response);

  } catch (const std::exception &e) {
    OBCX_ERROR("API request failed: {}", e.what());
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
    throw std::runtime_error("HTTP client not initialized");
  }

  try {
    std::map<std::string, std::string> headers;
    headers["User-Agent"] = "OBCX/1.0";

    if (!config_.access_token.empty()) {
      headers["Authorization"] = "Bearer " + config_.access_token;
    }

    json params = {{"file_id", file_id}};

    std::string get_file_path = "/bot" + config_.access_token + "/getFile";
    std::string body = params.dump();

    headers["Content-Type"] = "application/json";

    HttpResponse response =
        co_await http_client_->post(get_file_path, body, headers);

    if (response.is_success() && !response.body.empty()) {
      auto response_json = json::parse(response.body);

      if (response_json.contains("result") &&
          response_json["result"].contains("file_path")) {
        std::string file_path = response_json["result"]["file_path"];
        // Telegram returns a relative file_path; rebuild the absolute download
        // URL using the bot token.
        std::string download_url = "https://api.telegram.org/file/bot" +
                                   config_.access_token + "/" + file_path;
        co_return download_url;
      } else {
        throw std::runtime_error(
            std::string("No file_path field in getFile response"));
      }
    } else {
      throw std::runtime_error(fmt::format(
          "getFile request failed: {}", std::to_string(response.status_code)));
    }

  } catch (const std::exception &e) {
    OBCX_ERROR("Download file failed: {}", e.what());
    throw;
  }
}

auto TelegramConnectionManager::download_file_content(
    std::string_view download_url, const std::size_t maximum_bytes)
    -> asio::awaitable<std::string> {
  if (!http_client_) {
    throw std::runtime_error("HTTP client not initialized");
  }

  try {
    std::string url_str(download_url);
    size_t protocol_pos = url_str.find("://");
    if (protocol_pos == std::string::npos) {
      throw std::runtime_error("Invalid download URL format");
    }

    size_t host_start = protocol_pos + 3;
    size_t path_start = url_str.find('/', host_start);
    if (path_start == std::string::npos) {
      throw std::runtime_error("No path found in download URL");
    }

    std::string path = url_str.substr(path_start);

    // Empty header map: HttpClient::prepare_request fills in the full
    // browser-like header set we want for the file CDN.
    std::map<std::string, std::string> headers;

    HttpResponse response =
        co_await http_client_->get(path, headers, maximum_bytes);

    if (response.is_success()) {
      co_return response.body;
    } else {
      throw std::runtime_error(
          fmt::format("File download failed, status code: {}",
                      std::to_string(response.status_code)));
    }

  } catch (const std::exception &e) {
    OBCX_ERROR("Download file content failed: {}", e.what());
    throw;
  }
}

auto TelegramConnectionManager::upload_photo_multipart(
    std::string_view chat_id, const std::string &image_data,
    std::string_view filename, std::string_view mime_type,
    std::string_view caption, std::optional<int64_t> message_thread_id)
    -> asio::awaitable<std::string> {
  if (!http_client_) {
    throw std::runtime_error("HTTP client not initialized");
  }

  std::string body;
  body.reserve(image_data.size() + 512);

  body += "--";
  body += kMultipartBoundary;
  body += "\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n";
  body += chat_id;
  body += "\r\n";

  if (message_thread_id.has_value()) {
    body += "--";
    body += kMultipartBoundary;
    body += "\r\nContent-Disposition: form-data; "
            "name=\"message_thread_id\"\r\n\r\n";
    body += std::to_string(*message_thread_id);
    body += "\r\n";
  }

  if (!caption.empty()) {
    body += "--";
    body += kMultipartBoundary;
    body += "\r\nContent-Disposition: form-data; name=\"caption\"\r\n\r\n";
    body += caption;
    body += "\r\n";
  }

  body += "--";
  body += kMultipartBoundary;
  body += "\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"";
  body += filename;
  body += "\"\r\nContent-Type: ";
  body += mime_type;
  body += "\r\n\r\n";
  body += image_data;
  body += "\r\n";

  body += "--";
  body += kMultipartBoundary;
  body += "--\r\n";

  std::map<std::string, std::string> headers;
  headers["Content-Type"] = std::string("multipart/form-data; boundary=") +
                            std::string(kMultipartBoundary);

  std::string api_path = "/bot" + config_.access_token + "/sendPhoto";
  HttpResponse response = co_await http_client_->post(api_path, body, headers);

  co_return telegram_api_response_body(response);
}

auto TelegramConnectionManager::upload_media_group_multipart(
    std::string_view chat_id,
    const std::vector<core::TelegramMediaUpload> &media,
    std::string_view caption, std::optional<int64_t> message_thread_id,
    std::optional<std::string> reply_to_message_id)
    -> asio::awaitable<std::string> {
  co_return co_await upload_media_group_multipart_with_entities(
      chat_id, media, caption, message_thread_id,
      std::move(reply_to_message_id), {});
}

auto TelegramConnectionManager::upload_media_group_multipart_with_entities(
    std::string_view chat_id,
    const std::vector<core::TelegramMediaUpload> &media,
    std::string_view caption, std::optional<int64_t> message_thread_id,
    std::optional<std::string> reply_to_message_id,
    const std::vector<core::TelegramTextEntity> &caption_entities)
    -> asio::awaitable<std::string> {
  if (!http_client_) {
    throw std::runtime_error("HTTP client not initialized");
  }
  auto request = build_telegram_media_group_multipart_with_entities(
      chat_id, media, caption, message_thread_id, reply_to_message_id,
      caption_entities);

  std::map<std::string, std::string> headers;
  headers["Content-Type"] = request.content_type;

  const std::string api_path =
      "/bot" + config_.access_token + "/sendMediaGroup";
  HttpResponse response =
      co_await http_client_->post(api_path, request.body, headers);
  co_return telegram_api_response_body(response);
}

void TelegramConnectionManager::start_polling() {
  if (is_polling_.exchange(true) == false) {
    asio::co_spawn(poll_timer_.get_executor(), poll_updates(), asio::detached);
    OBCX_INFO("Start polling, interval: {}ms", config_.poll_timeout.count());
  }
}

void TelegramConnectionManager::stop_polling() {
  if (is_polling_.exchange(false)) {
    // Serialize timer cancellation with polling's expires_after/async_wait.
    asio::post(poll_timer_.get_executor(), [this] { poll_timer_.cancel(); });
    OBCX_INFO("Stop polling");
  }
}

auto TelegramConnectionManager::poll_updates() -> asio::awaitable<void> {
  while (is_polling_) {
    bool should_delay = false;

    try {
      if (!http_client_) {
        break;
      }

      std::map<std::string, std::string> headers;
      headers["User-Agent"] = "Mozilla/5.0 (X11; Linux x86_64; rv:147.0) "
                              "Gecko/20100101 Firefox/147.0";

      if (!config_.access_token.empty()) {
        headers["Authorization"] = "Bearer " + config_.access_token;
      }

      // poll_timeout: server-side long-poll wait — Telegram blocks up to this
      //   long when there are no new updates.
      // poll_force_close: client-side hard timeout that MUST exceed
      //   poll_timeout, otherwise the client tears down a healthy long-poll
      //   that's just waiting on the server.
      auto poll_timeout_sec =
          std::chrono::duration_cast<std::chrono::seconds>(config_.poll_timeout)
              .count();
      json params = {{"offset", update_offset_},
                     {"limit", 100},
                     {"timeout", poll_timeout_sec}};

      std::string updates_path = "/bot" + config_.access_token + "/getUpdates";
      std::string body = params.dump();

      headers["Content-Type"] = "application/json";

      // Force-close acts as the client safety net: if the connection silently
      // hangs we want to drop and retry rather than waiting forever.
      http_client_->set_timeout(config_.poll_force_close);

      HttpResponse response =
          co_await http_client_->post(updates_path, body, headers);

      if (!is_polling_) {
        break;
      }
      if (response.is_success() && !response.body.empty()) {
        process_updates(response.body);
      }

      // Telegram long-poll already implements the wait inside `timeout`; on
      // success we immediately reissue getUpdates with the advanced offset.

    } catch (const std::exception &e) {
      if (!is_polling_) {
        break;
      }
      OBCX_WARN("Polling failed: {}", e.what());
      should_delay = true;
    }

    // Only back off when an exception was thrown; happy path loops immediately.
    if (should_delay) {
      poll_timer_.expires_after(config_.poll_retry_interval);
      try {
        co_await poll_timer_.async_wait(asio::use_awaitable);
      } catch (const boost::system::system_error &e) {
        if (e.code() == asio::error::operation_aborted) {
          break;
        }
      }
    }
  }

  OBCX_DEBUG("Polling coroutine exited");
}

void TelegramConnectionManager::process_updates(std::string_view updates_json) {
  try {
    auto json_data = json::parse(updates_json);
    OBCX_DEBUG("Received updates: {}", updates_json);

    if (json_data.contains("result") && json_data["result"].is_array()) {
      auto result_array = json_data["result"];
      OBCX_DEBUG("Processing {} updates", result_array.size());

      // Advance offset past the last seen update_id; the next getUpdates call
      // will then ack everything we just processed.
      if (!result_array.empty()) {
        auto last_update = result_array.back();
        if (last_update.contains("update_id")) {
          update_offset_ = last_update["update_id"].get<int>() + 1;
        }
      }

      for (const auto &update_json : result_array) {
        std::string single_update = update_json.dump();
        OBCX_DEBUG("Processing single update: {}", single_update);
        auto event_opt = adapter_.parse_event(single_update);
        if (event_opt && event_callback_) {
          OBCX_DEBUG("Dispatching event");
          event_callback_(event_opt.value());
        } else if (!event_opt) {
          OBCX_DEBUG("Failed to parse event from update");
        } else {
          OBCX_DEBUG("Event callback not set");
        }
      }
    }
  } catch (const std::exception &e) {
    OBCX_WARN("Failed to parse update JSON: {}", e.what());
  }
}

} // namespace obcx::network
