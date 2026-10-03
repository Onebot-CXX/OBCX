#include "onebot11/bot/transport.hpp"
#include "onebot11/network/http/connection_manager.hpp"
#include "onebot11/network/websocket/connection_manager.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace obcx::core {
namespace {

auto legacy_config(
    const obcx::onebot11::configuration::WebSocketConnection &typed)
    -> common::ConnectionConfig {
  common::ConnectionConfig config;
  config.host = typed.host;
  config.port = typed.port;
  config.access_token = typed.access_token;
  config.connect_timeout = typed.connect_timeout;
  config.action_timeout = typed.action_timeout;
  return config;
}

auto legacy_config(const obcx::onebot11::configuration::HttpConnection &typed)
    -> common::ConnectionConfig {
  common::ConnectionConfig config;
  config.host = typed.host;
  config.port = typed.port;
  config.access_token = typed.access_token;
  config.use_ssl = typed.use_tls;
  config.connect_timeout = typed.connect_timeout;
  config.action_timeout = typed.action_timeout;
  return config;
}

auto transport_descriptor(std::string component_id,
                          const std::string_view protocol_capability)
    -> ComponentDescriptor {
  return {
      .id = ComponentId{std::move(component_id)},
      .provides = {CapabilityId{
          std::string{obcx::onebot11::bot::capability_ids::transport}}},
      .required = {CapabilityId{std::string{protocol_capability}}},
  };
}

} // namespace

class OneBot11TransportCapability::Impl {
public:
  using Manager =
      std::variant<std::monostate,
                   std::unique_ptr<network::WebSocketConnectionManager>,
                   std::unique_ptr<network::HttpConnectionManager>>;

  Impl(boost::asio::io_context &io_context, Config config)
      : io_context(&io_context), config(std::move(config)) {}

  boost::asio::io_context *io_context;
  Config config;
  std::shared_ptr<adapter::onebot11::ProtocolAdapter> protocol;
  Manager manager;
  EventCallback event_callback;
  bool running{};
};

OneBot11TransportCapability::OneBot11TransportCapability(
    boost::asio::io_context &io_context, Config config)
    : impl_(std::make_unique<Impl>(io_context, std::move(config))) {}

OneBot11TransportCapability::~OneBot11TransportCapability() { stop(); }

void OneBot11TransportCapability::configure(
    std::shared_ptr<adapter::onebot11::ProtocolAdapter> protocol) {
  if (protocol == nullptr ||
      !std::holds_alternative<std::monostate>(impl_->manager)) {
    throw BotComponentRuntimeError(
        "OneBot transport can only be configured once with a protocol");
  }
  impl_->protocol = std::move(protocol);
  if (const auto *websocket =
          std::get_if<obcx::onebot11::configuration::WebSocketConnection>(
              &impl_->config)) {
    (void)websocket;
    impl_->manager = std::make_unique<network::WebSocketConnectionManager>(
        *impl_->io_context, *impl_->protocol);
  } else {
    const auto &http =
        std::get<obcx::onebot11::configuration::HttpConnection>(impl_->config);
    auto manager = std::make_unique<network::HttpConnectionManager>(
        *impl_->io_context, *impl_->protocol);
    manager->set_poll_interval(http.poll_interval);
    impl_->manager = std::move(manager);
  }
  if (impl_->event_callback) {
    set_event_callback(impl_->event_callback);
  }
}

void OneBot11TransportCapability::start() {
  if (impl_->running) {
    return;
  }
  if (auto *manager =
          std::get_if<std::unique_ptr<network::WebSocketConnectionManager>>(
              &impl_->manager)) {
    (*manager)->connect(legacy_config(
        std::get<obcx::onebot11::configuration::WebSocketConnection>(
            impl_->config)));
  } else if (auto *manager =
                 std::get_if<std::unique_ptr<network::HttpConnectionManager>>(
                     &impl_->manager)) {
    (*manager)->connect(
        legacy_config(std::get<obcx::onebot11::configuration::HttpConnection>(
            impl_->config)));
  } else {
    throw BotComponentRuntimeError("OneBot transport is not configured");
  }
  impl_->running = true;
}

