#include "onebot11/bot/domain_binding.hpp"
#include "onebot11/bot/group_forward.hpp"

namespace obcx::onebot11::bot {
namespace detail {
auto forward_group_width(const ConversationRef &target) -> std::size_t {
  const QqNativeReferenceCodec codec;
  codec.validate(target);
  const auto parts = codec.unpack(target.id);
  if (parts.kind != "group")
    throw std::invalid_argument("QQ forward requires a group conversation");
  return parts.components.front().size();
}

auto forward_user_width(const UserRef &user) -> std::size_t {
  const QqNativeReferenceCodec codec;
  codec.validate(user);
  return codec.unpack(user.id).components.front().size();
}
} // namespace detail

void validate_independent_forward_plan(
    const ConversationRef &target, std::span<const ForwardNodeValue> nodes,
    std::size_t maximum_envelope_bytes,
    const obcx::bot::SendLimits &per_message_limits) {
  per_message_limits.validate();
  if (per_message_limits.maximum_messages != 1 || nodes.empty() ||
      nodes.size() > 99)
    throw std::invalid_argument("invalid independent QQ forward plan");
  detail::ForwardPreflight envelope(
      target, per_message_limits, maximum_envelope_bytes,
      detail::ForwardBudgetScope::IndependentNodes);
  for (const auto &node : nodes)
    std::visit(
        [&](const auto &value) {
          using T = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::same_as<T, MessageRef>) {
            throw std::invalid_argument(
                "opaque QQ forward nodes have no bounded recipe");
          } else {
            value.validate();
            std::size_t known = 0;
            obcx::bot::validate_message_body(target, value.body,
                                             per_message_limits, known);
            if constexpr (std::same_as<T, ForwardNode>)
              envelope.node(value.author, value.display_name, value.body);
            else
              envelope.authorless_node(value.body);
          }
        },
        node);
}
} // namespace obcx::onebot11::bot
