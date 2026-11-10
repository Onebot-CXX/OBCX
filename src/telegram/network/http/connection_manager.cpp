#include "telegram/network/connection_manager.hpp"
#include "common/logger.hpp"
#include "network/proxy_http_client.hpp"
#include "telegram/adapter/protocol_adapter.hpp"
#include "telegram/bot/platform_adapter.hpp"
#include "telegram/bot/response_parser.hpp"
#include "telegram/network/detail/media_read.hpp"

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
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
    asio::io_context &ioc,
    std::shared_ptr<adapter::telegram::ProtocolAdapter> adapter)
    : ioc_(ioc), adapter_(std::move(adapter)),
      poll_executor_(asio::make_strand(ioc)) {
  OBCX_INFO("Connection established to {}:{}", "TelegramConnectionManager",
            "initialized");
}

TelegramConnectionManager::~TelegramConnectionManager() {
  // Release resources while the referenced installation io_context is alive.
  // BotInstallation owns transports ahead of its executor and guarantees this
  // destruction order.
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
  std::lock_guard guard(event_mutex_);
  if (!identity_ || !adapter_ || !event_callback_)
    throw std::logic_error("Telegram receive authority not configured");
  shutdown();
  ++epoch_;
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
        std::make_shared<ProxyHttpClient>(ioc_, proxy_config, config_);
    OBCX_INFO(
        "HTTP connection will be established through {} proxy {}:{} to {}:{}",
        config_.proxy_type, config_.proxy_host, config_.proxy_port,
        config_.host, config_.port);
  } else {
    http_client_ = std::make_shared<HttpClient>(ioc_, config_);
    OBCX_INFO("Connection established to {}:{}", config_.host, config_.port);
  }

  is_connected_ = true;
  is_polling_ = true;
  update_offset_ = 0;
  try {
    // The current client is usable BEFORE cache eligibility is established.
    identity_->connection_changed(true);
    start_polling();
  } catch (...) {
    shutdown();
    throw;
  }
}

void TelegramConnectionManager::disconnect() { shutdown(); }

void TelegramConnectionManager::shutdown() {
  std::lock_guard guard(event_mutex_);
  if (!is_connected_ && !is_polling_)
    return;
  ++epoch_;
  is_connected_ = false;
  // Native lock -> cache -> evidence. Eligibility is revoked synchronously;
  // delayed timer/cancellation handlers cannot restore it.
  try {
    if (identity_)
      identity_->connection_changed(false);
  } catch (...) {
    // The cache has already committed disconnection before scheduling cleanup.
  }
  stop_polling();
  if (http_client_)
    http_client_->close();
  OBCX_INFO("Connection closed");
}

void TelegramConnectionManager::configure_identity(
    std::shared_ptr<core::SelfIdentityCache> cache) {
  std::lock_guard guard(event_mutex_);
  if (!cache || identity_ || is_connected_)
    throw std::logic_error(
        "Telegram receive authority must be configured once");
  identity_ = std::move(cache);
}

bool TelegramConnectionManager::receive_current(
    const TelegramReceiveOrigin &work) {
  if (!is_connected_ || work.epoch != epoch_ || work.client != http_client_ ||
      !identity_)
    return false;
  // This callback performs no locking, cache reentry, I/O or scheduling.
  return identity_
      ->observe_user_and_capture(work.authority, std::nullopt, [] {})
      .accepted();
}

auto TelegramConnectionManager::capture_receive(
    const core::SelfIdentityOrigin &origin)
    -> std::optional<TelegramReceiveOrigin> {
  std::lock_guard guard(event_mutex_);
  if (!is_connected_ || !http_client_ || !identity_)
    return std::nullopt;
  if (!identity_->observe_user_and_capture(origin, std::nullopt, [] {})
           .accepted())
    return std::nullopt;
  return TelegramReceiveOrigin{epoch_.load(), http_client_, origin,
                               config_.access_token};
}

