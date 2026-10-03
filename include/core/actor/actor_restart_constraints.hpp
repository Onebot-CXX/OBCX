#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace obcx::core {

// Process-lifetime registry for actor-owned values that cannot change across a
// live generation cutover. Candidates may compare during preparation, but only
// an activated generation publishes values. This service owns no actor policy.
class ActorRestartConstraintRegistry final {
public:
  [[nodiscard]] auto compatible(std::string_view actor, std::string_view key,
                                std::string_view candidate) const -> bool {
    std::scoped_lock lock(mutex_);
    const auto found = values_.find(storage_key(actor, key));
    return found == values_.end() || found->second == candidate;
  }

  void publish(std::string_view actor, std::string_view key,
               std::string value) {
    std::scoped_lock lock(mutex_);
    values_.insert_or_assign(storage_key(actor, key), std::move(value));
  }

private:
  [[nodiscard]] static auto storage_key(std::string_view actor,
                                        std::string_view key) -> std::string {
    std::string result;
    result.reserve(actor.size() + key.size() + 32);
    result += std::to_string(actor.size());
    result.push_back(':');
    result.append(actor);
    result.push_back('|');
    result.append(key);
    return result;
  }

  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::string> values_;
};

} // namespace obcx::core
