#pragma once

#include "interfaces/plugin.hpp"
#include "network/http_client.hpp"

#include <boost/asio.hpp>
#include <map>
#include <memory>
#include <string>
#include <thread>

namespace obcx::core {
class TGBot;
} // namespace obcx::core

namespace plugins {

class ExHentaiFetcherPlugin : public obcx::interface::IPlugin {
public:
  ExHentaiFetcherPlugin();
  ~ExHentaiFetcherPlugin() override;

  std::string get_name() const override;
  std::string get_version() const override;
  std::string get_description() const override;
  bool initialize() override;
  void deinitialize() override;
  void shutdown() override;

private:
  struct Config {
    // ExHentai auth cookies
    std::string ipb_member_id;
    std::string ipb_pass_hash;
    std::string igneous;

    // HTTP target
    std::string host = "exhentai.org";
    uint16_t port = 443;
    bool use_ssl = true;

    // Proxy
    bool proxy_enabled = false;
    std::string proxy_host;
    uint16_t proxy_port = 0;
    std::string proxy_type = "http";
    std::string proxy_username;
    std::string proxy_password;

    // Trigger command
    std::string command = "/exhentai";

    // Timeout in milliseconds
    int64_t timeout_ms = 30000;

    // Max galleries to post per request
    int max_galleries = 10;
  };

  bool load_configuration();
  std::map<std::string, std::string> build_headers() const;
  std::map<std::string, std::string> build_thumbnail_headers() const;

  // Create or retrieve a cached HTTP client for the given hostname
  obcx::network::HttpClient &get_thumbnail_client(const std::string &host,
                                                  bool use_ssl);

  boost::asio::awaitable<obcx::network::HttpResponse> fetch_page(
      std::string_view path) const;

  boost::asio::awaitable<void> handle_tg_message(
      obcx::core::IBot &bot, const obcx::common::MessageEvent &event);

  Config config_;

  // Primary HTTP client (exhentai.org)
  boost::asio::io_context http_ioc_;
  std::unique_ptr<obcx::network::HttpClient> http_client_;
  std::unique_ptr<
      boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>
      work_guard_;
  std::thread http_ioc_thread_;

  // Thumbnail HTTP clients keyed by hostname
  std::map<std::string, std::unique_ptr<obcx::network::HttpClient>>
      thumbnail_clients_;
};

} // namespace plugins