auto TelegramConnectionManager::fetch_self(TelegramReceiveOrigin work)
    -> asio::awaitable<std::optional<std::string>> {
  {
    std::lock_guard guard(event_mutex_);
    if (!receive_current(work))
      co_return std::nullopt;
  }
  // Use only the captured client/path, never the mutable current connection.
  const auto path = "/bot" + work.access_token + "/getMe";
  const std::string body = "{}";
  std::map<std::string, std::string> headers{
      {"Content-Type", "application/json"}, {"User-Agent", "OBCX/1.0"}};
  if (!work.access_token.empty())
    headers["Authorization"] = "Bearer " + work.access_token;
  const auto response = co_await work.client->post(path, body, headers);
  {
    std::lock_guard guard(event_mutex_);
    if (!receive_current(work))
      co_return std::nullopt;
  }
  co_return telegram_api_response_body(response);
}

auto TelegramConnectionManager::publish_self(const TelegramReceiveOrigin &work,
                                             const obcx::bot::SelfInfo &info)
    -> core::SelfObservationResult {
  std::lock_guard guard(event_mutex_);
  if (!is_connected_ || work.epoch != epoch_ || work.client != http_client_ ||
      !identity_)
    return {core::SelfObservationStatus::RejectedOrigin, std::nullopt};
  try {
    obcx::bot::binding::validate_native_self(
        obcx::telegram::bot::reference_codec(), info);
  } catch (...) {
    return {core::SelfObservationStatus::InvalidIdentity, std::nullopt};
  }
  auto result = identity_->publish_self(work.authority, info);
  if (result.status == core::SelfObservationStatus::Transitioned)
    retire_account();
  return result;
}

auto TelegramConnectionManager::is_connected() const -> bool {
  return is_connected_.load();
}

auto TelegramConnectionManager::send_action_and_wait_async(
    std::string action_payload, uint64_t echo_id)
    -> asio::awaitable<std::string> {

  auto client = http_client_;
  if (!http_client_) {
    throw std::runtime_error("HTTP client not initialized");
  }

  try {
    auto payload_json = json::parse(action_payload);
    OBCX_TRACE("Sending Telegram action ({} bytes)", action_payload.size());
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

    HttpResponse response = co_await client->post(api_path, body, headers);

    co_return telegram_api_response_body(response);

  } catch (const std::exception &) {
    // Native exception text can include token-bearing request/download paths.
    OBCX_ERROR("Telegram API request failed");
    throw;
  }
}

auto TelegramConnectionManager::send_bot_request(
    nlohmann::json parameters,
    const std::vector<std::pair<std::string, core::TelegramMediaUpload>> &files)
    -> asio::awaitable<std::string> {
  auto client = http_client_;
  if (!client || !is_connected_)
    throw HttpClientError("Telegram transport closed",
                          HttpRequestSubmissionState::DefinitelyNotSubmitted);
  const auto method = parameters.at("method").get<std::string>();
  parameters.erase("method");
  const std::string path = "/bot" + config_.access_token + "/" + method;
  std::map<std::string, std::string> headers;
  std::string body;
  if (files.empty()) {
    headers["Content-Type"] = "application/json";
    body = parameters.dump();
  } else {
    for (auto it = parameters.begin(); it != parameters.end(); ++it)
      append_multipart_field(body, it.key(),
                             it->is_string() ? it->get<std::string>()
                                             : it->dump());
    for (const auto &[name, file] : files)
      append_multipart_file(body, name, file);
    body += std::string("--") + std::string(kMultipartBoundary) + "--\r\n";
    headers["Content-Type"] = std::string("multipart/form-data; boundary=") +
                              std::string(kMultipartBoundary);
  }
  auto response = co_await client->post(path, body, headers);
  co_return telegram_api_response_body(response);
}

void TelegramConnectionManager::set_event_callback(EventCallback callback) {
  std::lock_guard guard(event_mutex_);
  event_callback_ = std::move(callback);
}

auto TelegramConnectionManager::get_connection_type() const -> std::string {
  return "Telegram_HTTP";
}

