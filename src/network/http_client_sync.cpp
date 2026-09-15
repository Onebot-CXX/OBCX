#include "common/logger.hpp"
#include "network/http_client.hpp"

#include "http_client_impl.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <utility>

namespace obcx::network {

namespace {

template <typename Operation, typename Cleanup>
auto run_sync(Operation &&operation, Cleanup &&cleanup) -> HttpResponse {
  asio::io_context local_ioc;
  auto result = asio::co_spawn(local_ioc, std::forward<Operation>(operation),
                               asio::use_future);
  local_ioc.run();
  std::forward<Cleanup>(cleanup)();
  return result.get();
}

} // namespace

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

auto HttpClient::post_sync(std::string_view path, std::string_view body,
                           const std::map<std::string, std::string> &headers)
    -> HttpResponse {
  return run_sync(post(path, body, headers),
                  [this] { pimpl_->close_drivers(); });
}

auto HttpClient::get_sync(std::string_view path,
                          const std::map<std::string, std::string> &headers)
    -> HttpResponse {
  return run_sync(get(path, headers), [this] { pimpl_->close_drivers(); });
}

auto HttpClient::head_sync(std::string_view path,
                           const std::map<std::string, std::string> &headers)
    -> HttpResponse {
  return run_sync(head(path, headers), [this] { pimpl_->close_drivers(); });
}

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

} // namespace obcx::network
