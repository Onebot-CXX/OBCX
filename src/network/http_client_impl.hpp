#ifndef OBCX_SRC_NETWORK_HTTP_CLIENT_IMPL_HPP_
#define OBCX_SRC_NETWORK_HTTP_CLIENT_IMPL_HPP_

#include "curl_asio_multi.hpp"
#include "network/http_client.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <utility>

namespace obcx::network {

struct HttpClientState {
  struct Settings {
    common::ConnectionConfig config;
    std::optional<detail::CurlProxySettings> proxy;
    std::uint64_t response_body_limit;
  };

  HttpClientState(asio::any_io_executor executor, common::ConnectionConfig cfg)
      : config(std::move(cfg)), executor_(std::move(executor)) {}

  ~HttpClientState() { close(); }

  auto settings() const -> Settings {
    std::lock_guard lock(mutex);
    if (closed_) {
      throw HttpClientError("HTTP client is closed",
                            HttpRequestSubmissionState::DefinitelyNotSubmitted);
    }
    return {config, proxy, response_body_limit};
  }

  auto driver() -> std::shared_ptr<detail::CurlAsioMulti> {
    std::lock_guard lock(mutex);
    if (closed_) {
      throw HttpClientError("HTTP client is closed",
                            HttpRequestSubmissionState::DefinitelyNotSubmitted);
    }
    if (!driver_) {
      driver_ = std::make_shared<detail::CurlAsioMulti>(executor_);
    }
    return driver_;
  }

  void mark_connected() {
    std::lock_guard lock(mutex);
    connected = !closed_;
  }

  void close() {
    std::shared_ptr<detail::CurlAsioMulti> retired;
    {
      std::lock_guard lock(mutex);
      closed_ = true;
      connected = false;
      retired = std::move(driver_);
    }
    if (retired) {
      retired->shutdown();
    }
  }

  static auto perform(std::shared_ptr<HttpClientState> self,
                      detail::CurlHttpMethod method, std::string path,
                      std::string body,
                      std::map<std::string, std::string> headers,
                      std::optional<std::uint64_t> response_body_limit)
      -> asio::awaitable<HttpResponse>;
  static auto perform_sync(std::shared_ptr<HttpClientState> self,
                           detail::CurlHttpMethod method, std::string path,
                           std::string body,
                           std::map<std::string, std::string> headers)
      -> HttpResponse;

  mutable std::mutex mutex;
  common::ConnectionConfig config;
  std::atomic<bool> connected{false};
  std::uint64_t response_body_limit{HttpClient::kDefaultResponseBodyLimit};
  std::optional<detail::CurlProxySettings> proxy;

private:
  // This context belongs to the client owner, never to an awaiting caller.
  const asio::any_io_executor executor_;
  std::shared_ptr<detail::CurlAsioMulti> driver_;
  bool closed_ = false;
};

// Keep HttpClient's public single-pointer layout while requests lease state.
struct HttpClient::Impl {
  Impl(asio::any_io_executor executor, common::ConnectionConfig config)
      : state(std::make_shared<HttpClientState>(std::move(executor),
                                                std::move(config))) {}
  std::shared_ptr<HttpClientState> state;
};

} // namespace obcx::network

#endif // OBCX_SRC_NETWORK_HTTP_CLIENT_IMPL_HPP_
