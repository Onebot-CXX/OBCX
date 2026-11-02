#include "onebot11/network/http/connection_manager.hpp"
#include "../../bot/standard_event_context.hpp"
#include "../../bot/standard_execution_codec.hpp"
#include "common/logger.hpp"
#include "network/http_call.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>

namespace obcx::network {

using json = nlohmann::json;
namespace st = obcx::onebot11::bot::standard;

struct HttpConnectionManager::ConnectionState {
  std::recursive_mutex mutex;
  std::atomic_uint64_t epoch{};
  std::atomic_bool connected{false};
  std::shared_ptr<HttpClient> client;
  common::ConnectionConfig config;
};

// All asynchronous standard work owns this lease. No manager pointer survives
// the synchronous wrapper; the connection/cache/state lock order is one-way.
struct HttpConnectionManager::StandardRequest {
  using FailureKind = st::detail::StandardTransportFailureKind;

  StandardRequest(std::shared_ptr<ConnectionState> connection_state,
                  std::shared_ptr<st::detail::StandardCallState> call_state,
                  std::shared_ptr<core::SelfIdentityCache> identity_owner,
                  core::SelfIdentityOrigin original_origin)
      : connection(std::move(connection_state)), call(std::move(call_state)),
        identity(std::move(identity_owner)),
        origin(std::move(original_origin)) {}

  auto with_authority(const std::function<void()> &capture) -> bool {
    const std::lock_guard lock{connection->mutex};
    if (!connection->connected || connection->epoch != epoch ||
        connection->client != client || !client || !identity)
      return false;
    return identity->observe_user_and_capture(origin, std::nullopt, capture)
        .accepted();
  }

  auto admit() -> bool {
    bool committed = false;
    bool capture_failed = false;
    const auto current = with_authority([&]() noexcept {
      try {
        committed = call->try_commit_write();
      } catch (...) {
        // The cache capture contract is no-throw/no-reentry. A failed capture
        // never authorizes the driver, and never erases checkpoint evidence.
        capture_failed = true;
      }
    });
    if (!current)
      (void)call->try_fail_before_commit(
          {FailureKind::ConnectionRetired, asio::error::operation_aborted});
    else if (capture_failed)
      (void)call->try_fail_before_commit(
          {FailureKind::TransportError, asio::error::invalid_argument});
    return current && committed;
  }

  void cancel(asio::cancellation_type_t type) {
    if (type == asio::cancellation_type::none)
      return;
    cancelled = true;
    // Revoke admission synchronously, BEFORE forwarding to the real operation.
    // After commitment only its first-terminal value completion arbitrates.
    (void)call->try_fail_before_commit(
        {FailureKind::Cancelled, asio::error::operation_aborted});
    forwarding.emit(type);
  }

  void fail(boost::system::error_code error) {
    auto kind = FailureKind::TransportError;
    if (cancelled)
      kind = FailureKind::Cancelled;
    else if (!with_authority([] {}))
      kind = FailureKind::ConnectionRetired;
    else if (std::chrono::steady_clock::now() >= call->deadline())
      kind = FailureKind::DeadlineExceeded;
    // A connect timeout or generic message_size alone proves neither overall
    // deadline expiry nor native JSON body excess. Preserve the actual code.
    (void)call->try_fail({kind, error});
  }

  static auto perform(std::shared_ptr<StandardRequest> self)
      -> asio::awaitable<void> {
    // co_spawn has its own cancellation domain. Do not alter the caller's
    // filters/throw policy, or lose an early cancellation before child binding.
    if (self->cancelled)
      co_return;
    co_await asio::this_coro::reset_cancellation_state(
        asio::enable_total_cancellation{});
    co_await asio::this_coro::throw_if_cancelled(false);
    auto result = co_await detail::HttpCallAccess::post_until(
        *self->client, std::move(self->path), std::move(self->parameters),
        std::move(self->headers), self->call->limits().maximum_response_bytes,
        self->call->deadline(), [self] { return self->admit(); });
    if (result.response_complete) {
      // First action after value completion: retain ALL statuses and the full
      // body. No late cancellation, authority or deadline check may discard it.
      (void)self->call->try_complete_reply(std::move(result.body),
                                           result.status_code);
      co_return;
    }
    self->fail(result.error);
  }

