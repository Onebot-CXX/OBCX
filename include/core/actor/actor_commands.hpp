#ifndef OBCX_INCLUDE_CORE_ACTOR_COMMANDS_HPP_
#define OBCX_INCLUDE_CORE_ACTOR_COMMANDS_HPP_

#include "core/actor/actor.hpp"

#include <array>
#include <concepts>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace obcx::command {

using CommandTransactionId = std::string;

inline constexpr std::string_view processed_header = "obcx.command.processed";
inline constexpr std::string_view name_header = "obcx.command.name";
inline constexpr std::string_view actor_header = "obcx.command.actor";
inline constexpr std::string_view transaction_header =
    "obcx.command.transaction";
inline constexpr std::string_view generation_header = "obcx.command.generation";
inline constexpr std::string_view reply_header = "obcx.command.reply";
inline constexpr std::string_view outcome_header = "obcx.command.outcome";
inline constexpr std::string_view help_name = "help";
inline constexpr std::string_view help_description =
    "List commands available to you";

enum class Propagation : std::uint8_t {
  Continue,
  Consume,
};

struct CommandInvocation {
  CommandTransactionId transaction_id;
  std::string name;
  std::string arguments;
  std::string source_message_id;
  std::string source_platform;
  std::string source_bot;
  std::string conversation_id;
  std::string sender;
  common::json source_event = common::json::object();
};

inline void to_json(common::json &document,
                    const CommandInvocation &invocation) {
  document = {
      {"transaction_id", invocation.transaction_id},
      {"name", invocation.name},
      {"arguments", invocation.arguments},
      {"source_message_id", invocation.source_message_id},
      {"source_platform", invocation.source_platform},
      {"source_bot", invocation.source_bot},
      {"conversation_id", invocation.conversation_id},
      {"sender", invocation.sender},
      {"source_event", invocation.source_event},
  };
}

inline void from_json(const common::json &document,
                      CommandInvocation &invocation) {
  if (!document.is_object()) {
    throw std::invalid_argument("CommandInvocation must be an object");
  }
  document.at("transaction_id").get_to(invocation.transaction_id);
  document.at("name").get_to(invocation.name);
  document.at("arguments").get_to(invocation.arguments);
  document.at("source_message_id").get_to(invocation.source_message_id);
  document.at("source_platform").get_to(invocation.source_platform);
  document.at("source_bot").get_to(invocation.source_bot);
  document.at("conversation_id").get_to(invocation.conversation_id);
  document.at("sender").get_to(invocation.sender);
  document.at("source_event").get_to(invocation.source_event);
}

struct CommandRequestMarker {};

template <typename Derived> struct RequestMessage : CommandRequestMarker {
  CommandInvocation invocation;

  friend void to_json(common::json &document, const Derived &request) {
    document = {{"invocation", request.invocation}};
  }

  friend void from_json(const common::json &document, Derived &request) {
    if (!document.is_object()) {
      throw std::invalid_argument("command request must be an object");
    }
    document.at("invocation").get_to(request.invocation);
  }
};

template <typename Message>
concept CommandRequestMessage =
    std::derived_from<std::remove_cvref_t<Message>, CommandRequestMarker>;

struct CommandCompleted {
  CommandTransactionId transaction_id;
  Propagation propagation = Propagation::Consume;
};

inline void to_json(common::json &document,
                    const CommandCompleted &completion) {
  document = {
      {"transaction_id", completion.transaction_id},
      {"propagation", completion.propagation == Propagation::Continue
                          ? "continue"
                          : "consume"},
  };
}

inline void from_json(const common::json &document,
                      CommandCompleted &completion) {
  if (!document.is_object()) {
    throw std::invalid_argument("CommandCompleted must be an object");
  }
  document.at("transaction_id").get_to(completion.transaction_id);
  const auto propagation = document.at("propagation").get<std::string>();
  if (propagation == "continue") {
    completion.propagation = Propagation::Continue;
  } else if (propagation == "consume") {
    completion.propagation = Propagation::Consume;
  } else {
    throw std::invalid_argument(
        "CommandCompleted propagation must be continue or consume");
  }
}

enum class MatcherKind : std::uint8_t {
  None,
  Re2,
};

struct Re2Pattern {
  std::string_view pattern;
};

[[nodiscard]] consteval auto re2(const std::string_view pattern) -> Re2Pattern {
  return {.pattern = pattern};
}

struct Observation {
  std::string_view name;
  std::string_view description;
  std::string_view request_type;
  bool request_message = false;
  MatcherKind matcher_kind = MatcherKind::None;
  std::string_view matcher_pattern;
};

[[nodiscard]] constexpr auto valid_name(const std::string_view name) -> bool {
  if (name.empty() || name.size() > 32) {
    return false;
  }
  for (const unsigned char character : name) {
    if (!((character >= 'a' && character <= 'z') ||
          (character >= '0' && character <= '9') || character == '_')) {
      return false;
    }
  }
  return true;
}

template <typename Message>
[[nodiscard]] consteval auto observe(const std::string_view name,
                                     const std::string_view description)
    -> Observation {
  using message_type = std::remove_cvref_t<Message>;
  return {
      .name = name,
      .description = description,
      .request_type = core::canonical_message_type_name<message_type>(),
      .request_message = CommandRequestMessage<message_type>,
  };
}

template <typename Message>
[[nodiscard]] consteval auto observe(const std::string_view name,
                                     const std::string_view description,
                                     const Re2Pattern matcher) -> Observation {
  auto observation = observe<Message>(name, description);
  observation.matcher_kind = MatcherKind::Re2;
  observation.matcher_pattern = matcher.pattern;
  return observation;
}

template <typename... Observations>
requires(std::same_as<std::remove_cvref_t<Observations>, Observation> && ...)
[[nodiscard]] consteval auto catalog(Observations... observations) {
  return std::array<Observation, sizeof...(Observations)>{observations...};
}

} // namespace obcx::command

#endif // OBCX_INCLUDE_CORE_ACTOR_COMMANDS_HPP_
