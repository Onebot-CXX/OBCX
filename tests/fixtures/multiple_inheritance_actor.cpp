#include "core/actor/reflected_actor.hpp"

namespace obcx::tests::events {
struct MultipleInheritanceProbe {};
inline void to_json(common::json &json, const MultipleInheritanceProbe &) {
  json = common::json::object();
}
inline void from_json(const common::json &, MultipleInheritanceProbe &) {}
} // namespace obcx::tests::events

namespace {

class PrefixInterface {
public:
  virtual ~PrefixInterface() = default;
  [[nodiscard]] virtual auto marker() const -> int { return 7; }
};

class MultipleInheritanceActor final
    : public PrefixInterface,
      public obcx::core::ReflectedActor<MultipleInheritanceActor> {
public:
  static constexpr std::string_view actor_name = "multiple_inheritance_actor";
  static constexpr std::string_view actor_version = "1.0.0";

  auto handle(const obcx::tests::events::MultipleInheritanceProbe &,
              const obcx::core::MessageEnvelope &message,
              obcx::core::ActorContext &context) -> obcx::core::ActorResult {
    (void)message;
    (void)context;
    return obcx::core::ActorResult::success();
  }
};

} // namespace

OBCX_ACTOR_EXPORT_V2(MultipleInheritanceActor)