auto detail::TelegramMediaReadAccess::run(
    std::shared_ptr<TelegramConnectionManager> manager,
    std::optional<TelegramReceiveOrigin> origin,
    obcx::bot::ReadMediaRequest request, HttpDeadline deadline)
    -> asio::awaitable<Result> {
  namespace d = obcx::bot;
  using Code = d::BotOperationErrorCode;
  const auto failure = [](Code code, const char *message) {
    return d::failed_operation<d::MediaBlob>(
        code, message,
        code == Code::MalformedResponse || code == Code::TransportFailure);
  };
  if (!manager)
    co_return failure(Code::UnsupportedAction,
                      "Telegram native media reader unavailable");
  if (!origin)
    co_return failure(Code::CacheUnavailable, "Telegram media origin retired");

  try {
    const auto cancellation = co_await asio::this_coro::cancellation_state;
    // This ordinary callback owns all transport state used by HTTP admission.
    const auto eligible = [manager, work = *origin] {
      std::lock_guard guard(manager->event_mutex_);
      return manager->receive_current(work);
    };
    const auto stage_failure = [&]() -> std::optional<Result> {
      if (!eligible())
        return failure(Code::CacheUnavailable, "Telegram media origin retired");
      if (cancellation.cancelled() != asio::cancellation_type::none)
        return failure(Code::Cancelled, "Telegram media read cancelled");
      if (std::chrono::steady_clock::now() >= deadline)
        return failure(Code::TransportFailure,
                       "Telegram media deadline expired");
      return std::nullopt;
    };
    const auto http_failure =
        [&](const HttpCallResult &response) -> std::optional<Result> {
      if (response.error == asio::error::message_size)
        return failure(Code::MediaTooLarge,
                       "Telegram media response exceeds limit");
      if (response.error == asio::error::operation_aborted)
        return failure(Code::Cancelled, "Telegram media read cancelled");
      if (response.error || !response.response_complete)
        return failure(Code::TransportFailure,
                       "Telegram media transport failed");
      return std::nullopt;
    };
    if (auto failed = stage_failure())
      co_return std::move(*failed);
    request.validate();
    const auto &codec = obcx::telegram::bot::reference_codec();
    codec.validate(request.media);
    const auto parts = codec.unpack(request.media.id);
    const auto body = json{{"file_id", parts.components[0]}}.dump();
    const auto metadata_limit = origin->client->response_body_limit();
    std::map<std::string, std::string> headers{
        {"Content-Type", "application/json"}, {"User-Agent", "OBCX/1.0"}};
    if (!origin->access_token.empty())
      headers["Authorization"] = "Bearer " + origin->access_token;
    auto response = co_await HttpCallAccess::post_until(
        *origin->client, "/bot" + origin->access_token + "/getFile", body,
        std::move(headers), metadata_limit, deadline, eligible);
    if (auto failed = stage_failure())
      co_return std::move(*failed);
    if (auto failed = http_failure(response))
      co_return std::move(*failed);

    d::MediaMetadata metadata;
    std::optional<d::MediaIdentity> identity;
    std::string path;
    try {
      if (response.body.size() > metadata_limit)
        co_return failure(Code::MediaTooLarge,
                          "Telegram metadata exceeds limit");
      // Bound parse nesting before allocating a deep tree, then inspect the
      // borrowed tree and individual strings before any owned field copies.
      const auto document = json::parse(
          response.body, [](int depth, json::parse_event_t, json &) {
            if (depth > 32)
              throw d::detail::BotBudgetExceeded{};
            return true;
          });
      d::detail::validate_bot_wire(document);
      if (!document.is_object() || !document.at("ok").is_boolean())
        throw std::invalid_argument("invalid provider status");
      if (!document.at("ok").get<bool>()) {
        auto parsed = obcx::telegram::bot::parse_telegram_operation_response(
            response.body, false);
        if (parsed.error) {
          // Keep typed provider status/retry evidence, never provider prose.
          parsed.error->message = "Telegram media provider response rejected";
          if (parsed.error->provider_code &&
              !std::ranges::all_of(
                  *parsed.error->provider_code,
                  [](unsigned char c) { return c >= '0' && c <= '9'; }))
            parsed.error->provider_code.reset();
          co_return Result::failure(std::move(*parsed.error));
        }
        throw std::invalid_argument("invalid provider failure");
      }
      if (response.status_code != 200)
        co_return failure(Code::TransportFailure,
                          "Telegram metadata HTTP status invalid");
      const auto &file = document.at("result");
      const auto &file_id =
          d::detail::require_identifier(file, "file_id", "Telegram file", 1024);
      if (file_id != parts.components[0])
        throw std::invalid_argument("file identity mismatch");
      if (file.contains("file_size")) {
        metadata.size_bytes =
            d::detail::bounded_unsigned(file.at("file_size"), UINT64_MAX);
        if (*metadata.size_bytes > request.maximum_bytes)
          co_return failure(Code::MediaTooLarge,
                            "Telegram file exceeds byte limit");
      }
      if (file.contains("file_unique_id")) {
        const auto &unique_id = d::detail::require_identifier(
            file, "file_unique_id", "Telegram file", 1024);
        identity = d::MediaIdentity{
            request.media.installation,
            codec.make<d::MediaIdentityId>("file-unique", {unique_id})};
      }
      const auto &file_path = d::detail::require_identifier(
          file, "file_path", "Telegram file", 1024);
      if (file_path.starts_with('/') ||
          !std::ranges::all_of(file_path,
                               [](unsigned char c) {
                                 return c < 128 &&
                                        (std::isalnum(c) || c == '/' ||
                                         c == '.' || c == '_' || c == '-');
                               }) ||
          file_path.find("..") != std::string::npos ||
          file_path.find_first_of("\\?#%\r\n") != std::string::npos)
        throw std::invalid_argument("unsafe relative file path");
      path = "/file/bot" + origin->access_token + "/" + file_path;
    } catch (...) {
      co_return failure(Code::MalformedResponse,
                        "Telegram file metadata invalid");
    }

    if (auto failed = stage_failure())
      co_return std::move(*failed);
    // No URL, redirect destination or credentials come from provider fields.
    // Await the shared HTTP operation itself, including cancellation cleanup.
    auto content = co_await HttpCallAccess::get_until(
        *origin->client, std::move(path), {}, request.maximum_bytes, deadline,
        eligible);
    if (auto failed = stage_failure())
      co_return std::move(*failed);
    if (auto failed = http_failure(content))
      co_return std::move(*failed);
    if (content.status_code != 200)
      co_return failure(Code::TransportFailure,
                        "Telegram media HTTP status invalid");
    if (content.body.empty())
      co_return failure(Code::MalformedResponse,
                        "Telegram returned empty media");
    if (content.body.size() > request.maximum_bytes)
      co_return failure(Code::MediaTooLarge,
                        "Telegram file exceeds byte limit");
    if (metadata.size_bytes && *metadata.size_bytes != content.body.size())
      co_return failure(Code::MalformedResponse,
                        "Telegram media size does not match file metadata");
    metadata.size_bytes = content.body.size();
    const auto kind = parts.kind == "photo" || parts.kind == "sticker-static"
                          ? d::MediaKind::Image
                      : parts.kind == "animation" ||
                              parts.kind == "sticker-webm" ||
                              parts.kind == "sticker-tgs"
                          ? d::MediaKind::Animation
                      : parts.kind == "video" || parts.kind == "video-note"
                          ? d::MediaKind::Video
                      : parts.kind == "audio" ? d::MediaKind::Audio
                      : parts.kind == "voice" ? d::MediaKind::Voice
                                              : d::MediaKind::File;
    if (parts.kind == "sticker-static")
      metadata.mime_type = "image/webp";
    if (parts.kind == "sticker-webm")
      metadata.mime_type = "video/webm";
    if (parts.kind == "sticker-tgs")
      metadata.mime_type = "application/x-tgsticker";
    d::MediaBlob blob{
        {{content.body.begin(), content.body.end()}, metadata}, kind, identity};
    blob.validate();
    // Publication is fenced again after the final owned-byte copy, under the
    // same native lock as stop/account retirement. Never recapture authority.
    std::lock_guard guard(manager->event_mutex_);
    if (auto failed = stage_failure())
      co_return std::move(*failed);
    co_return Result::success(std::move(blob));
  } catch (const boost::system::system_error &error) {
    co_return failure(error.code() == asio::error::operation_aborted
                          ? Code::Cancelled
                          : Code::TransportFailure,
                      "Telegram media read interrupted");
  } catch (...) {
    co_return failure(Code::TransportFailure, "Telegram media read failed");
  }
}

