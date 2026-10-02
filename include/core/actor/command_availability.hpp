#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obcx::command {

enum class ConversationKind : std::uint8_t { Group, Private };

struct Subject {
  std::string platform;
  std::string bot;
  ConversationKind conversation;
  std::string group_id;
  std::string user_id;
  std::optional<std::int64_t> topic_id;
};

enum class TopicSelection : std::uint8_t { None, Exact, Any };

struct GroupScope {
  std::string platform;
  std::string bot;
  std::string group_id;
  TopicSelection topic;
  std::optional<std::int64_t> topic_id;

  auto operator<=>(const GroupScope &) const = default;
};

using GroupScopes = std::vector<GroupScope>;

[[nodiscard]] auto valid_scope(const GroupScope &scope) noexcept -> bool;
[[nodiscard]] auto matches(const GroupScope &scope,
                           const Subject &subject) noexcept -> bool;
[[nodiscard]] auto matches(std::span<const GroupScope> scopes,
                           const Subject &subject) noexcept -> bool;

// Supplied only to the owning actor during generation preparation. The
// implementation copies values into core-owned storage; no actor callback is
// retained. A missing publication is distinct from an explicit empty set.
class AvailabilityPublisher {
public:
  virtual ~AvailabilityPublisher() = default;
  virtual void publish(std::string_view command, const GroupScopes &scopes) = 0;
};

} // namespace obcx::command
