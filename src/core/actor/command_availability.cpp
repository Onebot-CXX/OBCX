#include "core/actor/command_availability.hpp"

#include <algorithm>

namespace obcx::command {
namespace {

auto valid_id(const std::string_view value) noexcept -> bool {
  return !value.empty() && value.size() <= 1024 &&
         std::ranges::all_of(value, [](const unsigned char c) {
           return c >= 0x20U && c != 0x7FU;
         });
}

} // namespace

auto valid_scope(const GroupScope &scope) noexcept -> bool {
  if ((scope.platform != "telegram" && scope.platform != "qq") ||
      !valid_id(scope.bot) || !valid_id(scope.group_id) ||
      (scope.platform == "qq" && scope.topic != TopicSelection::None)) {
    return false;
  }
  switch (scope.topic) {
  case TopicSelection::None:
  case TopicSelection::Any:
    return !scope.topic_id;
  case TopicSelection::Exact:
    return scope.topic_id && *scope.topic_id > 0;
  }
  return false;
}

auto matches(const GroupScope &scope, const Subject &subject) noexcept -> bool {
  if (!valid_scope(scope) || subject.conversation != ConversationKind::Group ||
      subject.platform != scope.platform || subject.bot != scope.bot ||
      subject.group_id != scope.group_id ||
      (subject.topic_id &&
       (subject.platform != "telegram" || *subject.topic_id <= 0))) {
    return false;
  }
  switch (scope.topic) {
  case TopicSelection::None:
    return !subject.topic_id;
  case TopicSelection::Exact:
    return subject.topic_id == scope.topic_id;
  case TopicSelection::Any:
    return true;
  }
  return false;
}

auto matches(const std::span<const GroupScope> scopes,
             const Subject &subject) noexcept -> bool {
  return std::ranges::any_of(scopes, [&subject](const auto &scope) {
    return matches(scope, subject);
  });
}

} // namespace obcx::command