auto TelegramConnectionManager::download_file(std::string file_id)
    -> asio::awaitable<std::string> {
  const auto origin_epoch = epoch_.load();
  const auto token = config_.access_token;
  auto client = http_client_;
  if (!http_client_) {
    throw std::runtime_error("HTTP client not initialized");
  }

  try {
    std::map<std::string, std::string> headers;
    headers["User-Agent"] = "OBCX/1.0";

    if (!token.empty()) {
      headers["Authorization"] = "Bearer " + token;
    }

    json params = {{"file_id", file_id}};

    std::string get_file_path = "/bot" + token + "/getFile";
    std::string body = params.dump();

    headers["Content-Type"] = "application/json";

    HttpResponse response = co_await client->post(get_file_path, body, headers);

    if (origin_epoch != epoch_ || !is_connected_)
      throw std::runtime_error("Telegram file origin epoch retired");
    if (response.is_success() && !response.body.empty()) {
      auto response_json = json::parse(response.body);

      if (response_json.value("ok", false) &&
          response_json.contains("result") &&
          response_json["result"].value("file_id", std::string{}) == file_id &&
          response_json["result"].contains("file_path")) {
        std::string file_path = response_json["result"]["file_path"];
        // Telegram returns a relative file_path; rebuild the absolute download
        // URL using the bot token.
        if (file_path.empty() || file_path.size() > 1024 ||
            file_path.starts_with('/') ||
            !std::ranges::all_of(file_path,
                                 [](unsigned char c) {
                                   return c < 128 &&
                                          (std::isalnum(c) || c == '/' ||
                                           c == '.' || c == '_' || c == '-');
                                 }) ||
            file_path.find("..") != std::string::npos ||
            file_path.find_first_of("\\?#%\r\n") != std::string::npos)
          throw std::runtime_error("Telegram returned unsafe file path");
        std::string download_url = "/file/bot" + token + "/" + file_path;
        co_return download_url;
      } else {
        throw std::runtime_error(
            std::string("No file_path field in getFile response"));
      }
    } else {
      throw std::runtime_error(fmt::format(
          "getFile request failed: {}", std::to_string(response.status_code)));
    }

  } catch (const std::exception &) {
    OBCX_ERROR("Telegram file lookup failed");
    throw;
  }
}

