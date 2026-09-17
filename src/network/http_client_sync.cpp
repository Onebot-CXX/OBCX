#include "network/http_client.hpp"

#include "http_client_impl.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <utility>

namespace obcx::network {

auto HttpClientState::perform_sync(std::shared_ptr<HttpClientState> self,
                                   const detail::CurlHttpMethod method,
                                   std::string path, std::string body,
                                   std::map<std::string, std::string> headers)
    -> HttpResponse {
  const auto settings = self->settings();
  asio::io_context local_ioc;
  auto local = std::make_shared<HttpClientState>(local_ioc.get_executor(),
                                                 settings.config);
  local->proxy = settings.proxy;
  local->response_body_limit = settings.response_body_limit;
  auto result =
      asio::co_spawn(local_ioc,
                     perform(local, method, std::move(path), std::move(body),
                             std::move(headers), std::nullopt),
                     asio::use_future);
  local_ioc.run();
  local->close();
  local_ioc.restart();
  local_ioc.run();
  // Get only after cleanup, including on timeout/provider failure.
  try {
    auto response = result.get();
    self->mark_connected();
    return response;
  } catch (...) {
    self->connected = false;
    throw;
  }
}

auto HttpClient::post_sync(std::string_view path, std::string_view body,
                           const std::map<std::string, std::string> &headers)
    -> HttpResponse {
  return HttpClientState::perform_sync(
      pimpl_->state, detail::CurlHttpMethod::Post, std::string{path},
      std::string{body}, headers);
}

auto HttpClient::get_sync(std::string_view path,
                          const std::map<std::string, std::string> &headers)
    -> HttpResponse {
  return HttpClientState::perform_sync(pimpl_->state,
                                       detail::CurlHttpMethod::Get,
                                       std::string{path}, {}, headers);
}

auto HttpClient::head_sync(std::string_view path,
                           const std::map<std::string, std::string> &headers)
    -> HttpResponse {
  return HttpClientState::perform_sync(pimpl_->state,
                                       detail::CurlHttpMethod::Head,
                                       std::string{path}, {}, headers);
}

} // namespace obcx::network
