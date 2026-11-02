#pragma once

#include "common/message_type.hpp"
#include "core/bot/bot_component_runtime.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "onebot11/bot/capability_ids.hpp"
#include "onebot11/bot/configuration.hpp"
#include "onebot11/bot/standard_event_runtime.hpp"
#include "onebot11/network/receive_origin.hpp"
#include <boost/asio/awaitable.hpp>
#include <functional>
#include <optional>
#include <variant>

namespace obcx::onebot11::bot::standard::detail {
struct StandardTransportAccess;
}
namespace obcx::onebot11::bot::detail {
struct FileTransportAccess;
}

namespace obcx::core {

class OneBot11Transport {
public:
  using EventCallback = network::onebot11::EventCallback;

  OneBot11Transport() = default;
  OneBot11Transport(const OneBot11Transport &) = delete;
  auto operator=(const OneBot11Transport &) -> OneBot11Transport & = delete;
  OneBot11Transport(OneBot11Transport &&) = delete;
  auto operator=(OneBot11Transport &&) -> OneBot11Transport & = delete;
  virtual ~OneBot11Transport() = default;

  virtual void set_origin_capture(network::onebot11::OriginCapture capture) = 0;
  virtual void set_event_callback(EventCallback callback) = 0;
  virtual void set_connection_callback(std::function<void(bool)> callback) = 0;
  [[nodiscard]] virtual auto is_connected() const -> bool = 0;
  virtual auto send_profile_action(
      std::string payload, std::uint64_t echo, SelfIdentityOrigin origin,
      network::onebot11::OriginValidation validate,
      network::onebot11::ProfilePublication publish)
      -> boost::asio::awaitable<SelfIdentityCache::Result> = 0;
  virtual auto send_action(std::string payload, std::uint64_t echo)
      -> boost::asio::awaitable<std::string> = 0;
};

class OneBot11TransportCapability final : public OneBot11Transport {
public:
  using Config =
      std::variant<obcx::onebot11::configuration::WebSocketConnection,
                   obcx::onebot11::configuration::HttpConnection>;

  OneBot11TransportCapability(boost::asio::io_context &io_context,
                              Config config);
  ~OneBot11TransportCapability() override;

  OneBot11TransportCapability(const OneBot11TransportCapability &) = delete;
  auto operator=(const OneBot11TransportCapability &)
      -> OneBot11TransportCapability & = delete;
  OneBot11TransportCapability(OneBot11TransportCapability &&) = delete;
  auto operator=(OneBot11TransportCapability &&)
      -> OneBot11TransportCapability & = delete;

  void configure(std::shared_ptr<adapter::onebot11::ProtocolAdapter> protocol);
  void set_standard_events(
      std::shared_ptr<
          obcx::onebot11::bot::standard::StandardEventCapabilityV1>);
  void start();
  void stop() noexcept;
  void set_origin_capture(network::onebot11::OriginCapture capture) override;
  void set_event_callback(EventCallback callback) override;
  void set_connection_callback(std::function<void(bool)> callback) override;
  [[nodiscard]] auto is_connected() const -> bool override;
  auto send_profile_action(std::string payload, std::uint64_t echo,
                           SelfIdentityOrigin origin,
                           network::onebot11::OriginValidation validate,
                           network::onebot11::ProfilePublication publish)
      -> boost::asio::awaitable<SelfIdentityCache::Result> override;
  auto send_action(std::string payload, std::uint64_t echo)
      -> boost::asio::awaitable<std::string> override;

private:
  friend struct obcx::onebot11::bot::standard::detail::StandardTransportAccess;
  friend struct obcx::onebot11::bot::detail::FileTransportAccess;

  class Impl;
  std::unique_ptr<Impl> impl_;
};

class OneBot11WebSocketTransportComponent final : public BotComponent {
public:
  OneBot11WebSocketTransportComponent(
      boost::asio::io_context &io_context,
      obcx::onebot11::configuration::WebSocketConnection config);

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  std::shared_ptr<OneBot11TransportCapability> transport_;
};

class OneBot11HttpTransportComponent final : public BotComponent {
public:
  OneBot11HttpTransportComponent(
      boost::asio::io_context &io_context,
      obcx::onebot11::configuration::HttpConnection config);

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  std::shared_ptr<OneBot11TransportCapability> transport_;
};

} // namespace obcx::core