auto TelegramConnectionManager::download_file_content(
    std::string_view download_url, const std::size_t maximum_bytes)
    -> asio::awaitable<std::string> {
  auto client = http_client_;
  if (!http_client_) {
    throw std::runtime_error("HTTP client not initialized");
  }

  try {
    const std::string prefix = "/file/bot" + config_.access_token + "/";
    if (!download_url.starts_with(prefix) ||
        download_url.find("..") != std::string_view::npos ||
        download_url.find_first_of("\\?#%\r\n") != std::string_view::npos)
      throw std::runtime_error("unapproved Telegram download path");
    std::string path{download_url};

    // Empty header map: HttpClient::prepare_request fills in the full
    // browser-like header set we want for the file CDN.
    std::map<std::string, std::string> headers;

    HttpResponse response = co_await client->get(path, headers, maximum_bytes);

    if (response.is_success()) {
      co_return response.body;
    } else {
      throw std::runtime_error(
          fmt::format("File download failed, status code: {}",
                      std::to_string(response.status_code)));
    }

  } catch (const std::exception &) {
    OBCX_ERROR("Telegram file download failed");
    throw;
  }
}

auto TelegramConnectionManager::upload_photo_multipart(
    std::string_view chat_id, const std::string &image_data,
    std::string_view filename, std::string_view mime_type,
    std::string_view caption, std::optional<int64_t> message_thread_id)
    -> asio::awaitable<std::string> {
  auto client = http_client_;
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
  HttpResponse response = co_await client->post(api_path, body, headers);

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
  auto client = http_client_;
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
      co_await client->post(api_path, request.body, headers);
  co_return telegram_api_response_body(response);
}

