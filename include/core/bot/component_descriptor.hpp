#ifndef OBCX_INCLUDE_CORE_BOT_COMPONENT_DESCRIPTOR_HPP_
#define OBCX_INCLUDE_CORE_BOT_COMPONENT_DESCRIPTOR_HPP_

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace obcx::core {

class BotComponentRuntimeError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class ComponentId {
public:
  explicit ComponentId(std::string value);

  [[nodiscard]] auto value() const noexcept -> const std::string & {
    return value_;
  }
  auto operator==(const ComponentId &) const -> bool = default;

private:
  std::string value_;
};

class CapabilityId {
public:
  explicit CapabilityId(std::string value);

  [[nodiscard]] auto value() const noexcept -> const std::string & {
    return value_;
  }
  auto operator==(const CapabilityId &) const -> bool = default;

private:
  std::string value_;
};

struct ComponentDescriptor {
  ComponentId id;
  std::vector<CapabilityId> provides;
  std::vector<CapabilityId> required;

  auto operator==(const ComponentDescriptor &) const -> bool = default;
};

struct ComponentRecipeValidation {
  std::vector<std::size_t> lifecycle_order;
};

[[nodiscard]] auto validate_component_recipe(
    const std::vector<ComponentDescriptor> &components)
    -> ComponentRecipeValidation;

} // namespace obcx::core

#endif