  static auto run(std::shared_ptr<StandardRequest> self)
      -> asio::awaitable<void> {
    if (self->call->terminal())
      co_return;
    const auto cancellation = co_await asio::this_coro::cancellation_state;
    const auto executor = co_await asio::this_coro::executor;
    auto slot = cancellation.slot();
    if (slot.has_handler())
      throw std::logic_error(
          "standard HTTP caller cancellation slot is occupied");

    // Installation, emission and clearing require the caller's Asio
    // serialization domain. No concurrent cancellation-signal misuse support.
    struct SlotCleanup {
      asio::cancellation_slot caller;
      std::shared_ptr<StandardRequest> owner;
      ~SlotCleanup() {
        caller.clear();
        owner->forwarding.slot().clear();
      }
    } cleanup{slot, self};
    if (slot.is_connected())
      slot.assign(
          [self](asio::cancellation_type_t type) { self->cancel(type); });
    if (cancellation.cancelled() != asio::cancellation_type::none) {
      // Do not emit into an unbound child and then start I/O as if replayed.
      self->cancel(cancellation.cancelled());
      co_return;
    }
    try {
      // The separate bound slot prevents use_awaitable/co_spawn from replacing
      // our interceptor. Joined completion owns both frames until HTTP ends.
      co_await asio::co_spawn(
          executor, perform(self),
          asio::bind_cancellation_slot(self->forwarding.slot(),
                                       asio::use_awaitable));
    } catch (const std::bad_alloc &) {
      throw;
    } catch (const boost::system::system_error &error) {
      self->fail(error.code());
    } catch (const std::exception &) {
      self->fail(boost::system::error_code{});
    }
  }

