#include "core/actor/reflected_actor.hpp"

namespace obcx::compile_tests {

struct Message {
  int value = 0;
};

struct OtherMessage {
  int value = 0;
};

inline void from_json(const common::json &document, Message &message) {
  document.at("value").get_to(message.value);
}

inline void to_json(common::json &document, const Message &message) {
  document = {{"value", message.value}};
}

inline void from_json(const common::json &document, OtherMessage &message) {
  document.at("value").get_to(message.value);
}

inline void to_json(common::json &document, const OtherMessage &message) {
  document = {{"value", message.value}};
}

struct MissingDecode {};
struct MissingEncode {};

struct CommandMessage final : command::RequestMessage<CommandMessage> {};
struct OtherCommandMessage final
    : command::RequestMessage<OtherCommandMessage> {};

struct Outer {
  struct Nested {};
};

inline void from_json(const common::json &, Outer::Nested &) {}
inline void to_json(common::json &document, const Outer::Nested &) {
  document = common::json::object();
}

#if OBCX_CASE == 0
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "positive";
  static constexpr std::string_view actor_version = "1";
  auto handle(const Message &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorResult {
    return core::ActorResult::success();
  }
  auto handle(const OtherMessage &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorTask<core::ActorResult> {
    co_return core::ActorResult::success();
  }
};
#elif OBCX_CASE == 1
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "no_handler";
  static constexpr std::string_view actor_version = "1";
};
#elif OBCX_CASE == 2
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "private_handler";
  static constexpr std::string_view actor_version = "1";

private:
  auto handle(const Message &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorResult;
};
#elif OBCX_CASE == 3
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "wrong_arity";
  static constexpr std::string_view actor_version = "1";
  auto handle(const Message &, core::ActorContext &) -> core::ActorResult;
};
#elif OBCX_CASE == 4
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "wrong_cvref";
  static constexpr std::string_view actor_version = "1";
  auto handle(Message &, const core::MessageEnvelope &, core::ActorContext &)
      -> core::ActorResult;
};
#elif OBCX_CASE == 5
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "wrong_envelope";
  static constexpr std::string_view actor_version = "1";
  auto handle(const Message &, core::MessageEnvelope &, core::ActorContext &)
      -> core::ActorResult;
};
#elif OBCX_CASE == 6
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "wrong_context";
  static constexpr std::string_view actor_version = "1";
  auto handle(const Message &, const core::MessageEnvelope &,
              const core::ActorContext &) -> core::ActorResult;
};
#elif OBCX_CASE == 7
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "bad_return";
  static constexpr std::string_view actor_version = "1";
  auto handle(const Message &, const core::MessageEnvelope &,
              core::ActorContext &) -> int;
};
#elif OBCX_CASE == 8
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "duplicate";
  static constexpr std::string_view actor_version = "1";
  auto handle(const Message &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorResult;
  auto handle(const volatile Message &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorResult;
};
#elif OBCX_CASE == 9
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "missing_decode";
  static constexpr std::string_view actor_version = "1";
  auto handle(const MissingDecode &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorResult;
};
#elif OBCX_CASE == 14
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "valid_command";
  static constexpr std::string_view actor_version = "1";
  static constexpr auto command_contract() {
    return command::catalog(command::observe<CommandMessage>(
        "ping", "Ping the actor", command::re2(R"(^(?:ping|alias)$)")));
  }
  auto handle(const CommandMessage &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorResult;
};
#elif OBCX_CASE == 15
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "missing_command_input";
  static constexpr std::string_view actor_version = "1";
  static constexpr auto command_contract() {
    return command::catalog(
        command::observe<CommandMessage>("ping", "Ping the actor"));
  }
  auto handle(const Message &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorResult;
};
#elif OBCX_CASE == 16
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "duplicate_command";
  static constexpr std::string_view actor_version = "1";
  static constexpr auto command_contract() {
    return command::catalog(
        command::observe<CommandMessage>("ping", "Ping the actor"),
        command::observe<OtherCommandMessage>("ping", "Ping again"));
  }
  auto handle(const CommandMessage &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorResult;
  auto handle(const OtherCommandMessage &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorResult;
};
#elif OBCX_CASE == 17
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "invalid_command_message";
  static constexpr std::string_view actor_version = "1";
  static constexpr auto command_contract() {
    return command::catalog(
        command::observe<Message>("ping", "Ping the actor"));
  }
  auto handle(const Message &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorResult;
};
#elif OBCX_CASE == 18
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "empty_command_pattern";
  static constexpr std::string_view actor_version = "1";
  static constexpr auto command_contract() {
    return command::catalog(command::observe<CommandMessage>(
        "ping", "Ping the actor", command::re2("")));
  }
  auto handle(const CommandMessage &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorResult;
};
#elif OBCX_CASE == 19
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "invalid_command_name";
  static constexpr std::string_view actor_version = "1";
  static constexpr auto command_contract() {
    return command::catalog(command::observe<CommandMessage>(
        "Ping!", "Ping the actor", command::re2(R"(^alias$)")));
  }
  auto handle(const CommandMessage &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorResult;
};
#elif OBCX_CASE == 20
class Actor final : public core::ReflectedActor<Actor> {
public:
  static constexpr std::string_view actor_name = "reserved_command_name";
  static constexpr std::string_view actor_version = "1";
  static constexpr auto command_contract() {
    return command::catalog(
        command::observe<CommandMessage>("help", "Reserved command"));
  }
  auto handle(const CommandMessage &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorResult;
};
#endif

} // namespace obcx::compile_tests

#if OBCX_CASE == 11
namespace {
struct AnonymousMessage {};
} // namespace
#elif OBCX_CASE == 12
struct {
} unnamed_message;
#endif

auto main() -> int {
#if OBCX_CASE <= 9 || (OBCX_CASE >= 14 && OBCX_CASE <= 20)
  using Actor = obcx::compile_tests::Actor;
  return Actor::input_contract_json().empty() ? 1 : 0;
#elif OBCX_CASE == 10
  struct LocalMessage {};
  constexpr auto name = obcx::core::canonical_message_type_name<LocalMessage>();
  return name.empty();
#elif OBCX_CASE == 11
  constexpr auto name =
      obcx::core::canonical_message_type_name<AnonymousMessage>();
  return name.empty();
#elif OBCX_CASE == 12
  using UnnamedMessage = decltype(unnamed_message);
  constexpr auto name =
      obcx::core::canonical_message_type_name<UnnamedMessage>();
  return name.empty();
#elif OBCX_CASE == 13
  obcx::core::ActorResult result;
  obcx::core::MessageEnvelope parent;
  result.emit(obcx::compile_tests::MissingEncode{}, parent);
  return 0;
#endif
}
