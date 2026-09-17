#include "network/websocket_client.hpp"
#include "common/logger.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/errc.hpp>
#include <utility>

namespace obcx::network {

WebsocketClient::WebsocketClient(asio::io_context &ioc) : ws_(ioc) {}

auto WebsocketClient::run(std::string host, std::string port,
                          std::string access_token, MessageHandler on_message)
    -> asio::awaitable<void> {
  host_ = std::move(host);
  access_token_ = std::move(access_token);
  on_message_ = std::move(on_message);
  auto port_str = port;

  try {
    tcp::resolver resolver(co_await asio::this_coro::executor);
    auto const results =
        co_await resolver.async_resolve(host_, port_str, asio::use_awaitable);

    auto &lowest_layer = beast::get_lowest_layer(ws_);
    lowest_layer.expires_after(std::chrono::seconds(30));
    co_await lowest_layer.async_connect(results, asio::use_awaitable);

    lowest_layer.expires_never();
    ws_.set_option(websocket::stream_base::decorator(
        [this](websocket::request_type &req) -> void {
          if (!access_token_.empty()) {
            req.set(beast::http::field::authorization,
                    "Bearer " + access_token_);
          }
          req.set(beast::http::field::host, host_);
          req.set(beast::http::field::user_agent, "OBCX-Framework");
        }));
    co_await ws_.async_handshake(host_, "/", asio::use_awaitable);

    OBCX_INFO("WebSocket connected successfully to ws://{}:{}", host_,
              port_str);

    start_writer();

    // Notify upper layer of successful connection with an empty error code.
    on_message_({}, "");

    while (ws_.is_open()) {
      buffer_.clear();
      co_await ws_.async_read(buffer_, asio::use_awaitable);
      on_message_({}, beast::buffers_to_string(buffer_.data()));
    }
  } catch (const beast::system_error &se) {
    if (se.code() != websocket::error::closed &&
        se.code() != asio::error::operation_aborted) {
      OBCX_ERROR("WebSocket run error: {}", se.what());
    }
    on_message_(se.code(), "");
  } catch (const std::exception &e) {
    OBCX_CRITICAL("WebSocket caught unhandled exception: {}", e.what());
    beast::error_code ec = asio::error::fault;
    on_message_(ec, "");
  }

  stop_writer();
  OBCX_WARN("WebSocket connection closed.");
}

auto WebsocketClient::send(std::string message) -> asio::awaitable<void> {
  if (!ws_.is_open()) {
    OBCX_WARN("WebSocket not connected, cannot send message.");
    co_return;
  }

  const auto queue = write_queue_;
  if (!queue) {
    throw boost::system::system_error{asio::error::operation_aborted};
  }
  co_await queue->send(std::move(message));
}

auto WebsocketClient::close() -> asio::awaitable<void> {
  if (ws_.is_open()) {
    try {
      co_await ws_.async_close(websocket::close_code::normal,
                               asio::use_awaitable);
    } catch (const beast::system_error &se) {
      if (se.code() != websocket::error::closed) {
        OBCX_ERROR("WebSocket close error: {}", se.what());
      }
    } catch (const std::exception &e) {
      OBCX_ERROR("WebSocket caught unhandled exception during close: {}",
                 e.what());
    }
  }
}

void WebsocketClient::start_writer() {
  auto queue = std::make_shared<detail::WebsocketWriteQueue>(
      ws_.get_executor(), 100,
      [this](const std::string &message) -> asio::awaitable<void> {
        co_await ws_.async_write(asio::buffer(message), asio::use_awaitable);
        OBCX_DEBUG("WebSocket message sent: bytes={}", message.size());
      },
      [this] {
        beast::error_code error;
        static_cast<void>(beast::get_lowest_layer(ws_).socket().cancel(
            error)); // NOLINT(bugprone-unused-return-value)
      });
  write_queue_ = queue;
  asio::co_spawn(
      ws_.get_executor(),
      [queue = std::move(queue)]() -> asio::awaitable<void> {
        co_await queue->run();
      },
      asio::detached);
}

void WebsocketClient::stop_writer() {
  if (auto queue = std::exchange(write_queue_, nullptr)) {
    queue->stop();
  }
}

} // namespace obcx::network