void OneBot11TransportCapability::stop() noexcept {
  try {
    if (auto *manager =
            std::get_if<std::unique_ptr<network::WebSocketConnectionManager>>(
                &impl_->manager)) {
      (*manager)->disconnect();
    } else if (auto *manager =
                   std::get_if<std::unique_ptr<network::HttpConnectionManager>>(
                       &impl_->manager)) {
      (*manager)->disconnect();
    }
  } catch (...) { // NOLINT(bugprone-empty-catch)
  }
  impl_->running = false;
}

void OneBot11TransportCapability::set_event_callback(EventCallback callback) {
  impl_->event_callback = std::move(callback);
  if (auto *manager =
          std::get_if<std::unique_ptr<network::WebSocketConnectionManager>>(
              &impl_->manager)) {
    (*manager)->set_event_callback(impl_->event_callback);
  } else if (auto *manager =
                 std::get_if<std::unique_ptr<network::HttpConnectionManager>>(
                     &impl_->manager)) {
    (*manager)->set_event_callback(impl_->event_callback);
  }
}

auto OneBot11TransportCapability::is_connected() const -> bool {
  if (const auto *manager =
          std::get_if<std::unique_ptr<network::WebSocketConnectionManager>>(
              &impl_->manager)) {
    return (*manager)->is_connected();
  }
  if (const auto *manager =
          std::get_if<std::unique_ptr<network::HttpConnectionManager>>(
              &impl_->manager)) {
    return (*manager)->is_connected();
  }
  return false;
}

auto OneBot11TransportCapability::send_action(std::string payload,
                                              const std::uint64_t echo)
    -> boost::asio::awaitable<std::string> {
  if (auto *manager =
          std::get_if<std::unique_ptr<network::WebSocketConnectionManager>>(
              &impl_->manager)) {
    co_return co_await (*manager)->send_action_and_wait_async(
        std::move(payload), echo);
  }
  if (auto *manager =
          std::get_if<std::unique_ptr<network::HttpConnectionManager>>(
              &impl_->manager)) {
    co_return co_await (*manager)->send_action_and_wait_async(
        std::move(payload), echo);
  }
  throw BotComponentRuntimeError("OneBot transport is not configured");
}

OneBot11WebSocketTransportComponent::OneBot11WebSocketTransportComponent(
    boost::asio::io_context &io_context,
    obcx::onebot11::configuration::WebSocketConnection config)
    : transport_(std::make_shared<OneBot11TransportCapability>(
          io_context, std::move(config))) {}

auto OneBot11WebSocketTransportComponent::descriptor() const
    -> ComponentDescriptor {
  return transport_descriptor("onebot11.transport.websocket",
                              obcx::onebot11::bot::capability_ids::protocol);
}

void OneBot11WebSocketTransportComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<OneBot11Transport>(
      ComponentId{"onebot11.transport.websocket"},
      CapabilityId{std::string{obcx::onebot11::bot::capability_ids::transport}},
      transport_);
}

void OneBot11WebSocketTransportComponent::prepare(
    const CapabilityRegistry &registry) {
  transport_->configure(
      registry.get<adapter::onebot11::ProtocolAdapter>(CapabilityId{
          std::string{obcx::onebot11::bot::capability_ids::protocol}}));
}

void OneBot11WebSocketTransportComponent::start() { transport_->start(); }
void OneBot11WebSocketTransportComponent::stop() { transport_->stop(); }

OneBot11HttpTransportComponent::OneBot11HttpTransportComponent(
    boost::asio::io_context &io_context,
    obcx::onebot11::configuration::HttpConnection config)
    : transport_(std::make_shared<OneBot11TransportCapability>(
          io_context, std::move(config))) {}

auto OneBot11HttpTransportComponent::descriptor() const -> ComponentDescriptor {
  return transport_descriptor("onebot11.transport.http",
                              obcx::onebot11::bot::capability_ids::protocol);
}

void OneBot11HttpTransportComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<OneBot11Transport>(
      ComponentId{"onebot11.transport.http"},
      CapabilityId{std::string{obcx::onebot11::bot::capability_ids::transport}},
      transport_);
}

void OneBot11HttpTransportComponent::prepare(
    const CapabilityRegistry &registry) {
  transport_->configure(
      registry.get<adapter::onebot11::ProtocolAdapter>(CapabilityId{
          std::string{obcx::onebot11::bot::capability_ids::protocol}}));
}

void OneBot11HttpTransportComponent::start() { transport_->start(); }
void OneBot11HttpTransportComponent::stop() { transport_->stop(); }

} // namespace obcx::core