// One owned poll (including its retry wait) survives until co_spawn completion.
// Pending replacement work is captured by the lifecycle/account transition,
// not by the stale batch that happens to finish the join.
struct TelegramConnectionManager::Poll {
  Poll(TelegramReceiveOrigin receive, common::ConnectionConfig configuration,
       std::int64_t offset, asio::any_io_executor executor)
      : receive(std::move(receive)), config(std::move(configuration)),
        offset(offset), retry_timer(std::move(executor)) {}
  TelegramReceiveOrigin receive;
  const common::ConnectionConfig config;
  const std::int64_t offset;
  asio::steady_timer retry_timer;
  asio::cancellation_signal cancellation;
  bool rearm{true}; // event_mutex_-owned, including completion paths
};

void TelegramConnectionManager::retire_account() noexcept {
  ++epoch_;
  update_offset_ = 0;
  pending_poll_.reset();
  cancel_poll();
  try {
    // A fresh origin is for NEW work only. Never rebase the batch remainder.
    start_polling();
  } catch (...) {
    // Account/cursor retirement committed even if replacement scheduling fails.
    // Even a diagnostic failure must not hide the authoritative transition.
    try {
      OBCX_WARN("Telegram account retired; poll rearm unavailable");
    } catch (...) {
    }
  }
}

void TelegramConnectionManager::start_polling() {
  if (!is_polling_ || !is_connected_ || !identity_)
    return;
  const auto authority = identity_->capture_origin();
  if (!authority)
    return;
  auto receive = capture_receive(*authority);
  if (!receive)
    return;
  pending_poll_ = std::make_shared<Poll>(std::move(*receive), config_,
                                         update_offset_, poll_executor_);
  launch_pending_poll();
}

void TelegramConnectionManager::launch_pending_poll() {
  if (active_poll_ || !pending_poll_)
    return;
  auto poll = std::move(pending_poll_);
  if (!is_polling_ || !receive_current(poll->receive))
    return;
  active_poll_ = poll;
  try {
    asio::co_spawn(
        poll_executor_, poll_updates(poll),
        asio::bind_cancellation_slot(
            poll->cancellation.slot(),
            [self = shared_from_this(), poll](std::exception_ptr error) {
              self->poll_finished(poll, error);
            }));
  } catch (...) {
    active_poll_.reset();
    throw;
  }
}

void TelegramConnectionManager::cancel_poll() {
  if (!active_poll_)
    return;
  active_poll_->rearm = false;
  try {
    // Signal/timer operations are confined to the polling strand. This closure
    // owns only the retired poll; it cannot cancel a replacement poll's wait.
    asio::post(poll_executor_, [poll = active_poll_] {
      poll->cancellation.emit(asio::cancellation_type::all);
      poll->retry_timer.cancel();
    });
  } catch (...) {
    // Native/authority fencing still applies; wait for the bounded HTTP/wait
    // completion before replacement. Never overlap to bypass a failed cancel.
  }
}

void TelegramConnectionManager::stop_polling() {
  is_polling_ = false;
  pending_poll_.reset();
  cancel_poll();
}

void TelegramConnectionManager::poll_finished(const std::shared_ptr<Poll> &poll,
                                              std::exception_ptr error) {
  std::lock_guard guard(event_mutex_);
  if (active_poll_ != poll)
    return;
  active_poll_.reset(); // Join: the old HTTP coroutine has fully returned.
  try {
    if (pending_poll_) {
      // Only explicit newer lifecycle work may replace a superseded poll.
      launch_pending_poll();
    } else if (!error && poll->rearm && is_polling_ &&
               receive_current(poll->receive)) {
      start_polling(); // Genuinely new request, with its own origin/cursor.
    }
  } catch (...) {
    OBCX_WARN("Telegram poll rearm unavailable");
  }
}

