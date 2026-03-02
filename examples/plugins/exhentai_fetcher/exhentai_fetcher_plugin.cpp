#include "exhentai_fetcher_plugin.hpp"
#include "exhentai_parser.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <common/config_loader.hpp>
#include <common/logger.hpp>
#include <common/message_type.hpp>
#include <core/tg_bot.hpp>
#include <fmt/format.h>
#include <network/proxy_http_client.hpp>

namespace plugins {
ExHentaiFetcherPlugin::ExHentaiFetcherPlugin() {
  PLUGIN_DEBUG(get_name(), "ExHentaiFetcherPlugin constructor called");
}

ExHentaiFetcherPlugin::~ExHentaiFetcherPlugin() {
  shutdown();
  PLUGIN_DEBUG(get_name(), "ExHentaiFetcherPlugin destructor called");
}

auto ExHentaiFetcherPlugin::get_name() const -> std::string {
  return "exhentai_fetcher";
}

auto ExHentaiFetcherPlugin::get_version() const -> std::string {
  return "1.0.0";
}

auto ExHentaiFetcherPlugin::get_description() const -> std::string {
  return "ExHentai page fetcher plugin with cookie-based authentication";
}

auto ExHentaiFetcherPlugin::initialize() -> bool {
  try {
    PLUGIN_INFO(get_name(), "Initializing ExHentai Fetcher Plugin...");

    if (!load_configuration()) {
      PLUGIN_ERROR(get_name(), "Failed to load plugin configuration");
      return false;
    }

    // Build connection config
    obcx::common::ConnectionConfig conn_cfg;
    conn_cfg.host = config_.host;
    conn_cfg.port = config_.port;
    conn_cfg.use_ssl = config_.use_ssl;
    conn_cfg.connect_timeout = std::chrono::milliseconds(config_.timeout_ms);

    // Create HTTP client (proxy-aware)
    if (config_.proxy_enabled && !config_.proxy_host.empty() &&
        config_.proxy_port > 0) {
      obcx::network::ProxyConfig proxy_cfg;
      proxy_cfg.host = config_.proxy_host;
      proxy_cfg.port = config_.proxy_port;

      if (config_.proxy_type == "https") {
        proxy_cfg.type = obcx::network::ProxyType::HTTPS;
      } else if (config_.proxy_type == "socks5") {
        proxy_cfg.type = obcx::network::ProxyType::SOCKS5;
      } else {
        proxy_cfg.type = obcx::network::ProxyType::HTTP;
      }

      if (!config_.proxy_username.empty()) {
        proxy_cfg.username = config_.proxy_username;
      }
      if (!config_.proxy_password.empty()) {
        proxy_cfg.password = config_.proxy_password;
      }

      http_client_ = std::make_unique<obcx::network::ProxyHttpClient>(
          http_ioc_, proxy_cfg, conn_cfg);
      PLUGIN_INFO(get_name(), "Using proxy {}:{} ({})", config_.proxy_host,
                  config_.proxy_port, config_.proxy_type);
    } else {
      http_client_ =
          std::make_unique<obcx::network::HttpClient>(http_ioc_, conn_cfg);
    }

    // Keep http_ioc_ alive and run it in a background thread
    work_guard_ = std::make_unique<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>>(
        boost::asio::make_work_guard(http_ioc_));

    http_ioc_thread_ = std::thread([this]() { http_ioc_.run(); });

    // Register Telegram bot event callback
    try {
      auto [lock, bots] = get_bots();

      for (auto &bot_ptr : bots) {
        if (auto *tg_bot = dynamic_cast<obcx::core::TGBot *>(bot_ptr.get())) {
          tg_bot->on_event<obcx::common::MessageEvent>(
              [this](obcx::core::IBot &bot,
                     const obcx::common::MessageEvent &event)
                  -> boost::asio::awaitable<void> {
                co_await handle_tg_message(bot, event);
              });
          PLUGIN_INFO(get_name(),
                      "Registered Telegram message callback for ExHentai "
                      "Fetcher");
          break;
        }
      }
    } catch (const std::exception &e) {
      PLUGIN_ERROR(get_name(), "Failed to register callbacks: {}", e.what());
      return false;
    }

    PLUGIN_INFO(get_name(), "ExHentai Fetcher Plugin initialized successfully");
    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(),
                 "Exception during ExHentai Fetcher Plugin initialization: {}",
                 e.what());
    return false;
  }
}

void ExHentaiFetcherPlugin::deinitialize() {
  PLUGIN_INFO(get_name(), "Deinitializing ExHentai Fetcher Plugin...");
}

