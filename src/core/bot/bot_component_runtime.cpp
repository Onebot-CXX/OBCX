#include "core/bot/bot_component_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_set>

namespace obcx::core {
namespace {

auto valid_stable_id(const std::string_view value) -> bool {
  if (value.empty() || value.size() > 128) {
    return false;
  }
  return std::ranges::all_of(value, [](const unsigned char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '.' ||
           character == '_' || character == '-';
  });
}

auto descriptor_ids(const std::vector<CapabilityId> &ids)
    -> std::set<std::string> {
  std::set<std::string> values;
  for (const auto &id : ids) {
    if (!values.emplace(id.value()).second) {
      throw BotComponentRuntimeError("duplicate capability declaration: " +
                                     id.value());
    }
  }
  return values;
}

} // namespace

ComponentId::ComponentId(std::string value) : value_(std::move(value)) {
  if (!valid_stable_id(value_)) {
    throw BotComponentRuntimeError("invalid stable component id");
  }
}

CapabilityId::CapabilityId(std::string value) : value_(std::move(value)) {
  if (!valid_stable_id(value_)) {
    throw BotComponentRuntimeError("invalid stable capability id");
  }
}

auto validate_component_recipe(
    const std::vector<ComponentDescriptor> &components)
    -> ComponentRecipeValidation {
  std::unordered_map<std::string, std::size_t> component_indexes;
  std::unordered_map<std::string, std::size_t> providers;
  for (std::size_t index = 0; index < components.size(); ++index) {
    const auto &component = components[index];
    if (!component_indexes.emplace(component.id.value(), index).second) {
      throw BotComponentRuntimeError("duplicate component id: " +
                                     component.id.value());
    }
    (void)descriptor_ids(component.required);
    for (const auto &capability : descriptor_ids(component.provides)) {
      const auto [existing, inserted] = providers.emplace(capability, index);
      if (!inserted) {
        throw BotComponentRuntimeError(
            "duplicate capability provider: " + capability + " from " +
            components[existing->second].id.value() + " and " +
            component.id.value());
      }
    }
  }

  std::vector<std::vector<std::size_t>> dependents(components.size());
  std::vector<std::size_t> in_degree(components.size());
  for (std::size_t consumer = 0; consumer < components.size(); ++consumer) {
    std::unordered_set<std::size_t> dependencies;
    for (const auto &required : components[consumer].required) {
      const auto provider = providers.find(required.value());
      if (provider == providers.end()) {
        throw BotComponentRuntimeError(
            "component " + components[consumer].id.value() +
            " requires missing capability " + required.value());
      }
      if (provider->second != consumer &&
          dependencies.emplace(provider->second).second) {
        dependents[provider->second].push_back(consumer);
        ++in_degree[consumer];
      }
    }
  }

  std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>>
      ready;
  for (std::size_t index = 0; index < in_degree.size(); ++index) {
    if (in_degree[index] == 0) {
      ready.push(index);
    }
  }

  ComponentRecipeValidation validation;
  validation.lifecycle_order.reserve(components.size());
  while (!ready.empty()) {
    const auto component = ready.top();
    ready.pop();
    validation.lifecycle_order.push_back(component);
    for (const auto dependent : dependents[component]) {
      if (--in_degree[dependent] == 0) {
        ready.push(dependent);
      }
    }
  }
  if (validation.lifecycle_order.size() != components.size()) {
    std::ostringstream cycle;
    for (std::size_t index = 0; index < in_degree.size(); ++index) {
      if (in_degree[index] != 0) {
        cycle << (cycle.tellp() == std::streampos{} ? "" : ",")
              << components[index].id.value();
      }
    }
    throw BotComponentRuntimeError("component dependency cycle: " +
                                   cycle.str());
  }
  return validation;
}

void CapabilityRegistry::install_erased(const ComponentId &provider,
                                        const CapabilityId &id,
                                        const std::type_index type,
                                        std::shared_ptr<void> value) {
  std::scoped_lock lock(mutex_);
  if (sealed_) {
    throw BotComponentRuntimeError("capability registry is sealed");
  }
  const auto [existing, inserted] = entries_.emplace(
      id.value(),
      Entry{.provider = provider, .type = type, .value = std::move(value)});
  if (!inserted) {
    throw BotComponentRuntimeError(
        "duplicate capability provider for " + id.value() + ": " +
        existing->second.provider.value() + " and " + provider.value());
  }
}

auto CapabilityRegistry::get_erased(const CapabilityId &id,
                                    const std::type_index expected) const
    -> std::shared_ptr<void> {
  std::scoped_lock lock(mutex_);
  const auto entry = entries_.find(id.value());
  if (entry == entries_.end()) {
    throw BotComponentRuntimeError("capability is not installed: " +
                                   id.value());
  }
  if (entry->second.type != expected) {
    throw BotComponentRuntimeError("capability type mismatch: " + id.value());
  }
  return entry->second.value;
}

auto CapabilityRegistry::contains(const CapabilityId &id) const -> bool {
  std::scoped_lock lock(mutex_);
  return entries_.contains(id.value());
}

auto CapabilityRegistry::provider(const CapabilityId &id) const -> ComponentId {
  std::scoped_lock lock(mutex_);
  const auto entry = entries_.find(id.value());
  if (entry == entries_.end()) {
    throw BotComponentRuntimeError("capability is not installed: " +
                                   id.value());
  }
  return entry->second.provider;
}

void CapabilityRegistry::seal() {
  std::scoped_lock lock(mutex_);
  sealed_ = true;
}

void CapabilityRegistry::clear() noexcept {
  std::scoped_lock lock(mutex_);
  entries_.clear();
  sealed_ = false;
}

auto CapabilityRegistry::capabilities_for(const ComponentId &provider) const
    -> std::vector<CapabilityId> {
  std::scoped_lock lock(mutex_);
  std::vector<CapabilityId> capabilities;
  for (const auto &[id, entry] : entries_) {
    if (entry.provider == provider) {
      capabilities.emplace_back(id);
    }
  }
  return capabilities;
}

BotInstallation::BotInstallation(std::string installation_id,
                                 const bot::SurfaceId surface)
    : installation_id_(std::move(installation_id)), surface_(surface) {
  if (installation_id_.empty() || installation_id_.size() > 128) {
    throw BotComponentRuntimeError("invalid bot installation id");
  }
}

BotInstallation::~BotInstallation() {
  stop();
  // Runners must be joined before destruction. Drain any remaining stop work
  // (including installations that were never run) while components are alive.
  if (io_context_.stopped()) {
    io_context_.restart();
  }
  io_context_.run();
  components_.clear();
  capabilities_.clear();
}

void BotInstallation::add_component(std::unique_ptr<BotComponent> component) {
  std::scoped_lock lock(lifecycle_mutex_);
  if (state() != BotInstallationState::Constructed) {
    throw BotComponentRuntimeError(
        "components can only be added before installation assembly");
  }
  if (component == nullptr) {
    throw BotComponentRuntimeError("bot component cannot be null");
  }
  components_.push_back(std::move(component));
}

void BotInstallation::assemble() {
  std::scoped_lock lock(lifecycle_mutex_);
  if (state() != BotInstallationState::Constructed) {
    throw BotComponentRuntimeError(
        "bot installation can only be assembled once");
  }
  try {
    descriptors_.clear();
    descriptors_.reserve(components_.size());
    for (const auto &component : components_) {
      descriptors_.push_back(component->descriptor());
    }
    lifecycle_order_ = validate_component_recipe(descriptors_).lifecycle_order;

    for (std::size_t index = 0; index < components_.size(); ++index) {
      components_[index]->install_capabilities(capabilities_);
      const auto declared = descriptor_ids(descriptors_[index].provides);
      const auto installed = descriptor_ids(
          capabilities_.capabilities_for(descriptors_[index].id));
      if (declared != installed) {
        throw BotComponentRuntimeError(
            "component capability installation differs from declaration: " +
            descriptors_[index].id.value());
      }
    }
    capabilities_.seal();
    state_.store(BotInstallationState::Assembled, std::memory_order_release);
  } catch (...) { // NOLINT(bugprone-empty-catch)
    capabilities_.clear();
    lifecycle_order_.clear();
    state_.store(BotInstallationState::Failed, std::memory_order_release);
    throw;
  }
}

void BotInstallation::start() {
  if (state() == BotInstallationState::Constructed) {
    assemble();
  }

  std::scoped_lock lock(lifecycle_mutex_);
  if (state() != BotInstallationState::Assembled) {
    throw BotComponentRuntimeError(
        "only an assembled bot installation can start");
  }
  try {
    prepared_order_.clear();
    for (const auto index : lifecycle_order_) {
      prepared_order_.push_back(index);
      components_[index]->prepare(capabilities_);
    }
    state_.store(BotInstallationState::Prepared, std::memory_order_release);
    for (const auto index : lifecycle_order_) {
      components_[index]->start();
    }
    accepting_work_.store(true, std::memory_order_release);
    state_.store(BotInstallationState::Running, std::memory_order_release);
  } catch (...) { // NOLINT(bugprone-empty-catch)
    rollback();
    throw;
  }
}

void BotInstallation::rollback() noexcept {
  accepting_work_.store(false, std::memory_order_release);
  for (auto component = prepared_order_.rbegin();
       component != prepared_order_.rend(); ++component) {
    try {
      components_[*component]->stop();
    } catch (...) { // NOLINT(bugprone-empty-catch)
    }
  }
  prepared_order_.clear();
  try {
    (void)io_context_.poll();
  } catch (...) { // NOLINT(bugprone-empty-catch)
  }
  // Pending cancellation work must be allowed to finish before destruction.
  state_.store(BotInstallationState::Failed, std::memory_order_release);
}

void BotInstallation::stop() noexcept {
  std::vector<std::size_t> stop_order;
  {
    std::scoped_lock lock(lifecycle_mutex_);
    const auto current = state();
    if (current == BotInstallationState::Stopping ||
        current == BotInstallationState::Stopped) {
      return;
    }
    if (current == BotInstallationState::Constructed) {
      accepting_work_.store(false, std::memory_order_release);
      io_context_.stop();
      state_.store(BotInstallationState::Stopped, std::memory_order_release);
      return;
    }
    state_.store(BotInstallationState::Stopping, std::memory_order_release);
    accepting_work_.store(false, std::memory_order_release);
    stop_order.assign(prepared_order_.rbegin(), prepared_order_.rend());
    prepared_order_.clear();
  }

  for (const auto component : stop_order) {
    try {
      components_[component]->stop();
    } catch (...) { // NOLINT(bugprone-empty-catch)
    }
  }
  try {
    (void)io_context_.poll();
  } catch (...) { // NOLINT(bugprone-empty-catch)
  }
  // Do not stop the context: a handler may still be executing on a runner and
  // enqueue more cleanup after poll() returns. Joining runners is the drain
  // barrier; the application bounds that join with its shutdown deadline.
  state_.store(BotInstallationState::Stopped, std::memory_order_release);
}

void BotInstallation::run() {
  if (state() != BotInstallationState::Running) {
    throw BotComponentRuntimeError(
        "bot installation executor requires a running installation");
  }
  if (io_context_.stopped()) {
    io_context_.restart();
  }
  io_context_.run();
}

auto BotInstallation::lifecycle_order() const -> std::vector<std::string> {
  std::scoped_lock lock(lifecycle_mutex_);
  std::vector<std::string> order;
  order.reserve(lifecycle_order_.size());
  for (const auto index : lifecycle_order_) {
    order.push_back(descriptors_[index].id.value());
  }
  return order;
}

} // namespace obcx::core
