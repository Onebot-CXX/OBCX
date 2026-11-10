#pragma once

#include "core/bot/native_reference_codec.hpp"
#include "onebot11/bot/standard_common.hpp"
#include <algorithm>
#include <stdexcept>

namespace obcx::onebot11::bot::standard::detail {
[[noreturn]] inline void invalid_content() {
  throw std::invalid_argument("invalid standard send content");
}

// One owned-value/work budget for borrowed input and retained output. Bytes
// are raw string/key bytes, not JSON framing or physical buffer residency.
class ContentBudget {
public:
  explicit ContentBudget(const ValueLimits &limits) : limits_(limits) {}
  void bytes(std::size_t count) {
    add(bytes_, count,
        std::min(limits_.maximum_bytes, obcx::bot::detail::bot_maximum_bytes));
  }
  void node(std::size_t depth) {
    if (depth > std::min<std::size_t>(limits_.maximum_depth, 32))
      invalid_content();
    add(nodes_, 1,
        std::min(limits_.maximum_nodes, obcx::bot::detail::bot_maximum_items));
  }
  void string(std::string_view value) {
    std::size_t length = 0;
    string_part(length, value.size());
    obcx::bot::detail::validate_utf8(value);
  }
  // The caller keeps one length for the ENTIRE generated string, even when
  // escaping emits many chunks or concatenates multiple leaves.
  void string_part(std::size_t &length, std::size_t count) {
    add(length, count,
        std::min(limits_.maximum_text_bytes,
                 obcx::bot::detail::bot_maximum_bytes));
    bytes(count);
  }
  void string_value(std::string_view value, std::size_t depth) {
    node(depth);
    string(value);
  }

  auto limits() const noexcept -> const ValueLimits & { return limits_; }

private:
  static void add(std::size_t &used, std::size_t count, std::size_t limit) {
    if (count > limit - used)
      invalid_content();
    used += count;
  }
  const ValueLimits &limits_;
  std::size_t bytes_{};
  std::size_t nodes_{};
};

inline void inspect_installation(
    ContentBudget &budget, const obcx::bot::BotInstallationRef &installation,
    std::size_t depth) {
  budget.node(depth);
  budget.string_value(installation.installation_id, depth + 1);
  budget.string_value(installation.surface.value(), depth + 1);
}

template <class Ref>
void inspect_reference(ContentBudget &budget, const Ref &ref,
                       std::size_t depth) {
  budget.node(depth);
  inspect_installation(budget, ref.installation, depth + 1);
  // Inspect the original opaque spelling before codec validation unpacks it.
  budget.string_value(ref.id.serialized(), depth + 1);
}

} // namespace obcx::onebot11::bot::standard::detail
