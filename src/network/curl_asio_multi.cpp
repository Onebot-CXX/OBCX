#include "curl_asio_multi.hpp"

#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <curl/curl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

#if LIBCURL_VERSION_NUM < 0x075500
#error "OBCX requires libcurl 7.85 or newer for protocol string restrictions"
#endif

namespace obcx::network::detail {
namespace {

class CurlErrorCategory final : public boost::system::error_category {
public:
  [[nodiscard]] auto name() const noexcept -> const char * override {
    return "libcurl";
  }

  [[nodiscard]] auto message(const int value) const -> std::string override {
    return curl_easy_strerror(static_cast<CURLcode>(value));
  }
};

const CurlErrorCategory kCurlErrorCategory;

void ensure_curl_initialized() {
  static const auto result = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (result != CURLE_OK) {
    throw std::runtime_error("libcurl global initialization failed");
  }
}

[[nodiscard]] auto supports_protocol(const curl_version_info_data &info,
                                     const std::string_view protocol) -> bool {
  if (info.protocols == nullptr) {
    return false;
  }
  for (const char *const *entry = info.protocols; *entry != nullptr; ++entry) {
    if (protocol == *entry) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] auto checked_size(const std::size_t size, const std::size_t count)
    -> std::size_t {
  if (count != 0 && size > std::numeric_limits<std::size_t>::max() / count) {
    return std::numeric_limits<std::size_t>::max();
  }
  return size * count;
}

[[nodiscard]] auto equal_ascii_case_insensitive(std::string_view left,
                                                std::string_view right)
    -> bool {
  return left.size() == right.size() &&
         std::ranges::equal(left, right, [](const char lhs, const char rhs) {
           return std::tolower(static_cast<unsigned char>(lhs)) ==
                  std::tolower(static_cast<unsigned char>(rhs));
         });
}

} // namespace

auto curl_error_code(const int native_code) -> boost::system::error_code {
  return {native_code, kCurlErrorCategory};
}

auto curl_error_definitely_not_submitted(
    const boost::system::error_code &error) noexcept -> bool {
  if (&error.category() != &kCurlErrorCategory) {
    return false;
  }
  switch (static_cast<CURLcode>(error.value())) {
  case CURLE_UNSUPPORTED_PROTOCOL:
  case CURLE_FAILED_INIT:
  case CURLE_URL_MALFORMAT:
  case CURLE_NOT_BUILT_IN:
  case CURLE_COULDNT_RESOLVE_PROXY:
  case CURLE_COULDNT_RESOLVE_HOST:
  case CURLE_COULDNT_CONNECT:
  case CURLE_SSL_CONNECT_ERROR:
  case CURLE_PEER_FAILED_VERIFICATION:
  case CURLE_SSL_CERTPROBLEM:
  case CURLE_SSL_CIPHER:
  case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
    return true;
  default:
    return false;
  }
}

auto curl_runtime_capabilities() -> CurlRuntimeCapabilities {
  ensure_curl_initialized();
  const auto *info = curl_version_info(CURLVERSION_NOW);
  if (info == nullptr) {
    throw std::runtime_error("libcurl version information is unavailable");
  }

  auto probe = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>(
      curl_easy_init(), &curl_easy_cleanup);
  const auto socks5_hostname =
      probe != nullptr &&
      curl_easy_setopt(probe.get(), CURLOPT_PROXYTYPE,
                       CURLPROXY_SOCKS5_HOSTNAME) == CURLE_OK;

  return CurlRuntimeCapabilities{
      .version = info->version == nullptr ? std::string{} : info->version,
      .ssl_backend =
          info->ssl_version == nullptr ? std::string{} : info->ssl_version,
      .asynchronous_dns = (info->features & CURL_VERSION_ASYNCHDNS) != 0,
      .tls = (info->features & CURL_VERSION_SSL) != 0,
      .https_proxy = (info->features & CURL_VERSION_HTTPS_PROXY) != 0,
      .http = supports_protocol(*info, "http"),
      .https = supports_protocol(*info, "https"),
      .socks5_hostname = socks5_hostname,
  };
}

class CurlAsioMulti::Impl final
    : public std::enable_shared_from_this<CurlAsioMulti::Impl> {
public:
  using Completion =
      std::move_only_function<void(boost::system::error_code, CurlResponse)>;

  explicit Impl(boost::asio::any_io_executor executor)
      : strand_(boost::asio::make_strand(std::move(executor))),
        timer_(strand_) {
    const auto capabilities = curl_runtime_capabilities();
    if (!capabilities.asynchronous_dns || !capabilities.tls ||
        !capabilities.https_proxy || !capabilities.http ||
        !capabilities.https || !capabilities.socks5_hostname) {
      throw std::runtime_error(
          "libcurl lacks required asynchronous DNS, TLS, HTTP/HTTPS, or proxy "
          "capabilities");
    }

    multi_ = curl_multi_init();
    if (multi_ == nullptr) {
      throw std::runtime_error("libcurl multi initialization failed");
    }
    if (curl_multi_setopt(multi_, CURLMOPT_SOCKETFUNCTION,
                          &Impl::socket_callback) != CURLM_OK ||
        curl_multi_setopt(multi_, CURLMOPT_SOCKETDATA, this) != CURLM_OK ||
        curl_multi_setopt(multi_, CURLMOPT_TIMERFUNCTION,
                          &Impl::timer_callback) != CURLM_OK ||
        curl_multi_setopt(multi_, CURLMOPT_TIMERDATA, this) != CURLM_OK) {
      curl_multi_cleanup(multi_);
      multi_ = nullptr;
      throw std::runtime_error("libcurl multi callback setup failed");
    }
  }

  ~Impl() {
    try {
      (void)timer_.cancel();
    } catch (...) {
    }
    for (auto &[socket, watch] : sockets_) {
      (void)socket;
      watch->detach();
    }
    sockets_.clear();
    for (auto &[easy, transfer] : transfers_) {
      (void)transfer;
      curl_multi_remove_handle(multi_, easy);
    }
    transfers_.clear();
    if (multi_ != nullptr) {
      curl_multi_cleanup(multi_);
    }
  }

  [[nodiscard]] auto perform(CurlRequest request)
      -> boost::asio::awaitable<CurlResponse> {
    auto self = shared_from_this();
    auto token = boost::asio::use_awaitable;
    co_return co_await boost::asio::async_initiate<
        decltype(token), void(boost::system::error_code, CurlResponse)>(
        [self, request = std::move(request)](auto handler) mutable {
          auto executor = boost::asio::any_io_executor{
              boost::asio::get_associated_executor(handler, self->strand_)};
          auto cancellation =
              boost::asio::get_associated_cancellation_slot(handler);
          auto work = boost::asio::make_work_guard(executor);
          Completion completion = [executor, work = std::move(work),
                                   handler = std::move(handler)](
                                      boost::system::error_code error,
                                      CurlResponse response) mutable {
            boost::asio::post(
                executor, [work = std::move(work), handler = std::move(handler),
                           error, response = std::move(response)]() mutable {
                  handler(error, std::move(response));
                });
          };
          auto transfer = std::make_shared<Transfer>(std::move(request),
                                                     std::move(cancellation),
                                                     std::move(completion));
          if (transfer->cancellation.is_connected()) {
            transfer->cancellation.assign(
                [weak = std::weak_ptr<Impl>{self},
                 transfer](boost::asio::cancellation_type_t type) {
                  if (type == boost::asio::cancellation_type::none) {
                    return;
                  }
                  if (auto owner = weak.lock()) {
                    boost::asio::dispatch(owner->strand_, [owner, transfer] {
                      owner->cancel_transfer(transfer);
                    });
                  }
                });
          }
          boost::asio::dispatch(self->strand_, [self, transfer] {
            self->start_transfer(transfer);
          });
        },
        token);
  }

  void shutdown() noexcept {
    auto weak = weak_from_this();
    boost::asio::dispatch(strand_, [weak] {
      if (auto self = weak.lock()) {
        self->shutdown_on_strand();
      }
    });
  }

private:
  enum class AbortReason : std::uint8_t {
    None,
    DecodedBodyLimit,
    HeaderLimit,
    WireBodyLimit,
  };

  struct Transfer final {
    Transfer(CurlRequest value,
             boost::asio::cancellation_slot cancellation_slot,
             Completion completion_handler)
        : request(std::move(value)), cancellation(std::move(cancellation_slot)),
          completion(std::move(completion_handler)) {
      error_buffer.fill('\0');
    }

    ~Transfer() {
      if (headers != nullptr) {
        curl_slist_free_all(headers);
      }
      if (easy != nullptr) {
        curl_easy_cleanup(easy);
      }
    }

    CurlRequest request;
    CurlResponse response{.status_code = 0, .body = {}, .header_lines = {}};
    boost::asio::cancellation_slot cancellation;
    Completion completion;
    CURL *easy = nullptr;
    curl_slist *headers = nullptr;
    std::array<char, CURL_ERROR_SIZE> error_buffer;
    AbortReason abort_reason = AbortReason::None;
    std::size_t header_bytes = 0;
    bool added = false;
    bool complete = false;
  };

  struct SocketWatch final {
    SocketWatch(boost::asio::any_io_executor executor, const curl_socket_t fd)
        : descriptor(std::move(executor)) {
      boost::system::error_code error;
      descriptor.assign(fd, error);
      if (error) {
        throw boost::system::system_error(error);
      }
    }

    ~SocketWatch() { detach(); }

    void detach() noexcept {
      if (!active) {
        return;
      }
      active = false;
      ++epoch;
      boost::system::error_code ignored;
      descriptor.cancel(ignored);
      if (descriptor.is_open()) {
        try {
          (void)descriptor.release();
        } catch (...) {
        }
      }
    }

    boost::asio::posix::stream_descriptor descriptor;
    int interest = CURL_POLL_NONE;
    std::uint64_t epoch = 0;
    bool read_pending = false;
    bool write_pending = false;
    bool active = true;
  };

  template <typename Value>
  static void set_easy(CURL *easy, const CURLoption option, Value value) {
    const auto result = curl_easy_setopt(easy, option, value);
    if (result != CURLE_OK) {
      throw boost::system::system_error(curl_error_code(result));
    }
  }

  static auto write_callback(char *data, const std::size_t size,
                             const std::size_t count, void *user_data)
      -> std::size_t {
    auto &transfer = *static_cast<Transfer *>(user_data);
    const auto bytes = checked_size(size, count);
    if (bytes == std::numeric_limits<std::size_t>::max() ||
        transfer.response.body.size() >
            transfer.request.maximum_response_bytes ||
        bytes > transfer.request.maximum_response_bytes -
                    transfer.response.body.size()) {
      transfer.abort_reason = AbortReason::DecodedBodyLimit;
      return CURL_WRITEFUNC_ERROR;
    }
    transfer.response.body.append(data, bytes);
    return bytes;
  }

  static auto header_callback(char *data, const std::size_t size,
                              const std::size_t count, void *user_data)
      -> std::size_t {
    auto &transfer = *static_cast<Transfer *>(user_data);
    const auto bytes = checked_size(size, count);
    std::size_t current = 0;
    current = transfer.header_bytes;
    if (bytes == std::numeric_limits<std::size_t>::max() ||
        current > transfer.request.maximum_header_bytes ||
        bytes > transfer.request.maximum_header_bytes - current) {
      transfer.abort_reason = AbortReason::HeaderLimit;
      return CURL_WRITEFUNC_ERROR;
    }
    transfer.response.header_lines.emplace_back(data, bytes);
    transfer.header_bytes += bytes;
    return bytes;
  }

  static auto progress_callback(void *user_data, curl_off_t /*download_total*/,
                                const curl_off_t downloaded,
                                curl_off_t /*upload_total*/,
                                curl_off_t /*uploaded*/) -> int {
    auto &transfer = *static_cast<Transfer *>(user_data);
    if (downloaded < 0 || static_cast<std::uint64_t>(downloaded) >
                              transfer.request.maximum_response_bytes) {
      transfer.abort_reason = AbortReason::WireBodyLimit;
      return 1;
    }
    return 0;
  }

  void validate_request(const CurlRequest &request) const {
    if (request.url.empty() || request.connect_timeout.count() <= 0 ||
        request.total_timeout.count() <= 0 ||
        request.maximum_response_bytes == 0 ||
        request.maximum_header_bytes == 0) {
      throw std::invalid_argument("invalid bounded curl request");
    }
    if (request.proxy.has_value() &&
        (request.proxy->host.empty() || request.proxy->port == 0)) {
      throw std::invalid_argument("invalid explicit curl proxy");
    }
  }

  void configure_proxy(Transfer &transfer) {
    if (!transfer.request.proxy.has_value()) {
      set_easy(transfer.easy, CURLOPT_PROXY, "");
      set_easy(transfer.easy, CURLOPT_NOPROXY, "*");
      return;
    }

    const auto &proxy = *transfer.request.proxy;
    set_easy(transfer.easy, CURLOPT_PROXY, proxy.host.c_str());
    set_easy(transfer.easy, CURLOPT_PROXYPORT, static_cast<long>(proxy.port));
    set_easy(transfer.easy, CURLOPT_NOPROXY, "");
    set_easy(transfer.easy, CURLOPT_HTTPPROXYTUNNEL, 1L);
    switch (proxy.kind) {
    case CurlProxyKind::Http:
      set_easy(transfer.easy, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
      break;
    case CurlProxyKind::Https:
      set_easy(transfer.easy, CURLOPT_PROXYTYPE, CURLPROXY_HTTPS);
      break;
    case CurlProxyKind::Socks5Hostname:
      set_easy(transfer.easy, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
      break;
    }
    if (proxy.username.has_value()) {
      set_easy(transfer.easy, CURLOPT_PROXYUSERNAME, proxy.username->c_str());
    }
    if (proxy.password.has_value()) {
      set_easy(transfer.easy, CURLOPT_PROXYPASSWORD, proxy.password->c_str());
    }
    set_easy(transfer.easy, CURLOPT_PROXY_SSL_VERIFYPEER, 1L);
    set_easy(transfer.easy, CURLOPT_PROXY_SSL_VERIFYHOST, 2L);
    if (proxy.ca_bundle.has_value()) {
      set_easy(transfer.easy, CURLOPT_PROXY_CAINFO, proxy.ca_bundle->c_str());
    }
  }

  void configure_transfer(Transfer &transfer) {
    validate_request(transfer.request);
    transfer.easy = curl_easy_init();
    if (transfer.easy == nullptr) {
      throw std::runtime_error("libcurl easy initialization failed");
    }

    set_easy(transfer.easy, CURLOPT_URL, transfer.request.url.c_str());
    set_easy(transfer.easy, CURLOPT_PRIVATE, &transfer);
    set_easy(transfer.easy, CURLOPT_ERRORBUFFER, transfer.error_buffer.data());
    set_easy(transfer.easy, CURLOPT_WRITEFUNCTION, &Impl::write_callback);
    set_easy(transfer.easy, CURLOPT_WRITEDATA, &transfer);
    set_easy(transfer.easy, CURLOPT_HEADERFUNCTION, &Impl::header_callback);
    set_easy(transfer.easy, CURLOPT_HEADERDATA, &transfer);
    set_easy(transfer.easy, CURLOPT_XFERINFOFUNCTION, &Impl::progress_callback);
    set_easy(transfer.easy, CURLOPT_XFERINFODATA, &transfer);
    set_easy(transfer.easy, CURLOPT_NOPROGRESS, 0L);
    set_easy(transfer.easy, CURLOPT_NOSIGNAL, 1L);
    set_easy(transfer.easy, CURLOPT_FOLLOWLOCATION, 0L);
    set_easy(transfer.easy, CURLOPT_MAXREDIRS, 0L);
    set_easy(transfer.easy, CURLOPT_PROTOCOLS_STR, "http,https");
    set_easy(transfer.easy, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    set_easy(transfer.easy, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
    set_easy(transfer.easy, CURLOPT_SSL_VERIFYPEER, 1L);
    set_easy(transfer.easy, CURLOPT_SSL_VERIFYHOST, 2L);
    set_easy(transfer.easy, CURLOPT_CONNECTTIMEOUT_MS,
             static_cast<long>(transfer.request.connect_timeout.count()));
    set_easy(transfer.easy, CURLOPT_TIMEOUT_MS,
             static_cast<long>(transfer.request.total_timeout.count()));
    set_easy(transfer.easy, CURLOPT_MAXFILESIZE_LARGE,
             static_cast<curl_off_t>(transfer.request.maximum_response_bytes));
    // An empty value asks libcurl to advertise and decode every encoding built
    // into the linked library. A caller-provided value is still routed through
    // this option so libcurl decodes exactly the advertised set.
    const auto accept_encoding =
        std::ranges::find_if(transfer.request.headers, [](const auto &header) {
          return equal_ascii_case_insensitive(header.first, "Accept-Encoding");
        });
    set_easy(transfer.easy, CURLOPT_ACCEPT_ENCODING,
             accept_encoding == transfer.request.headers.end()
                 ? ""
                 : accept_encoding->second.c_str());
    set_easy(transfer.easy, CURLOPT_HTTP_CONTENT_DECODING, 1L);
    if (transfer.request.ca_bundle.has_value()) {
      set_easy(transfer.easy, CURLOPT_CAINFO,
               transfer.request.ca_bundle->c_str());
    }

    switch (transfer.request.method) {
    case CurlHttpMethod::Get:
      set_easy(transfer.easy, CURLOPT_HTTPGET, 1L);
      break;
    case CurlHttpMethod::Post:
      set_easy(transfer.easy, CURLOPT_POST, 1L);
      set_easy(transfer.easy, CURLOPT_POSTFIELDS, transfer.request.body.data());
      set_easy(transfer.easy, CURLOPT_POSTFIELDSIZE_LARGE,
               static_cast<curl_off_t>(transfer.request.body.size()));
      break;
    case CurlHttpMethod::Head:
      set_easy(transfer.easy, CURLOPT_NOBODY, 1L);
      break;
    }

    for (const auto &[name, value] : transfer.request.headers) {
      if (equal_ascii_case_insensitive(name, "Accept-Encoding")) {
        continue;
      }
      const auto line = value.empty() ? name + ";" : name + ": " + value;
      auto *next = curl_slist_append(transfer.headers, line.c_str());
      if (next == nullptr) {
        throw std::bad_alloc{};
      }
      transfer.headers = next;
    }
    if (transfer.headers != nullptr) {
      set_easy(transfer.easy, CURLOPT_HTTPHEADER, transfer.headers);
    }
    configure_proxy(transfer);
  }

  void start_transfer(const std::shared_ptr<Transfer> &transfer) {
    if (shutting_down_) {
      complete_transfer(transfer, boost::asio::error::operation_aborted);
      return;
    }
    try {
      configure_transfer(*transfer);
      const auto result = curl_multi_add_handle(multi_, transfer->easy);
      if (result != CURLM_OK) {
        throw std::runtime_error("libcurl could not add transfer");
      }
      transfer->added = true;
      transfers_.emplace(transfer->easy, transfer);
      perform_socket_action(CURL_SOCKET_TIMEOUT, 0);
    } catch (const boost::system::system_error &error) {
      complete_transfer(transfer, error.code());
    } catch (...) {
      complete_transfer(transfer, boost::system::errc::make_error_code(
                                      boost::system::errc::invalid_argument));
    }
  }

  void cancel_transfer(const std::shared_ptr<Transfer> &transfer) {
    if (transfer->complete) {
      return;
    }
    complete_transfer(transfer, boost::asio::error::operation_aborted);
  }

  void complete_transfer(const std::shared_ptr<Transfer> &transfer,
                         boost::system::error_code error) {
    if (transfer->complete) {
      return;
    }
    transfer->complete = true;
    transfer->cancellation.clear();
    if (transfer->added) {
      curl_multi_remove_handle(multi_, transfer->easy);
      transfer->added = false;
      transfers_.erase(transfer->easy);
    }
    auto completion = std::move(transfer->completion);
    auto response = std::move(transfer->response);
    completion(error, std::move(response));
  }

  void check_completions() {
    int remaining = 0;
    while (auto *message = curl_multi_info_read(multi_, &remaining)) {
      if (message->msg != CURLMSG_DONE) {
        continue;
      }
      const auto found = transfers_.find(message->easy_handle);
      if (found == transfers_.end()) {
        continue;
      }
      auto transfer = found->second;
      curl_easy_getinfo(transfer->easy, CURLINFO_RESPONSE_CODE,
                        &transfer->response.status_code);
      boost::system::error_code error;
      if (transfer->abort_reason == AbortReason::DecodedBodyLimit ||
          transfer->abort_reason == AbortReason::HeaderLimit ||
          transfer->abort_reason == AbortReason::WireBodyLimit ||
          message->data.result == CURLE_FILESIZE_EXCEEDED) {
        error = boost::asio::error::message_size;
      } else if (message->data.result == CURLE_OPERATION_TIMEDOUT) {
        error = boost::asio::error::timed_out;
      } else if (message->data.result != CURLE_OK) {
        error = curl_error_code(message->data.result);
      }
      complete_transfer(transfer, error);
    }
  }

  static auto socket_callback(CURL * /*easy*/, const curl_socket_t socket,
                              const int action, void *user_data,
                              void * /*socket_data*/) -> int {
    try {
      return static_cast<Impl *>(user_data)->update_socket(socket, action);
    } catch (...) {
      return -1;
    }
  }

  static auto timer_callback(CURLM * /*multi*/, const long timeout_ms,
                             void *user_data) -> int {
    try {
      static_cast<Impl *>(user_data)->update_timer(timeout_ms);
      return 0;
    } catch (...) {
      return -1;
    }
  }

  auto update_socket(const curl_socket_t socket, const int action) -> int {
    if (shutting_down_ || action == CURL_POLL_REMOVE) {
      const auto found = sockets_.find(socket);
      if (found != sockets_.end()) {
        found->second->detach();
        sockets_.erase(found);
      }
      curl_multi_assign(multi_, socket, nullptr);
      return 0;
    }

    auto &watch = sockets_[socket];
    if (!watch) {
      watch = std::make_shared<SocketWatch>(strand_, socket);
      curl_multi_assign(multi_, socket, watch.get());
    }
    ++watch->epoch;
    watch->interest = action;
    boost::system::error_code ignored;
    watch->descriptor.cancel(ignored);
    arm_socket(socket, watch);
    return 0;
  }

  void arm_socket(const curl_socket_t socket,
                  const std::shared_ptr<SocketWatch> &watch) {
    if (!watch->active || shutting_down_) {
      return;
    }
    const bool wants_read =
        watch->interest == CURL_POLL_IN || watch->interest == CURL_POLL_INOUT;
    const bool wants_write =
        watch->interest == CURL_POLL_OUT || watch->interest == CURL_POLL_INOUT;
    const auto epoch = watch->epoch;
    if (wants_read && !watch->read_pending) {
      watch->read_pending = true;
      watch->descriptor.async_wait(
          boost::asio::posix::stream_descriptor::wait_read,
          [weak = weak_from_this(), socket, watch,
           epoch](const boost::system::error_code &error) {
            if (auto self = weak.lock()) {
              self->socket_ready(socket, watch, epoch, true, error);
            }
          });
    }
    if (wants_write && !watch->write_pending) {
      watch->write_pending = true;
      watch->descriptor.async_wait(
          boost::asio::posix::stream_descriptor::wait_write,
          [weak = weak_from_this(), socket, watch,
           epoch](const boost::system::error_code &error) {
            if (auto self = weak.lock()) {
              self->socket_ready(socket, watch, epoch, false, error);
            }
          });
    }
  }

  void socket_ready(const curl_socket_t socket,
                    const std::shared_ptr<SocketWatch> &watch,
                    const std::uint64_t epoch, const bool read,
                    const boost::system::error_code &error) {
    if (read) {
      watch->read_pending = false;
    } else {
      watch->write_pending = false;
    }
    const auto found = sockets_.find(socket);
    if (found == sockets_.end() || found->second != watch || !watch->active ||
        shutting_down_) {
      return;
    }
    if (error == boost::asio::error::operation_aborted ||
        epoch != watch->epoch) {
      arm_socket(socket, watch);
      return;
    }
    const auto action =
        error ? CURL_CSELECT_ERR : (read ? CURL_CSELECT_IN : CURL_CSELECT_OUT);
    perform_socket_action(socket, action);
    arm_socket(socket, watch);
  }

  void update_timer(const long timeout_ms) {
    ++timer_epoch_;
    (void)timer_.cancel();
    if (shutting_down_ || timeout_ms < 0) {
      return;
    }
    const auto epoch = timer_epoch_;
    timer_.expires_after(std::chrono::milliseconds(timeout_ms));
    timer_.async_wait([weak = weak_from_this(),
                       epoch](const boost::system::error_code &error) {
      if (auto self = weak.lock(); self && !error &&
                                   epoch == self->timer_epoch_ &&
                                   !self->shutting_down_) {
        self->perform_socket_action(CURL_SOCKET_TIMEOUT, 0);
      }
    });
  }

  void perform_socket_action(const curl_socket_t socket, const int action) {
    int running = 0;
    const auto result =
        curl_multi_socket_action(multi_, socket, action, &running);
    if (result != CURLM_OK) {
      auto active = transfers_;
      for (auto &[easy, transfer] : active) {
        (void)easy;
        complete_transfer(transfer, boost::system::errc::make_error_code(
                                        boost::system::errc::io_error));
      }
      return;
    }
    check_completions();
  }

  void shutdown_on_strand() {
    if (shutting_down_) {
      return;
    }
    shutting_down_ = true;
    ++timer_epoch_;
    try {
      (void)timer_.cancel();
    } catch (...) {
    }
    for (auto &[socket, watch] : sockets_) {
      (void)socket;
      watch->detach();
    }
    sockets_.clear();
    auto active = transfers_;
    for (auto &[easy, transfer] : active) {
      (void)easy;
      complete_transfer(transfer, boost::asio::error::operation_aborted);
    }
  }

  boost::asio::strand<boost::asio::any_io_executor> strand_;
  boost::asio::steady_timer timer_;
  CURLM *multi_ = nullptr;
  std::unordered_map<curl_socket_t, std::shared_ptr<SocketWatch>> sockets_;
  std::unordered_map<CURL *, std::shared_ptr<Transfer>> transfers_;
  std::uint64_t timer_epoch_ = 0;
  bool shutting_down_ = false;
};

CurlAsioMulti::CurlAsioMulti(boost::asio::any_io_executor executor)
    : impl_(std::make_shared<Impl>(std::move(executor))) {}

CurlAsioMulti::~CurlAsioMulti() { shutdown(); }

auto CurlAsioMulti::perform(CurlRequest request)
    -> boost::asio::awaitable<CurlResponse> {
  co_return co_await impl_->perform(std::move(request));
}

void CurlAsioMulti::shutdown() noexcept {
  if (impl_) {
    impl_->shutdown();
  }
}

} // namespace obcx::network::detail
