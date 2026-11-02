#pragma once

#include "onebot11/bot/types.hpp"

namespace obcx::onebot11::bot {
// Explicit current native query intent, not a reference import or evidence of
// existence/membership. Only a validated provider response produces a UserRef.
struct NativeUserTarget {
  BotInstallationRef installation;
  std::int64_t user_id;
  void validate() const {
    detail::require_onebot(installation, "native QQ user query");
    if (user_id <= 0)
      throw std::invalid_argument("invalid QQ user query identifier");
  }
};
using UserTarget = std::variant<UserRef, NativeUserTarget>;
inline auto user_target_installation(const UserTarget &target)
    -> const BotInstallationRef & {
  return std::visit(
      [](const auto &value) -> const BotInstallationRef & {
        return value.installation;
      },
      target);
}
inline void validate_user_target(const UserTarget &target) {
  std::visit([](const auto &value) { value.validate(); }, target);
  detail::require_onebot(user_target_installation(target), "QQ user query");
}
// A current scoped identity observation for actor-owned display/cache policy.
// No old IDs, native event JSON, URLs or reference-construction API are
// accepted.
struct UserIdentityView {
  UserRef subject;
  std::int64_t user_id;
};
auto user_identity_view(const UserRef &) -> UserIdentityView;
auto user_target_matches(const UserTarget &, const UserRef &) -> bool;
} // namespace obcx::onebot11::bot

namespace obcx::bot {
OBCX_DOMAIN_FIELDS(onebot11::bot::NativeUserTarget,
                   OBCX_DOMAIN_FIELD(installation), OBCX_DOMAIN_FIELD(user_id));
} // namespace obcx::bot