  std::shared_ptr<ConnectionState> connection;
  std::shared_ptr<st::detail::StandardCallState> call;
  std::shared_ptr<core::SelfIdentityCache> identity;
  core::SelfIdentityOrigin origin;
  std::shared_ptr<HttpClient> client;
  std::uint64_t epoch{};
  std::string path;
  std::string parameters;
  std::map<std::string, std::string> headers;
  asio::cancellation_signal forwarding;
  bool cancelled{false}; // caller execution domain, not the curl driver
};

auto HttpConnectionManager::send_standard_action(
    st::detail::NativeAction action, st::ExecutionMode mode,
    std::string parameters, std::shared_ptr<st::detail::StandardCallState> call,
    std::shared_ptr<core::SelfIdentityCache> identity,
    core::SelfIdentityOrigin origin) -> asio::awaitable<void> {
  if (!call)
    throw std::invalid_argument("standard HTTP call state is required");
  if (parameters.size() > call->limits().maximum_parameter_bytes)
    throw std::invalid_argument("standard HTTP parameters exceed byte limit");
  auto path = "/" + st::detail::native_action_name(action, mode);
  auto request = std::make_shared<StandardRequest>(
      connection_, std::move(call), std::move(identity), std::move(origin));
  request->path = std::move(path);
  request->parameters = std::move(parameters);
  {
    const std::lock_guard lock{connection_->mutex};
    request->client = connection_->client;
    request->epoch = connection_->epoch;
    request->headers["Content-Type"] = "application/json";
    request->headers["User-Agent"] = "OBCX/1.0";
    if (!connection_->config.access_token.empty())
      request->headers["Authorization"] =
          "Bearer " + connection_->config.access_token;
    if (!request->with_authority([] {}))
      (void)request->call->try_fail_before_commit(
          {StandardRequest::FailureKind::ConnectionRetired,
           asio::error::operation_aborted});
  }
  return StandardRequest::run(std::move(request));
}

auto HttpConnectionManager::send_file_query(
    obcx::onebot11::bot::detail::FileQueryKind kind, std::string parameters,
    std::shared_ptr<st::detail::StandardCallState> call,
    std::shared_ptr<core::SelfIdentityCache> identity,
    core::SelfIdentityOrigin origin) -> asio::awaitable<void> {
  if (!call || parameters.size() > call->limits().maximum_parameter_bytes)
    throw std::invalid_argument("QQ file HTTP query exceeds call bounds");
  const auto name = obcx::onebot11::bot::detail::file_query_name(kind);
  auto request = std::make_shared<StandardRequest>(
      connection_, std::move(call), std::move(identity), std::move(origin));
  request->path = "/" + std::string(name);
  request->parameters = std::move(parameters);
  {
    const std::lock_guard lock{connection_->mutex};
    request->client = connection_->client;
    request->epoch = connection_->epoch;
    request->headers["Content-Type"] = "application/json";
    request->headers["User-Agent"] = "OBCX/1.0";
    if (!connection_->config.access_token.empty())
      request->headers["Authorization"] =
          "Bearer " + connection_->config.access_token;
    if (!request->with_authority([] {}))
      (void)request->call->try_fail_before_commit(
          {StandardRequest::FailureKind::ConnectionRetired,
           asio::error::operation_aborted});
  }
  return StandardRequest::run(std::move(request));
}

HttpConnectionManager::HttpConnectionManager(
    asio::io_context &ioc, adapter::onebot11::ProtocolAdapter &adapter)
    : ioc_(ioc), adapter_(adapter),
      connection_(std::make_shared<ConnectionState>()),
      poll_timer_(asio::make_strand(ioc)) {
  OBCX_INFO("HttpConnectionManager initialized");
}

HttpConnectionManager::~HttpConnectionManager() {
  // Release our own resources (poll_timer_, connection_->client) while the
  // referenced installation io_context is still alive. BotInstallation owns
  // transports ahead of its executor and guarantees this destruction order.
  try {
    shutdown();
  } catch (const std::exception &) {
    OBCX_ERROR("Failed to shut down HTTP connection manager");
  } catch (...) {
    OBCX_ERROR("Failed to shut down HTTP connection manager");
  }
}

void HttpConnectionManager::connect(const common::ConnectionConfig &config) {
  std::lock_guard epoch_lock(connection_->mutex);
  if (connection_->connected)
    throw std::logic_error("QQ HTTP transport already connected");
  connection_->config = config;

  connection_->client = std::make_shared<HttpClient>(ioc_, connection_->config);

  connection_->connected = true;
  if (connection_callback_)
    connection_callback_(true);
  start_polling();

  OBCX_INFO("HTTP connection established to {}:{}", connection_->config.host,
            connection_->config.port);
}

void HttpConnectionManager::disconnect() { shutdown(); }

void HttpConnectionManager::shutdown() {
  std::lock_guard epoch_lock(connection_->mutex);
  const bool connected = connection_->connected.exchange(false);
  // Invalidate the lease and close its client before external notification;
  // a throwing callback must not leave a saved standard request active.
  stop_polling();
  if (connection_->client) {
    connection_->client->close();
  }
  if (connected && connection_callback_)
    connection_callback_(false);

  OBCX_INFO("HTTP connection disconnected");
}

auto HttpConnectionManager::is_connected() const -> bool {
  return connection_->connected.load();
}

auto HttpConnectionManager::send_action_and_wait_async(
    std::string action_payload, uint64_t echo_id,
    std::optional<std::uint64_t> expected_epoch)
    -> asio::awaitable<std::string> {

  std::shared_ptr<HttpClient> client;
  std::map<std::string, std::string> headers;
  {
    std::lock_guard lock(connection_->mutex);
    if (!connection_->connected || !connection_->client ||
        (expected_epoch && *expected_epoch != connection_->epoch))
      throw std::runtime_error("HTTP originating connection retired");
    client = connection_->client;
    headers["Content-Type"] = "application/json";
    headers["User-Agent"] = "OBCX/1.0";
    if (!connection_->config.access_token.empty())
      headers["Authorization"] = "Bearer " + connection_->config.access_token;
  }
  try {
    std::string api_path = "/api"; // OneBot11 standard action endpoint
    auto response = co_await client->post(api_path, action_payload, headers);

    if (!response.is_success()) {
      throw std::runtime_error(fmt::format(
          "HTTP request failed: {}", std::to_string(response.status_code)));
    }

    co_return response.body;

  } catch (const std::exception &) {
    OBCX_ERROR("HTTP API request failed");
    throw;
  }
}

void HttpConnectionManager::set_standard_events(
    std::shared_ptr<obcx::onebot11::bot::standard::StandardEventCapabilityV1>
        events) {
  std::lock_guard lock(connection_->mutex);
  standard_events_ = std::move(events);
}
void HttpConnectionManager::set_event_callback(EventCallback callback) {
  std::lock_guard lock(connection_->mutex);
  event_callback_ = std::move(callback);
}

void HttpConnectionManager::set_origin_capture(
    onebot11::OriginCapture capture) {
  std::lock_guard lock(connection_->mutex);
  origin_capture_ = std::move(capture);
}
void HttpConnectionManager::set_connection_callback(
    std::function<void(bool)> callback) {
  std::lock_guard lock(connection_->mutex);
  connection_callback_ = std::move(callback);
}
auto HttpConnectionManager::send_profile_action(
    std::string payload, std::uint64_t echo, core::SelfIdentityOrigin origin,
    onebot11::OriginValidation validate, onebot11::ProfilePublication publish)
    -> asio::awaitable<core::SelfIdentityCache::Result> {
  std::uint64_t epoch;
  {
    std::lock_guard lock(connection_->mutex);
    if (!connection_->connected || !connection_->client ||
        !validate(origin).accepted())
      co_return onebot11::retired_profile();
    epoch = connection_->epoch;
  }
  const auto response =
      co_await send_action_and_wait_async(std::move(payload), echo, epoch);
  std::lock_guard lock(connection_->mutex);
  if (!connection_->connected || epoch != connection_->epoch)
    co_return onebot11::retired_profile();
  // Native lock precedes cache lock; publisher validates and atomically
  // publishes with the original origin, never a freshly captured replacement.
  co_return publish(response, origin);
}

auto HttpConnectionManager::get_connection_type() const -> std::string {
  return "HTTP";
}

void HttpConnectionManager::set_poll_interval(
    const std::chrono::milliseconds interval) {
  if (is_polling_) {
    throw std::logic_error(
        "cannot change OneBot HTTP poll interval while running");
  }
  poll_interval_ = interval;
}

void HttpConnectionManager::start_polling() {
  if (is_polling_.exchange(true) == false) {
    std::lock_guard epoch_lock(connection_->mutex);
    const auto epoch = ++connection_->epoch;
    auto origin = origin_capture_ ? origin_capture_() : std::nullopt;
    asio::co_spawn(poll_timer_.get_executor(),
                   poll_events(epoch, connection_->client, std::move(origin)),
                   asio::detached);
    OBCX_INFO("Start HTTP event polling, interval: {}ms",
              poll_interval_.count());
  }
}

void HttpConnectionManager::stop_polling() {
  std::lock_guard epoch_lock(connection_->mutex);
  ++connection_->epoch;
  if (is_polling_.exchange(false)) {
    asio::post(poll_timer_.get_executor(),
               [this, epoch = connection_->epoch.load()] {
                 std::lock_guard lock(connection_->mutex);
                 if (epoch == connection_->epoch)
                   poll_timer_.cancel();
               });
    OBCX_INFO("Stop HTTP event polling");
  }
}

auto HttpConnectionManager::poll_events(
    std::uint64_t epoch, std::shared_ptr<HttpClient> client,
    std::optional<core::SelfIdentityOrigin> first_origin)
    -> asio::awaitable<void> {
  bool first_read = true;
  while (is_polling_ && epoch == connection_->epoch) {
    try {
      if (!client) {
        break;
      }

      std::map<std::string, std::string> headers;
      std::optional<core::SelfIdentityOrigin> origin;
      {
        std::lock_guard lock(connection_->mutex);
        if (!is_polling_ || epoch != connection_->epoch ||
            client != connection_->client)
          break;
        headers["User-Agent"] = "OBCX/1.0";
        if (!connection_->config.access_token.empty())
          headers["Authorization"] =
              "Bearer " + connection_->config.access_token;
        // Freeze this read's process authority BEFORE issuing the GET.
        if (first_read) {
          origin = std::move(first_origin);
          first_read = false;
        } else if (origin_capture_)
          origin = origin_capture_();
      }

      std::string_view events_path =
          "/get_latest_events"; // OneBot11 events endpoint
      auto response = co_await client->get(events_path, headers);

      if (!is_polling_ || epoch != connection_->epoch) {
        break;
      }
      if (response.is_success() && !response.body.empty()) {
        process_events(response.body, epoch, origin);
      }

    } catch (const std::exception &) {
      if (!is_polling_ || epoch != connection_->epoch) {
        break;
      }
      OBCX_WARN("Event polling failed");
    }

    if (!is_polling_ || epoch != connection_->epoch) {
      break;
    }
    {
      std::lock_guard lock(connection_->mutex);
      if (!is_polling_ || epoch != connection_->epoch)
        break;
      poll_timer_.expires_after(poll_interval_);
    }
    try {
      co_await poll_timer_.async_wait(asio::use_awaitable);
    } catch (const boost::system::system_error &e) {
      if (e.code() == asio::error::operation_aborted) {
        break;
      }
    }
  }

  OBCX_DEBUG("HTTP event polling coroutine exited");
}

void HttpConnectionManager::process_events(
    std::string_view events_json, std::uint64_t epoch,
    const std::optional<core::SelfIdentityOrigin> &origin) {
  std::lock_guard lock(connection_->mutex);
  if (!is_polling_ || epoch != connection_->epoch)
    return;
  const auto publish = [&](const common::Event &event) {
    if (!event_callback_)
      return true;
    const auto result = event_callback_(event, origin);
    // A transition admits only the event that observed it. Never rebase the
    // remainder, even when no previous readable self snapshot existed.
    return result.status != core::SelfObservationStatus::Transitioned &&
           result.status != core::SelfObservationStatus::RejectedOrigin &&
           result.status != core::SelfObservationStatus::Closed;
  };
  try {
    json document;
    if (standard_events_) {
      auto frame = st::detail::StandardEventAccess::receive(*standard_events_,
                                                            events_json);
      if (frame.disposition !=
          st::detail::EventIngressDisposition::EventCompatible)
        return;
      document = std::move(frame.document);
    } else {
      document = json::parse(events_json);
    }
    const auto accept = [&](json &value, std::string_view wire) {
      // A poll batch is structurally preflighted as a whole. API-like entries
      // are skipped, never responded to or admitted as events. Unsupported
      // standard projections can still use the existing legacy conversion.
      if (!st::detail::StandardEventAccess::event_candidate(value))
        return true;
      auto event = adapter_.parse_event(std::string(wire));
      if (standard_events_)
        st::detail::StandardEventAccess::admit(*standard_events_,
                                               std::move(value), origin);
      return !event || publish(*event);
    };
    if (document.is_object()) {
      (void)accept(document, events_json);
    } else if (document.is_array()) {
      for (auto &value : document) {
        if (!is_polling_ || epoch != connection_->epoch)
          break;
        // Admit in order, retaining this poll's original origin. No later
        // standard or legacy entry is published after a legacy transition.
        if (!st::detail::StandardEventAccess::event_candidate(value))
          continue;
        if (!accept(value, value.dump()))
          break;
      }
    }
  } catch (const json::exception &) {
    OBCX_WARN("Failed to parse event JSON");
  }
}

} // namespace obcx::network
