#ifndef OBCX_SRC_NETWORK_HTTP_CLIENT_IMPL_HPP_
#define OBCX_SRC_NETWORK_HTTP_CLIENT_IMPL_HPP_

#include "curl_asio_multi.hpp"
#include "network/http_client.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace obcx::network {

struct HttpClient::Impl {
  common::ConnectionConfig config;
  std::atomic<bool> connected{false};
  std::uint64_t response_body_limit{HttpClient::kDefaultResponseBodyLimit};
  std::optional<detail::CurlProxySettings> proxy;

  explicit Impl(common::ConnectionConfig cfg) : config(std::move(cfg)) {}

  ~Impl() { close_drivers(); }

  auto driver_for(const asio::any_io_executor &executor)
      -> std::shared_ptr<detail::CurlAsioMulti> {
    std::lock_guard lock(drivers_mutex);
    for (const auto &[candidate, driver] : drivers) {
      if (candidate == executor) {
        return driver;
      }
    }
    auto driver = std::make_shared<detail::CurlAsioMulti>(executor);
    drivers.emplace_back(executor, driver);
    return driver;
  }

  void close_drivers() {
    std::vector<DriverEntry> current;
    {
      std::lock_guard lock(drivers_mutex);
      current.swap(drivers);
    }
    for (const auto &[executor, driver] : current) {
      (void)executor;
      driver->shutdown();
    }
  }

private:
  using DriverEntry =
      std::pair<asio::any_io_executor, std::shared_ptr<detail::CurlAsioMulti>>;

  std::mutex drivers_mutex;
  std::vector<DriverEntry> drivers;
};

} // namespace obcx::network

#endif // OBCX_SRC_NETWORK_HTTP_CLIENT_IMPL_HPP_
