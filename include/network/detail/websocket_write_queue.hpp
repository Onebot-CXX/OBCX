#ifndef OBCX_INCLUDE_NETWORK_DETAIL_WEBSOCKET_WRITE_QUEUE_HPP_
#define OBCX_INCLUDE_NETWORK_DETAIL_WEBSOCKET_WRITE_QUEUE_HPP_

#include <atomic>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace obcx::network::detail {

namespace asio = boost::asio;

class WebsocketWriteQueue final {
public:
  using WriteOperation =
      std::function<asio::awaitable<void>(const std::string &)>;
  using CancelOperation = std::function<void()>;

  WebsocketWriteQueue(asio::any_io_executor executor, std::size_t capacity,
                      WriteOperation write_operation,
                      CancelOperation cancel_operation = {});
  ~WebsocketWriteQueue();

  WebsocketWriteQueue(const WebsocketWriteQueue &) = delete;
  auto operator=(const WebsocketWriteQueue &) -> WebsocketWriteQueue & = delete;

  auto send(std::string message) -> asio::awaitable<void>;
  auto run() -> asio::awaitable<void>;
  void stop(std::exception_ptr failure = {});

  [[nodiscard]] auto stopped() const noexcept -> bool {
    return stopped_.load(std::memory_order_acquire);
  }

private:
  struct WriteRequest;
  using RequestChannel = asio::experimental::concurrent_channel<void(
      boost::system::error_code, std::shared_ptr<WriteRequest>)>;

  [[nodiscard]] static auto cancellation_failure() -> std::exception_ptr;
  [[nodiscard]] auto terminal_failure() const -> std::exception_ptr;
  void set_active(std::shared_ptr<WriteRequest> request);
  void clear_active(const std::shared_ptr<WriteRequest> &request);
  void drain_queued(const std::exception_ptr &failure);

  asio::any_io_executor executor_;
  RequestChannel requests_;
  WriteOperation write_operation_;
  CancelOperation cancel_operation_;
  std::atomic_bool stopped_{false};
  mutable std::mutex state_mutex_;
  std::exception_ptr terminal_failure_;
  std::shared_ptr<WriteRequest> active_;
};

} // namespace obcx::network::detail

#endif // OBCX_INCLUDE_NETWORK_DETAIL_WEBSOCKET_WRITE_QUEUE_HPP_
