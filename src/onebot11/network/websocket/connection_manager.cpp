#include "onebot11/network/websocket/connection_manager.hpp"
#include "../../bot/standard_event_context.hpp"
#include "common/logger.hpp"
#include "standard_ws_wire.hpp"

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/this_coro.hpp>
#include <utility>

namespace obcx::network {
namespace st = obcx::onebot11::bot::standard;
namespace sd = st::detail;

struct WebSocketConnectionManager::ConnectionState
    : std::enable_shared_from_this<ConnectionState> {
  ConnectionState(asio::io_context &context,
                  adapter::onebot11::ProtocolAdapter &protocol,
                  detail::ActionDeadlineFactory factory)
      : ioc(context), adapter(&protocol), strand(asio::make_strand(ioc)),
        reconnect_timer(strand),
        requests(std::make_shared<detail::ActionRequestTracker>(
            ioc.get_executor(), std::move(factory))) {}

  void connect(const common::ConnectionConfig &config) {
    std::lock_guard lock(mutex);
    if (retired || running) {
      OBCX_WARN("ConnectionManager already has a running connection.");
      return;
    }
    action_timeout = config.action_timeout;
    host = config.host;
    port = config.port;
    access_token = config.access_token;
    running = true;
    do_connect(epoch);
  }
  static auto close_client(std::shared_ptr<WebsocketClient> client)
      -> asio::awaitable<void> {
    co_await client->close();
  }
  void shutdown(bool destroying) {
    std::shared_ptr<WebsocketClient> old_client;
    std::uint64_t stopped_epoch;
    {
      std::lock_guard lock(mutex);
      stopped_epoch = ++epoch;
      running = false;
      const bool was_connected = std::exchange(connected, false);
      old_client = std::exchange(client, nullptr);
      if (destroying) {
        retired = true;
        event_callback = {};
        origin_capture = {};
        connection_callback = {};
        adapter = nullptr;
      }
      // Invalidation is synchronous, before any external callback can throw.
      requests->cancel_all();
      asio::post(strand,
                 [self = shared_from_this(), old_client, stopped_epoch] {
                   {
                     std::lock_guard lock(self->mutex);
                     if (self->epoch == stopped_epoch)
                       self->reconnect_timer.cancel();
                   }
                   if (old_client)
                     asio::co_spawn(self->strand, close_client(old_client),
                                    asio::detached);
                 });
      if (was_connected && connection_callback)
        connection_callback(false);
    }
  }
  static auto run_client(std::shared_ptr<ConnectionState> self,
                         std::shared_ptr<WebsocketClient> client,
                         std::uint64_t epoch, std::string host,
                         std::string port, std::string token)
      -> asio::awaitable<void> {
    const std::weak_ptr<ConnectionState> weak = self;
    co_await client->run(
        std::move(host), std::move(port), std::move(token),
        [weak, epoch](const beast::error_code &error, const std::string &) {
          if (auto owner = weak.lock())
            owner->on_connection(epoch, error);
        },
        [weak, epoch]() -> WebsocketClient::MessageHandler {
          if (auto owner = weak.lock())
            return owner->make_read_handler(epoch);
          return [](const beast::error_code &, const std::string &) {};
        });
  }
  void do_connect(std::uint64_t expected_epoch) {
    asio::post(strand, [self = shared_from_this(), expected_epoch] {
      std::lock_guard lock(self->mutex);
      if (self->retired || !self->running || self->epoch != expected_epoch)
        return;
      const auto epoch = ++self->epoch;
      auto client = std::make_shared<WebsocketClient>(self->ioc);
      self->client = client;
      OBCX_INFO("Attempting to connect to ws://{}:{}", self->host, self->port);
      asio::co_spawn(self->strand,
                     run_client(self, client, epoch, self->host,
                                std::to_string(self->port), self->access_token),
                     asio::detached);
    });
  }
  void on_connection(std::uint64_t read_epoch, const beast::error_code &error) {
    std::lock_guard lock(mutex);
    if (retired || !running || epoch != read_epoch)
      return;
    if (error) {
      ++epoch;
      connected = false;
      requests->fail_all(
          std::make_exception_ptr(boost::system::system_error{error}));
      schedule_reconnect();
      if (connection_callback)
        connection_callback(false);
      return;
    }
    if (std::exchange(connected, true))
      return;
    reconnect_timer.cancel(); // Lifecycle runs on strand, as does this timer.
    if (connection_callback)
      connection_callback(true);
  }
  auto make_read_handler(std::uint64_t read_epoch)
      -> WebsocketClient::MessageHandler {
    std::lock_guard lock(mutex);
    std::optional<core::SelfIdentityOrigin> origin;
    if (!retired && running && connected && epoch == read_epoch &&
        origin_capture)
      origin = origin_capture();
    return [weak = weak_from_this(), read_epoch, origin = std::move(origin)](
               const beast::error_code &error, const std::string &message) {
      if (auto self = weak.lock())
        self->on_message(read_epoch, origin, error, message);
    };
  }
  void on_message(std::uint64_t read_epoch,
                  const std::optional<core::SelfIdentityOrigin> &origin,
                  const beast::error_code &error, const std::string &message) {
    if (error)
      return;
    // Reserved namespace routing precedes both legacy DOM and EVENT epoch
    // fencing. A known result never depends on a fresh receive-account token.
    if (requests->receive_standard(read_epoch, message))
      return;
    std::lock_guard lock(mutex);
    if (retired || !running || !connected || epoch != read_epoch || !adapter)
      return;
    try {
      common::json document;
      if (standard_events) {
        auto frame =
            sd::StandardEventAccess::receive(*standard_events, message);
        if (frame.disposition == sd::EventIngressDisposition::Rejected)
          return;
        document = frame.disposition == sd::EventIngressDisposition::ApiOnly
                       ? common::json::parse(message)
                       : std::move(frame.document);
      } else {
        document = common::json::parse(message);
      }
      if (document.is_object() &&
          (document.contains("echo") || document.contains("retcode"))) {
        if (document.contains("echo") && document.contains("retcode")) {
          const std::uint64_t echo = document["echo"];
          if (requests->respond(echo, message))
            return;
          OBCX_WARN("Received OneBot action response with unknown echo");
        }
        // API-only, including mixed event objects and unknown/malformed echoes.
        return;
      }
      if (standard_events) {
        if (document.is_array()) {
          for (auto &entry : document)
            sd::StandardEventAccess::admit(*standard_events, std::move(entry),
                                           origin);
        } else {
          sd::StandardEventAccess::admit(*standard_events, std::move(document),
                                         origin);
        }
      }
    } catch (const nlohmann::json::exception &) {
      OBCX_WARN("Failed to parse WebSocket message JSON");
      return;
    }
    if (auto event = adapter->parse_event(message); event && event_callback)
      (void)event_callback(*event, origin);
  }
  void schedule_reconnect() {
    if (!running || retired)
      return;
    reconnect_timer.expires_after(std::chrono::seconds(5));
    OBCX_INFO("Reconnection scheduled in {}ms", 5000);
    reconnect_timer.async_wait(
        [weak = weak_from_this(),
         expected_epoch = epoch](const beast::error_code &error) {
          if (!error)
            if (auto self = weak.lock())
              self->do_connect(expected_epoch);
        });
  }

  static auto send_legacy(std::shared_ptr<ConnectionState> self,
                          std::string payload, std::uint64_t echo,
                          std::optional<std::uint64_t> expected_epoch)
      -> asio::awaitable<std::string> {
    std::shared_ptr<WebsocketClient> client;
    std::uint64_t epoch;
    std::chrono::milliseconds timeout;
    {
      std::lock_guard lock(self->mutex);
      if (self->retired || !self->client || !self->connected ||
          (expected_epoch && *expected_epoch != self->epoch))
        throw std::runtime_error("No available WebSocket client");
      client = self->client;
      epoch = self->epoch;
      timeout = self->action_timeout;
    }
    auto request = self->requests->start(echo, timeout);
    try {
      co_await asio::co_spawn(
          self->strand,
          [self, client, epoch,
           payload = std::move(payload)]() -> asio::awaitable<void> {
            {
              std::lock_guard lock(self->mutex);
              if (self->retired || epoch != self->epoch || !self->connected)
                throw std::runtime_error("QQ send connection epoch retired");
            }
            co_await client->send(payload, [self, client, epoch] {
              std::lock_guard lock(self->mutex);
              return !self->retired && epoch == self->epoch &&
                     self->connected && self->client == client;
            });
          },
          asio::use_awaitable);
      OBCX_DEBUG("OneBot action sent: echo={}", echo);
    } catch (...) {
      (void)self->requests->fail(echo, request, std::current_exception());
    }
    auto outcome = co_await request->wait();
    switch (outcome.status) {
    case detail::ActionTerminalStatus::Response:
      OBCX_DEBUG("OneBot action completed: echo={} response_bytes={}", echo,
                 outcome.response.size());
      co_return std::move(outcome.response);
    case detail::ActionTerminalStatus::Timeout:
      OBCX_ERROR("OneBot action timed out: echo={}", echo);
      throw std::runtime_error("API request timeout");
    case detail::ActionTerminalStatus::TransportFailure:
    case detail::ActionTerminalStatus::Cancelled:
      if (outcome.failure)
        std::rethrow_exception(outcome.failure);
      throw boost::system::system_error{asio::error::operation_aborted};
    }
    throw std::runtime_error("OneBot action completed with an invalid state");
  }
  static auto send_profile(std::shared_ptr<ConnectionState> self,
                           std::string payload, std::uint64_t echo,
                           core::SelfIdentityOrigin origin,
                           onebot11::OriginValidation validate,
                           onebot11::ProfilePublication publish)
      -> asio::awaitable<core::SelfIdentityCache::Result> {
    std::uint64_t epoch;
    {
      std::lock_guard lock(self->mutex);
      if (self->retired || !self->connected || !self->client ||
          !validate(origin).accepted())
        co_return onebot11::retired_profile();
      epoch = self->epoch;
    }
    const auto response =
        co_await send_legacy(self, std::move(payload), echo, epoch);
    std::lock_guard lock(self->mutex);
    if (self->retired || !self->connected || epoch != self->epoch)
      co_return onebot11::retired_profile();
    co_return publish(response, origin);
  }
  asio::io_context &ioc;
  adapter::onebot11::ProtocolAdapter *adapter;
  std::recursive_mutex mutex;
  asio::strand<asio::io_context::executor_type> strand;
  asio::steady_timer reconnect_timer;
  std::shared_ptr<detail::ActionRequestTracker> requests;
  std::shared_ptr<WebsocketClient> client;
  EventCallback event_callback;
  std::shared_ptr<st::StandardEventCapabilityV1> standard_events;
  onebot11::OriginCapture origin_capture;
  std::function<void(bool)> connection_callback;
  std::uint64_t epoch{};
  bool connected{};
  bool running{};
  bool retired{};
  std::string host;
  std::uint16_t port{0};
  std::string access_token;
  std::chrono::milliseconds action_timeout{30000};
};

struct WebSocketConnectionManager::StandardRequest
    : std::enable_shared_from_this<StandardRequest> {
  using FailureKind = sd::StandardTransportFailureKind;
  StandardRequest(std::shared_ptr<ConnectionState> state,
                  std::shared_ptr<sd::StandardCallState> call_state,
                  std::shared_ptr<core::SelfIdentityCache> identity_owner,
                  core::SelfIdentityOrigin original_origin, std::uint64_t key)
      : connection(std::move(state)), call(std::move(call_state)),
        identity(std::move(identity_owner)), origin(std::move(original_origin)),
        echo(key), joined(connection->strand, 1) {}

  auto with_authority(const std::function<void()> &capture) -> bool {
    std::lock_guard lock(connection->mutex);
    if (connection->retired || !connection->running || !connection->connected ||
        connection->client != client || connection->epoch != epoch || !client ||
        !identity)
      return false;
    return identity->observe_user_and_capture(origin, std::nullopt, capture)
        .accepted();
  }
  void retire() {
    std::shared_ptr<detail::StandardPendingAction> current;
    {
      std::lock_guard lock(registration_mutex);
      current = pending;
    }
    if (current)
      connection->requests->retire_standard(current);
  }
  void fail(FailureKind kind, boost::system::error_code error) {
    (void)call->try_fail({kind, error});
    retire();
  }
  auto admit() -> bool {
    bool committed = false;
    bool capture_failed = false;
    const bool current = with_authority([&]() noexcept {
      try {
        committed = call->try_commit_write();
      } catch (...) {
        capture_failed = true;
      }
    });
    if (!committed) {
      const auto kind = !current ? FailureKind::ConnectionRetired
                        : std::chrono::steady_clock::now() >= call->deadline()
                            ? FailureKind::DeadlineExceeded
                            : FailureKind::TransportError;
      fail(kind, capture_failed ? asio::error::fault
                                : asio::error::operation_aborted);
    }
    return current && committed;
  }
  void cancel(asio::cancellation_type_t type) {
    if (type == asio::cancellation_type::none)
      return;
    // Call-state arbitration is synchronous on delivery, including after
    // commitment. Only signal access crosses to the owned serialized domain.
    fail(FailureKind::Cancelled, asio::error::operation_aborted);
    asio::post(connection->strand, [weak = weak_from_this(), type] {
      if (auto self = weak.lock(); self && self->send_active)
        self->send_cancellation.emit(type);
    });
  }
  static auto send(std::shared_ptr<StandardRequest> self)
      -> asio::awaitable<void> {
    const auto cancellation = co_await asio::this_coro::cancellation_state;
    if (self->call->terminal() ||
        cancellation.cancelled() != asio::cancellation_type::none)
      co_return;
    co_await asio::this_coro::reset_cancellation_state(
        asio::enable_total_cancellation{});
    if (self->call->terminal())
      co_return;
    co_await self->client->send(std::move(self->payload),
                                [self] { return self->admit(); });
  }
  void send_finished(std::exception_ptr failure) {
    send_active = false;
    send_cancellation.slot().clear();
    if (failure) {
      boost::system::error_code code;
      try {
        std::rethrow_exception(failure);
      } catch (const boost::system::system_error &error) {
        code = error.code();
      } catch (...) {
      }
      fail(std::chrono::steady_clock::now() >= call->deadline()
               ? FailureKind::DeadlineExceeded
               : FailureKind::TransportError,
           code);
    }
    (void)joined.try_send(boost::system::error_code{}, true);
  }
  static auto coordinate(std::shared_ptr<StandardRequest> self)
      -> asio::awaitable<void> {
    // Separate owned cleanup domain; the caller's filters/policy are untouched.
    co_await asio::this_coro::reset_cancellation_state(
        asio::disable_cancellation{});
    if (self->call->terminal())
      co_return;
    if (!self->with_authority([] {})) {
      self->fail(FailureKind::ConnectionRetired,
                 asio::error::operation_aborted);
      co_return;
    }
    if (std::chrono::steady_clock::now() >= self->call->deadline()) {
      self->fail(FailureKind::DeadlineExceeded, asio::error::timed_out);
      co_return;
    }
    std::shared_ptr<detail::StandardPendingAction> request;
    bool started = false;
    std::exception_ptr failure;
    try {
      self->connection->requests->start_standard(
          self->echo, self->epoch, self->client, self->call, request);
      {
        std::lock_guard lock(self->registration_mutex);
        self->pending = request;
      }
      // Includes cancellation between publication and assignment. Even an
      // already-terminal registration must pass through both cleanup joins.
      self->retire();
      if (!self->call->terminal()) {
        self->send_active = true;
        asio::co_spawn(
            self->connection->strand, send(self),
            asio::bind_cancellation_slot(self->send_cancellation.slot(),
                                         [self](std::exception_ptr error) {
                                           self->send_finished(error);
                                         }));
        started = true;
        // Observe terminal concurrently with send, never after awaiting send.
        co_await request->wait_terminal();
      }
    } catch (...) {
      // Before publication retain the original outer exception handling.
      if (!request)
        throw;
      failure = std::current_exception();
      self->fail(FailureKind::TransportError, asio::error::fault);
    }
    // Use the publication output, not just pending: start/assignment can fail
    // after the map has made this registration visible to other threads.
    if (request)
      self->connection->requests->retire_standard(request);
    if (started) {
      if (self->send_active)
        self->send_cancellation.emit(asio::cancellation_type::all);
      (void)co_await self->joined.async_receive(asio::use_awaitable);
    } else {
      self->send_active = false;
      self->send_cancellation.slot().clear();
    }
    if (request)
      co_await request->join_deadline();
    if (failure)
      std::rethrow_exception(failure);
  }
  static auto run(std::shared_ptr<StandardRequest> self)
      -> asio::awaitable<void> {
    if (self->call->terminal())
      co_return;
    const auto cancellation = co_await asio::this_coro::cancellation_state;
    auto slot = cancellation.slot();
    if (slot.has_handler())
      throw std::logic_error(
          "standard WS caller cancellation slot is occupied");
    struct Cleanup {
      asio::cancellation_slot slot;
      ~Cleanup() { slot.clear(); }
    } cleanup{slot};
    // Caller slot install/delivery/clear obey the caller's Asio serialization
    // contract, not an assumption that any_io_executor is itself a strand.
    if (slot.is_connected())
      slot.assign(
          [self](asio::cancellation_type_t type) { self->cancel(type); });
    if (cancellation.cancelled() != asio::cancellation_type::none) {
      self->cancel(cancellation.cancelled());
      co_return;
    }
    try {
      // Disconnected binding preserves our interceptor until the single join
      // completes. No subsequent caller await needs slot rearming.
      co_await asio::co_spawn(
          self->connection->strand, coordinate(self),
          asio::bind_cancellation_slot(asio::cancellation_slot{},
                                       asio::use_awaitable));
    } catch (const std::bad_alloc &) {
      throw;
    } catch (const boost::system::system_error &error) {
      self->fail(FailureKind::TransportError, error.code());
    } catch (const std::exception &) {
      self->fail(FailureKind::TransportError, {});
    }
  }
  std::shared_ptr<ConnectionState> connection;
  std::shared_ptr<sd::StandardCallState> call;
  std::shared_ptr<core::SelfIdentityCache> identity;
  core::SelfIdentityOrigin origin;
  const std::uint64_t echo;
  std::shared_ptr<WebsocketClient> client;
  std::uint64_t epoch{};
  std::string payload;
  std::mutex registration_mutex;
  std::shared_ptr<detail::StandardPendingAction> pending;
  asio::cancellation_signal send_cancellation;
  bool send_active{}; // connection->strand only
  asio::experimental::concurrent_channel<void(boost::system::error_code, bool)>
      joined;
};

auto WebSocketConnectionManager::send_standard_action(
    sd::NativeAction action, st::ExecutionMode mode, std::string parameters,
    std::uint64_t echo, std::shared_ptr<sd::StandardCallState> call,
    std::shared_ptr<core::SelfIdentityCache> identity,
    core::SelfIdentityOrigin origin) -> asio::awaitable<void> {
  if (!call)
    throw std::invalid_argument("standard WS call state is required");
  if (parameters.size() > call->limits().maximum_parameter_bytes)
    throw std::invalid_argument("standard WS parameters exceed byte limit");
  (void)sd::native_action_name(action, mode);
  auto request = std::make_shared<StandardRequest>(connection_, std::move(call),
                                                   std::move(identity),
                                                   std::move(origin), echo);
  {
    std::lock_guard lock(connection_->mutex);
    request->client = connection_->client;
    request->epoch = connection_->epoch;
    if (!request->with_authority([] {}))
      request->fail(StandardRequest::FailureKind::ConnectionRetired,
                    asio::error::operation_aborted);
  }
  if (!request->call->terminal())
    request->payload =
        detail::make_standard_ws_envelope(action, mode, parameters, echo);
  return StandardRequest::run(std::move(request));
}
auto WebSocketConnectionManager::send_file_query(
    obcx::onebot11::bot::detail::FileQueryKind kind, std::string parameters,
    std::uint64_t echo, std::shared_ptr<sd::StandardCallState> call,
    std::shared_ptr<core::SelfIdentityCache> identity,
    core::SelfIdentityOrigin origin) -> asio::awaitable<void> {
  if (!call || parameters.size() > call->limits().maximum_parameter_bytes)
    throw std::invalid_argument("QQ file WS query exceeds call bounds");
  const auto name = obcx::onebot11::bot::detail::file_query_name(kind);
  auto request = std::make_shared<StandardRequest>(connection_, std::move(call),
                                                   std::move(identity),
                                                   std::move(origin), echo);
  {
    std::lock_guard lock(connection_->mutex);
    request->client = connection_->client;
    request->epoch = connection_->epoch;
    if (!request->with_authority([] {}))
      request->fail(StandardRequest::FailureKind::ConnectionRetired,
                    asio::error::operation_aborted);
  }
  if (!request->call->terminal()) {
    // Names are a closed enum; params is the owned, typed endpoint
    // serialization. Preserve the shared tracker's namespace without adding
    // vendor operations to the public OneBot11-standard NativeAction inventory.
    request->payload =
        "{\"action\":\"" + std::string(name) + "\",\"params\":" + parameters +
        ",\"echo\":\"obcx.standard:" + std::to_string(echo) + "\"}";
  }
  return StandardRequest::run(std::move(request));
}

WebSocketConnectionManager::WebSocketConnectionManager(
    asio::io_context &ioc, adapter::onebot11::ProtocolAdapter &adapter,
    detail::ActionDeadlineFactory factory)
    : connection_(std::make_shared<ConnectionState>(ioc, adapter,
                                                    std::move(factory))) {}
WebSocketConnectionManager::~WebSocketConnectionManager() {
  try {
    connection_->shutdown(true);
  } catch (...) {
    OBCX_ERROR("Failed to shut down WebSocket connection manager");
  }
}
void WebSocketConnectionManager::connect(
    const common::ConnectionConfig &config) {
  connection_->connect(config);
}
void WebSocketConnectionManager::disconnect() { connection_->shutdown(false); }
auto WebSocketConnectionManager::is_connected() const -> bool {
  std::lock_guard lock(connection_->mutex);
  return connection_->connected;
}
void WebSocketConnectionManager::set_standard_events(
    std::shared_ptr<st::StandardEventCapabilityV1> events) {
  std::lock_guard lock(connection_->mutex);
  connection_->standard_events = std::move(events);
}
void WebSocketConnectionManager::set_event_callback(EventCallback callback) {
  std::lock_guard lock(connection_->mutex);
  connection_->event_callback = std::move(callback);
}
void WebSocketConnectionManager::set_origin_capture(
    onebot11::OriginCapture capture) {
  std::lock_guard lock(connection_->mutex);
  connection_->origin_capture = std::move(capture);
}
void WebSocketConnectionManager::set_connection_callback(
    std::function<void(bool)> callback) {
  std::lock_guard lock(connection_->mutex);
  connection_->connection_callback = std::move(callback);
}
auto WebSocketConnectionManager::get_send_strand()
    -> asio::strand<asio::io_context::executor_type> & {
  return connection_->strand;
}
auto WebSocketConnectionManager::get_connection_type() const -> std::string {
  return "WebSocket";
}
auto WebSocketConnectionManager::send_action_and_wait_async(
    std::string payload, std::uint64_t echo,
    std::optional<std::uint64_t> expected_epoch)
    -> asio::awaitable<std::string> {
  return ConnectionState::send_legacy(connection_, std::move(payload), echo,
                                      expected_epoch);
}
auto WebSocketConnectionManager::send_profile_action(
    std::string payload, std::uint64_t echo, core::SelfIdentityOrigin origin,
    onebot11::OriginValidation validate, onebot11::ProfilePublication publish)
    -> asio::awaitable<core::SelfIdentityCache::Result> {
  return ConnectionState::send_profile(connection_, std::move(payload), echo,
                                       std::move(origin), std::move(validate),
                                       std::move(publish));
}
} // namespace obcx::network