void ExHentaiFetcherPlugin::shutdown() {
  try {
    PLUGIN_INFO(get_name(), "Shutting down ExHentai Fetcher Plugin...");

    work_guard_.reset(); // Allow ioc to drain
    http_ioc_.stop();
    if (http_ioc_thread_.joinable()) {
      http_ioc_thread_.join();
    }
    thumbnail_clients_.clear();
    http_client_.reset();

    PLUGIN_INFO(get_name(), "ExHentai Fetcher Plugin shutdown complete");
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(),
                 "Exception during ExHentai Fetcher Plugin shutdown: {}",
                 e.what());
  }
}

bool ExHentaiFetcherPlugin::load_configuration() {
  try {
    PLUGIN_INFO(get_name(), "Loading exhentai_fetcher plugin configuration...");

    auto &config_loader = obcx::common::ConfigLoader::instance();
    auto plugin_config = config_loader.get_plugin_config(get_name());
    if (!plugin_config) {
      PLUGIN_ERROR(get_name(), "Plugin config section not found for plugin: {}",
                   get_name());
      return false;
    }

    // Required auth cookies
    auto ipb_member_id = get_config_value<std::string>("ipb_member_id");
    if (!ipb_member_id.has_value() || ipb_member_id->empty()) {
      PLUGIN_ERROR(get_name(), "ipb_member_id is required but not set");
      return false;
    }
    config_.ipb_member_id = *ipb_member_id;

    auto ipb_pass_hash = get_config_value<std::string>("ipb_pass_hash");
    if (!ipb_pass_hash.has_value() || ipb_pass_hash->empty()) {
      PLUGIN_ERROR(get_name(), "ipb_pass_hash is required but not set");
      return false;
    }
    config_.ipb_pass_hash = *ipb_pass_hash;

    auto igneous = get_config_value<std::string>("igneous");
    if (!igneous.has_value() || igneous->empty()) {
      PLUGIN_ERROR(get_name(), "igneous is required but not set");
      return false;
    }
    config_.igneous = *igneous;

    // HTTP target
    auto host = get_config_value<std::string>("host");
    config_.host = host.value_or("exhentai.org/watched");

    auto port = get_config_value<int64_t>("port");
    config_.port = static_cast<uint16_t>(port.value_or(443));

    auto use_ssl = get_config_value<bool>("use_ssl");
    config_.use_ssl = use_ssl.value_or(true);

    // Proxy
    auto proxy_enabled = get_config_value<bool>("proxy_enabled");
    config_.proxy_enabled = proxy_enabled.value_or(false);

    auto proxy_host = get_config_value<std::string>("proxy_host");
    config_.proxy_host = proxy_host.value_or("");

    auto proxy_port = get_config_value<int64_t>("proxy_port");
    config_.proxy_port = static_cast<uint16_t>(proxy_port.value_or(0));

    auto proxy_type = get_config_value<std::string>("proxy_type");
    config_.proxy_type = proxy_type.value_or("http");

    auto proxy_username = get_config_value<std::string>("proxy_username");
    config_.proxy_username = proxy_username.value_or("");

    auto proxy_password = get_config_value<std::string>("proxy_password");
    config_.proxy_password = proxy_password.value_or("");

    // Command trigger
    auto command = get_config_value<std::string>("command");
    config_.command = command.value_or("/exhentai");

    // Timeout
    auto timeout_ms = get_config_value<int64_t>("timeout_ms");
    config_.timeout_ms = timeout_ms.value_or(30000);

    // Max galleries
    auto max_galleries = get_config_value<int64_t>("max_galleries");
    config_.max_galleries = static_cast<int>(max_galleries.value_or(10));

    PLUGIN_INFO(get_name(),
                "Configuration loaded: host={}:{}, proxy={}, "
                "command={}",
                config_.host, config_.port,
                config_.proxy_enabled ? "enabled" : "disabled",
                config_.command);
    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to load configuration: {}", e.what());
    return false;
  }
}

std::map<std::string, std::string> ExHentaiFetcherPlugin::build_headers()
    const {
  return {
      {"User-Agent",
       "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:148.0) Gecko/20100101 "
       "Firefox/148.0"},
      {"Accept",
       "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,"
       "image/webp,image/png,image/svg+xml,*/*;q=0.8"},
      {"Accept-Language", "en-US,en;q=0.5"},
      {"Accept-Encoding", "gzip, deflate, br, zstd"},
      {"DNT", "1"},
      {"Sec-GPC", "1"},
      {"Connection", "keep-alive"},
      {"Cookie", fmt::format("ipb_member_id={}; ipb_pass_hash={}; igneous={}",
                             config_.ipb_member_id, config_.ipb_pass_hash,
                             config_.igneous)},
      {"Upgrade-Insecure-Requests", "1"},
      {"Sec-Fetch-Dest", "document"},
      {"Sec-Fetch-Mode", "navigate"},
      {"Sec-Fetch-Site", "none"},
      {"Sec-Fetch-User", "?1"},
      {"Priority", "u=0, i"},
      // Suppress the Pragma/Cache-Control: no-cache defaults injected by
      // prepare_request — a real browser browse request does not send these.
      {"Pragma", ""},
      {"Cache-Control", ""},
  };
}

