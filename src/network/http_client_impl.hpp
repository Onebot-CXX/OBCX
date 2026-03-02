#pragma once

// Internal header: defines HttpClient::Impl and prepare_request template.
// Included by http_client.cpp and http_client_sync.cpp only.

#include "network/http_client.hpp"

#include <atomic>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/ssl.hpp>
#include <optional>

namespace obcx::network {

namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

struct HttpClient::Impl {
  asio::io_context &ioc;
  common::ConnectionConfig config;
  std::optional<ssl::context> ssl_ctx;
  std::atomic<bool> connected{false};

  Impl(asio::io_context &io, common::ConnectionConfig cfg)
      : ioc(io), config(std::move(cfg)) {
    if (config.port == 443 || config.use_ssl) {
      ssl_ctx.emplace(ssl::context::tls_client);
      ssl_ctx->set_verify_mode(ssl::verify_none);
    }
  }
};

template <typename RequestType>
void HttpClient::prepare_request(
    RequestType &request, const std::map<std::string, std::string> &headers) {
  if (!request.count(http::field::user_agent)) {
    request.set(http::field::user_agent,
                "Mozilla/5.0 (X11; Linux x86_64; rv:142.0) Gecko/20100101 "
                "Firefox/142.0");
  }

  if (!request.count(http::field::accept)) {
    request.set(
        http::field::accept,
        "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
  }

  if (!request.count(http::field::accept_language)) {
    request.set(http::field::accept_language, "en-US,en;q=0.5");
  }

  if (!request.count(http::field::accept_encoding)) {
    request.set(http::field::accept_encoding, "gzip, deflate, br, zstd");
  }

  if (!request.count("DNT")) {
    request.set("DNT", "1");
  }

  if (!request.count("Sec-GPC")) {
    request.set("Sec-GPC", "1");
  }

  if (!request.count(http::field::connection)) {
    request.set(http::field::connection, "keep-alive");
  }

  if (!request.count("Upgrade-Insecure-Requests")) {
    request.set("Upgrade-Insecure-Requests", "1");
  }

  if (!request.count("Sec-Fetch-Dest")) {
    request.set("Sec-Fetch-Dest", "document");
  }

  if (!request.count("Sec-Fetch-Mode")) {
    request.set("Sec-Fetch-Mode", "navigate");
  }

  if (!request.count("Sec-Fetch-Site")) {
    request.set("Sec-Fetch-Site", "cross-site");
  }

  if (!request.count("Priority")) {
    request.set("Priority", "u=0, i");
  }

  if (!request.count(http::field::pragma)) {
    request.set(http::field::pragma, "no-cache");
  }

  if (!request.count(http::field::cache_control)) {
    request.set(http::field::cache_control, "no-cache");
  }

  if (!request.count(http::field::content_type) && !request.body().empty()) {
    request.set(http::field::content_type, "application/json");
  }

  if (!pimpl_->config.access_token.empty()) {
    request.set(http::field::authorization,
                "Bot " + pimpl_->config.access_token);
  }

  for (const auto &[key, value] : headers) {
    request.set(key, value);
  }
}

} // namespace obcx::network