auto TelegramConnectionManager::poll_updates(std::shared_ptr<Poll> poll)
    -> asio::awaitable<void> {
  bool should_delay = false;
  try {
    {
      std::lock_guard guard(event_mutex_);
      if (active_poll_ != poll || !is_polling_ ||
          !receive_current(poll->receive)) {
        poll->rearm = false;
        co_return;
      }
      poll->receive.client->set_timeout(poll->config.poll_force_close);
    }
    std::map<std::string, std::string> headers{
        {"User-Agent", "Mozilla/5.0 (X11; Linux x86_64; rv:147.0) "
                       "Gecko/20100101 Firefox/147.0"},
        {"Content-Type", "application/json"}};
    if (!poll->config.access_token.empty())
      headers["Authorization"] = "Bearer " + poll->config.access_token;
    const json params{
        {"offset", poll->offset},
        {"limit", 100},
        {"timeout", std::chrono::duration_cast<std::chrono::seconds>(
                        poll->config.poll_timeout)
                        .count()}};
    const auto body = params.dump();
    const auto path = "/bot" + poll->receive.access_token + "/getUpdates";
    const auto response =
        co_await poll->receive.client->post(path, body, headers);
    {
      std::lock_guard guard(event_mutex_);
      if (active_poll_ != poll || !is_polling_ ||
          !receive_current(poll->receive)) {
        poll->rearm = false;
        co_return;
      }
      if (response.is_success() && !response.body.empty())
        process_updates(response.body, poll);
    }
  } catch (const std::exception &) {
    {
      std::lock_guard guard(event_mutex_);
      if (active_poll_ != poll || !is_polling_ || !poll->rearm ||
          !receive_current(poll->receive)) {
        poll->rearm = false;
        co_return;
      }
      OBCX_WARN("Telegram polling failed; waiting for configured retry");
      poll->retry_timer.expires_after(poll->config.poll_retry_interval);
      should_delay = true;
    }
    // The wait is initiated below, outside the catch handler (C++ coroutine
    // restriction), with the same frozen origin and poll ownership.
  }
  if (should_delay) {
    try {
      co_await poll->retry_timer.async_wait(asio::use_awaitable);
    } catch (const boost::system::system_error &) {
      std::lock_guard guard(event_mutex_);
      poll->rearm = false;
    }
  }
}

void TelegramConnectionManager::process_updates(
    std::string_view updates_json, const std::shared_ptr<Poll> &poll) {
  if (active_poll_ != poll || !is_polling_ || !receive_current(poll->receive)) {
    poll->rearm = false;
    return;
  }
  try {
    const auto document = json::parse(updates_json);
    if (!document.contains("result") || !document.at("result").is_array())
      return;
    const auto &updates = document.at("result");
    std::optional<std::int64_t> next_offset;
    if (!updates.empty() && updates.back().contains("update_id"))
      next_offset =
          static_cast<std::int64_t>(obcx::bot::detail::bounded_unsigned(
              updates.back().at("update_id"), INT64_MAX - 1)) +
          1;
    // Retain the predecessor's batch acknowledgement order, but atomically
    // check ORIGINAL authority before advancing even a single offset. Native
    // ownership is locked throughout. A subsequent admitted account transition
    // resets this old cursor before releasing the lock or rearming fresh I/O.
    const auto committed = identity_->observe_user_and_capture(
        poll->receive.authority, std::nullopt, [&]() noexcept {
          if (next_offset)
            update_offset_ = *next_offset;
        });
    if (!committed.accepted()) {
      poll->rearm = false;
      return;
    }
    for (const auto &update : updates) {
      if (!receive_current(poll->receive)) {
        poll->rearm = false;
        return;
      }
      const auto event = adapter_->parse_event(update.dump());
      if (!event || !event_callback_)
        continue; // Preserve the existing unsupported/malformed-update drop
                  // path.
      const auto observation = event_callback_(*event, poll->receive.authority);
      if (observation.status == core::SelfObservationStatus::Transitioned) {
        // Preserve committed transition even if event delivery closed
        // afterwards.
        retire_account();
        poll->rearm = false;
        return;
      }
      if (observation.status == core::SelfObservationStatus::RejectedOrigin ||
          observation.status == core::SelfObservationStatus::Closed) {
        poll->rearm = false;
        return;
      }
      // Notice publication's Accepted/null-origin is not cache authority;
      // neither cursor commit nor rearm relies on that compatibility result.
    }
  } catch (const std::exception &) {
    OBCX_WARN("Failed to parse Telegram update batch");
  }
}

} // namespace obcx::network
