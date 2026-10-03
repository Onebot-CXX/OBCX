#ifndef OBCX_INCLUDE_CORE_BOT_COMPONENT_RUNTIME_HPP_
#define OBCX_INCLUDE_CORE_BOT_COMPONENT_RUNTIME_HPP_

#include "core/bot/component_descriptor.hpp"
#include "core/bot/ids.hpp"

#include <boost/asio/io_context.hpp>

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace obcx::core {

class CapabilityRegistry {
public:
  CapabilityRegistry() = default;
  ~CapabilityRegistry() = default;
  CapabilityRegistry(const CapabilityRegistry &) = delete;
  auto operator=(const CapabilityRegistry &) -> CapabilityRegistry & = delete;
  CapabilityRegistry(CapabilityRegistry &&) = delete;
  auto operator=(CapabilityRegistry &&) -> CapabilityRegistry & = delete;

  template <typename Capability>
  void install(const ComponentId &provider, const CapabilityId &id,
               std::shared_ptr<Capability> capability) {
    if (capability == nullptr) {
      throw BotComponentRuntimeError("capability " + id.value() +
                                     " cannot be null");
    }
    install_erased(provider, id, std::type_index(typeid(Capability)),
                   std::move(capability));
  }

  template <typename Capability>
  [[nodiscard]] auto get(const CapabilityId &id) const
      -> std::shared_ptr<Capability> {
    const auto entry = get_erased(id, std::type_index(typeid(Capability)));
    return std::static_pointer_cast<Capability>(entry);
  }

  [[nodiscard]] auto contains(const CapabilityId &id) const -> bool;
  [[nodiscard]] auto provider(const CapabilityId &id) const -> ComponentId;

private:
  friend class BotInstallation;

  struct Entry {
    ComponentId provider;
    std::type_index type;
    std::shared_ptr<void> value;
  };

  void install_erased(const ComponentId &provider, const CapabilityId &id,
                      std::type_index type, std::shared_ptr<void> value);
  [[nodiscard]] auto get_erased(const CapabilityId &id,
                                std::type_index expected) const
      -> std::shared_ptr<void>;
  void seal();
  void clear() noexcept;
  [[nodiscard]] auto capabilities_for(const ComponentId &provider) const
      -> std::vector<CapabilityId>;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Entry> entries_;
  bool sealed_{};
};

class BotComponent {
public:
  BotComponent() = default;
  BotComponent(const BotComponent &) = delete;
  auto operator=(const BotComponent &) -> BotComponent & = delete;
  BotComponent(BotComponent &&) = delete;
  auto operator=(BotComponent &&) -> BotComponent & = delete;
  virtual ~BotComponent() = default;

  [[nodiscard]] virtual auto descriptor() const -> ComponentDescriptor = 0;
  virtual void install_capabilities(CapabilityRegistry &registry) = 0;
  virtual void prepare(const CapabilityRegistry &registry) = 0;
  virtual void start() = 0;
  virtual void stop() = 0;
};

enum class BotInstallationState : std::uint8_t {
  Constructed,
  Assembled,
  Prepared,
  Running,
  Stopping,
  Stopped,
  Failed,
};

class BotInstallation {
public:
  BotInstallation(std::string installation_id, bot::SurfaceId surface);
  ~BotInstallation();

  BotInstallation(const BotInstallation &) = delete;
  auto operator=(const BotInstallation &) -> BotInstallation & = delete;
  BotInstallation(BotInstallation &&) = delete;
  auto operator=(BotInstallation &&) -> BotInstallation & = delete;

  void add_component(std::unique_ptr<BotComponent> component);
  void assemble();
  void start();
  // Request component cancellation without halting the executor. Join every
  // run() thread before destruction so admitted completions can drain.
  void stop() noexcept;
  void run();

  [[nodiscard]] auto installation_id() const noexcept -> const std::string & {
    return installation_id_;
  }
  [[nodiscard]] auto surface() const noexcept -> const bot::SurfaceId & {
    return surface_;
  }
  [[nodiscard]] auto state() const noexcept -> BotInstallationState {
    return state_.load(std::memory_order_acquire);
  }
  [[nodiscard]] auto accepting_work() const noexcept -> bool {
    return accepting_work_.load(std::memory_order_acquire);
  }
  [[nodiscard]] auto executor() noexcept -> boost::asio::io_context & {
    return io_context_;
  }
  [[nodiscard]] auto lifecycle_order() const -> std::vector<std::string>;

  template <typename Capability>
  [[nodiscard]] auto capability(const CapabilityId &id) const
      -> std::shared_ptr<Capability> {
    return capabilities_.get<Capability>(id);
  }

private:
  void rollback() noexcept;

  // Declared first so executor-dependent components and capabilities are
  // destroyed before the executor during reverse member destruction.
  boost::asio::io_context io_context_;
  CapabilityRegistry capabilities_;
  std::vector<std::unique_ptr<BotComponent>> components_;
  std::vector<ComponentDescriptor> descriptors_;
  std::vector<std::size_t> lifecycle_order_;
  std::vector<std::size_t> prepared_order_;
  std::string installation_id_;
  bot::SurfaceId surface_;
  mutable std::mutex lifecycle_mutex_;
  std::atomic<BotInstallationState> state_{BotInstallationState::Constructed};
  std::atomic_bool accepting_work_{false};
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_BOT_COMPONENT_RUNTIME_HPP_
