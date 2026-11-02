#include "onebot11/bot/user_target.hpp"
#include "onebot11/bot/domain_binding.hpp"

namespace obcx::onebot11::bot {
auto user_identity_view(const UserRef &user) -> UserIdentityView {
  QqNativeReferenceCodec codec;
  codec.validate(user);
  return {user, std::stoll(codec.unpack(user.id).components.front())};
}
auto user_target_matches(const UserTarget &target, const UserRef &user)
    -> bool {
  validate_user_target(target);
  const auto view = user_identity_view(user);
  return std::visit(
      [&](const auto &value) {
        using V = std::decay_t<decltype(value)>;
        if constexpr (std::same_as<V, UserRef>)
          return value == user;
        else
          return value.installation == user.installation &&
                 value.user_id == view.user_id;
      },
      target);
}
} // namespace obcx::onebot11::bot