std::map<std::string, std::string>
ExHentaiFetcherPlugin::build_thumbnail_headers() const {
  return {
      {"User-Agent",
       "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:148.0) Gecko/20100101 "
       "Firefox/148.0"},
      {"Accept", "image/avif,image/webp,image/png,image/svg+xml,*/*;q=0.8"},
      {"Accept-Language", "en-US,en;q=0.5"},
      {"Accept-Encoding", "gzip, deflate, br, zstd"},
      {"Cookie", fmt::format("ipb_member_id={}; ipb_pass_hash={}; igneous={}",
                             config_.ipb_member_id, config_.ipb_pass_hash,
                             config_.igneous)},
      {"Referer", "https://exhentai.org/"},
      {"Pragma", ""},
      {"Cache-Control", ""},
  };
}

obcx::network::HttpClient &ExHentaiFetcherPlugin::get_thumbnail_client(
    const std::string &host, bool use_ssl) {
  auto it = thumbnail_clients_.find(host);
  if (it != thumbnail_clients_.end()) {
    return *it->second;
  }

  obcx::common::ConnectionConfig conn_cfg;
  conn_cfg.host = host;
  conn_cfg.port = use_ssl ? 443 : 80;
  conn_cfg.use_ssl = use_ssl;
  conn_cfg.connect_timeout = std::chrono::milliseconds(config_.timeout_ms);

  std::unique_ptr<obcx::network::HttpClient> client;

  if (config_.proxy_enabled && !config_.proxy_host.empty() &&
      config_.proxy_port > 0) {
    obcx::network::ProxyConfig proxy_cfg;
    proxy_cfg.host = config_.proxy_host;
    proxy_cfg.port = config_.proxy_port;
    if (config_.proxy_type == "https") {
      proxy_cfg.type = obcx::network::ProxyType::HTTPS;
    } else if (config_.proxy_type == "socks5") {
      proxy_cfg.type = obcx::network::ProxyType::SOCKS5;
    } else {
      proxy_cfg.type = obcx::network::ProxyType::HTTP;
    }
    if (!config_.proxy_username.empty()) {
      proxy_cfg.username = config_.proxy_username;
    }
    if (!config_.proxy_password.empty()) {
      proxy_cfg.password = config_.proxy_password;
    }
    client = std::make_unique<obcx::network::ProxyHttpClient>(
        http_ioc_, proxy_cfg, conn_cfg);
  } else {
    client = std::make_unique<obcx::network::HttpClient>(http_ioc_, conn_cfg);
  }

  auto &ref = *client;
  thumbnail_clients_.emplace(host, std::move(client));
  return ref;
}

auto ExHentaiFetcherPlugin::fetch_page(std::string_view path) const
    -> boost::asio::awaitable<obcx::network::HttpResponse> {
  auto headers = build_headers();
  auto response = co_await http_client_->get(path, headers);
  PLUGIN_INFO(get_name(), "ExHentai GET {} -> HTTP {}", path,
              response.status_code);
  co_return response;
}

// Parse the absolute URL to extract host and path.
// Returns {host, path, use_ssl} or empty strings on failure.
static std::tuple<std::string, std::string, bool> parse_url(
    const std::string &url) {
  bool use_ssl = false;
  std::string_view rest = url;
  if (rest.starts_with("https://")) {
    use_ssl = true;
    rest.remove_prefix(8);
  } else if (rest.starts_with("http://")) {
    rest.remove_prefix(7);
  } else {
    return {};
  }
  auto slash = rest.find('/');
  if (slash == std::string_view::npos) {
    return {std::string(rest), "/", use_ssl};
  }
  return {std::string(rest.substr(0, slash)), std::string(rest.substr(slash)),
          use_ssl};
}

auto ExHentaiFetcherPlugin::handle_tg_message(
    obcx::core::IBot &bot, const obcx::common::MessageEvent &event)
    -> boost::asio::awaitable<void> {
  if (!event.raw_message.starts_with(config_.command)) {
    co_return;
  }

  std::string chat_id =
      event.group_id.has_value() ? event.group_id.value() : event.user_id;
  std::optional<int64_t> topic_id;
  // topic_id is not available in the generic MessageEvent; left as nullopt

  PLUGIN_INFO(get_name(), "Received {} command from chat {}", config_.command,
              chat_id);

  auto send_text =
      [&](const std::string &text) -> boost::asio::awaitable<void> {
    obcx::common::Message reply = {
        {{.type = {"text"}, .data = {{"text", text}}}}};
    if (event.group_id.has_value()) {
      co_await bot.send_group_message(event.group_id.value(), reply);
    } else {
      co_await bot.send_private_message(event.user_id, reply);
    }
  };

  // Extract optional path from command (e.g. "/exhentai /watched")
  std::string path = "/";
  if (event.raw_message.size() > config_.command.size()) {
    std::string arg = event.raw_message.substr(config_.command.size());
    auto start = arg.find_first_not_of(" \t");
    if (start != std::string::npos) {
      path = arg.substr(start);
    }
  }

  std::string outer_error;

  // Fetch + parse
  std::vector<GalleryEntry> galleries;
  obcx::network::HttpResponse page_response;
  try {
    page_response = co_await fetch_page(path);
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Error fetching page: {}", e.what());
    outer_error = fmt::format("Error: {}", e.what());
  }

  if (!outer_error.empty()) {
    co_await send_text(outer_error);
    co_return;
  }

  if (!page_response.is_success()) {
    co_await send_text(fmt::format("HTTP error {}", page_response.status_code));
    co_return;
  }

  galleries = ExHentaiParser::parse(page_response.body);
  if (galleries.empty()) {
    co_await send_text("No galleries found.");
    co_return;
  }

  auto *tg_bot = dynamic_cast<obcx::core::TGBot *>(&bot);

  int count = 0;
  for (const auto &gallery : galleries) {
    if (count >= config_.max_galleries) {
      break;
    }
    ++count;

    // Build caption
    std::string tags_str;
    for (std::size_t i = 0; i < gallery.tags.size(); ++i) {
      if (i > 0) {
        tags_str += ", ";
      }
      tags_str += gallery.tags[i];
    }
    std::string caption =
        fmt::format("{}\nTags: {}\n{}", gallery.title, tags_str, gallery.url);

    // Try to download thumbnail and send as photo
    bool sent_photo = false;
    if (tg_bot && !gallery.thumbnail_url.empty() &&
        (gallery.thumbnail_url.starts_with("http://") ||
         gallery.thumbnail_url.starts_with("https://"))) {
      auto [thumb_host, thumb_path, thumb_ssl] =
          parse_url(gallery.thumbnail_url);
      if (!thumb_host.empty()) {
        bool thumb_ok = true;
        obcx::network::HttpResponse thumb_resp;
        try {
          auto &thumb_client = get_thumbnail_client(thumb_host, thumb_ssl);
          auto thumb_headers = build_thumbnail_headers();
          thumb_resp = co_await thumb_client.get(thumb_path, thumb_headers);
        } catch (const std::exception &e) {
          PLUGIN_WARN(get_name(), "Thumbnail download failed: {}", e.what());
          thumb_ok = false;
        }

        if (thumb_ok && thumb_resp.is_success() && !thumb_resp.body.empty()) {
          // Detect mime type from URL extension
          std::string mime = "image/jpeg";
          std::string fname = "thumb.jpg";
          if (gallery.thumbnail_url.ends_with(".webp")) {
            mime = "image/webp";
            fname = "thumb.webp";
          } else if (gallery.thumbnail_url.ends_with(".png")) {
            mime = "image/png";
            fname = "thumb.png";
          } else if (gallery.thumbnail_url.ends_with(".gif")) {
            mime = "image/gif";
            fname = "thumb.gif";
          }

          bool upload_ok = true;
          try {
            co_await tg_bot->send_photo_bytes(chat_id, thumb_resp.body, fname,
                                              mime, caption, topic_id);
          } catch (const std::exception &e) {
            PLUGIN_WARN(get_name(), "Thumbnail upload failed: {}", e.what());
            upload_ok = false;
          }
          if (upload_ok) {
            sent_photo = true;
          }
        }
      }
    }

    if (!sent_photo) {
      co_await send_text(caption);
    }
  }

  const auto posted = static_cast<std::size_t>(count);
  co_await send_text(fmt::format("Fetched {} galleries.", posted));

  co_return;
}
} // namespace plugins

OBCX_PLUGIN_EXPORT(plugins::ExHentaiFetcherPlugin)
