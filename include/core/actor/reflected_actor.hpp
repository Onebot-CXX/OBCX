#ifndef OBCX_INCLUDE_CORE_REFLECTED_ACTOR_HPP_
#define OBCX_INCLUDE_CORE_REFLECTED_ACTOR_HPP_

#include "core/actor/actor.hpp"
#include "core/actor/actor_commands.hpp"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <meta>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if !defined(__cpp_expansion_statements) || __cpp_expansion_statements < 202506L
#error "OBCX requires C++26 expansion statements"
#endif

namespace obcx::core {
namespace detail {

template <typename Message>
concept JsonDecodable = requires(const common::json &document) {
  { document.template get<Message>() } -> std::same_as<Message>;
};

template <typename Derived>
consteval auto direct_handle_count() -> std::size_t {
  std::size_t count = 0;
  template for (constexpr auto member :
                std::define_static_array(std::meta::members_of(
                    ^^Derived, std::meta::access_context::unchecked()))) {
    if constexpr (std::meta::is_function(member) &&
                  std::meta::has_identifier(member) &&
                  std::meta::identifier_of(member) == "handle") {
      ++count;
    }
  }
  return count;
}

template <typename Derived>
inline constexpr auto reflected_members = std::define_static_array(
    std::meta::members_of(^^Derived, std::meta::access_context::unchecked()));

template <auto Handler>
consteval auto normalized_handler_input() -> std::meta::info {
  constexpr auto parameters =
      std::define_static_array(std::meta::parameters_of(Handler));
  if constexpr (parameters.size() == 3) {
    return std::meta::dealias(
        std::meta::remove_cvref(std::meta::type_of(parameters[0])));
  }
  return ^^void;
}

template <typename Derived>
consteval auto validate_reflected_handlers() -> bool {
  static_assert(direct_handle_count<Derived>() > 0,
                "OBCX_REFLECTED_ACTOR_NO_HANDLER");

  template for (constexpr auto handler : reflected_members<Derived>) {
    if constexpr (std::meta::is_function(handler) &&
                  std::meta::has_identifier(handler) &&
                  std::meta::identifier_of(handler) == "handle") {
      static_assert(std::meta::is_public(handler),
                    "OBCX_REFLECTED_ACTOR_NON_PUBLIC_HANDLER");
      constexpr auto parameters =
          std::define_static_array(std::meta::parameters_of(handler));
      static_assert(parameters.size() == 3, "OBCX_REFLECTED_ACTOR_WRONG_ARITY");
      if constexpr (parameters.size() == 3) {
        constexpr auto message_parameter = std::meta::type_of(parameters[0]);
        static_assert(std::meta::is_lvalue_reference_type(message_parameter),
                      "OBCX_REFLECTED_ACTOR_MESSAGE_NOT_LVALUE_REF");
        if constexpr (std::meta::is_lvalue_reference_type(message_parameter)) {
          static_assert(std::meta::is_const_type(
                            std::meta::remove_reference(message_parameter)),
                        "OBCX_REFLECTED_ACTOR_MESSAGE_NOT_CONST");
        }
        static_assert(std::meta::type_of(parameters[1]) ==
                          ^^const MessageEnvelope &,
                      "OBCX_REFLECTED_ACTOR_WRONG_ENVELOPE_PARAMETER");
        static_assert(std::meta::type_of(parameters[2]) == ^^ActorContext &,
                      "OBCX_REFLECTED_ACTOR_WRONG_CONTEXT_PARAMETER");

        constexpr auto message = normalized_handler_input<handler>();
        using message_type = typename[:message:];
        static_assert(canonical_message_name_storage<message_type>[0] != '\0',
                      "OBCX_REFLECTED_MESSAGE_UNSTABLE_IDENTITY");
        static_assert(JsonDecodable<message_type>,
                      "OBCX_REFLECTED_ACTOR_MISSING_JSON_DESERIALIZATION");

        constexpr auto result = std::meta::return_type_of(handler);
        static_assert(result == ^^ActorResult ||
                          result == ^^ActorTask<ActorResult>,
                      "OBCX_REFLECTED_ACTOR_BAD_RETURN");

        template for (constexpr auto other : reflected_members<Derived>) {
          if constexpr (handler != other && std::meta::is_function(other) &&
                        std::meta::has_identifier(other) &&
                        std::meta::identifier_of(other) == "handle") {
            constexpr auto other_parameters =
                std::define_static_array(std::meta::parameters_of(other));
            if constexpr (other_parameters.size() == 3) {
              static_assert(message != normalized_handler_input<other>(),
                            "OBCX_REFLECTED_ACTOR_DUPLICATE_INPUT");
            }
          }
        }
      }
    }
  }
  return true;
}

template <typename Derived>
inline constexpr bool reflected_handlers_valid =
    validate_reflected_handlers<Derived>();

template <typename Derived> auto accepted_inputs() -> std::vector<std::string> {
  static_assert(reflected_handlers_valid<Derived>);
  std::vector<std::string> result;
  template for (constexpr auto handler : reflected_members<Derived>) {
    if constexpr (std::meta::is_function(handler) &&
                  std::meta::has_identifier(handler) &&
                  std::meta::identifier_of(handler) == "handle") {
      constexpr auto parameters =
          std::define_static_array(std::meta::parameters_of(handler));
      if constexpr (parameters.size() == 3) {
        using message_type = typename[:normalized_handler_input<handler>():];
        result.emplace_back(canonical_message_name_storage<message_type>);
      }
    }
  }
  std::ranges::sort(result);
  return result;
}

template <typename Derived>
consteval auto has_reflected_input(const std::string_view request_type)
    -> bool {
  template for (constexpr auto handler : reflected_members<Derived>) {
    if constexpr (std::meta::is_function(handler) &&
                  std::meta::has_identifier(handler) &&
                  std::meta::identifier_of(handler) == "handle") {
      constexpr auto parameters =
          std::define_static_array(std::meta::parameters_of(handler));
      if constexpr (parameters.size() == 3) {
        using message_type = typename[:normalized_handler_input<handler>():];
        if (canonical_message_name_storage<message_type> == request_type) {
          return true;
        }
      }
    }
  }
  return false;
}

template <std::size_t Size>
consteval auto command_name_count(
    const std::array<command::Observation, Size> &commands,
    const std::string_view name) -> std::size_t {
  std::size_t count = 0;
  for (const auto &entry : commands) {
    if (entry.name == name) {
      ++count;
    }
  }
  return count;
}

template <typename Derived> consteval auto validate_command_contract() -> bool {
  if constexpr (!requires { Derived::command_contract(); }) {
    return true;
  } else {
    static constexpr auto commands = Derived::command_contract();
    template for (constexpr auto entry : commands) {
      static_assert(entry.request_message,
                    "OBCX_COMMAND_REQUEST_TYPE_REQUIRED");
      static_assert(command::valid_name(entry.name),
                    "OBCX_COMMAND_INVALID_NAME");
      static_assert(entry.name != command::help_name,
                    "OBCX_COMMAND_RESERVED_NAME");
      static_assert(!entry.description.empty(),
                    "OBCX_COMMAND_EMPTY_DESCRIPTION");
      static_assert(entry.matcher_kind == command::MatcherKind::None ||
                        entry.matcher_kind == command::MatcherKind::Re2,
                    "OBCX_COMMAND_MATCHER_KIND_UNSUPPORTED");
      static_assert(entry.matcher_kind == command::MatcherKind::None ||
                        !entry.matcher_pattern.empty(),
                    "OBCX_COMMAND_MATCHER_PATTERN_EMPTY");
      static_assert(has_reflected_input<Derived>(entry.request_type),
                    "OBCX_COMMAND_REQUEST_INPUT_MISSING");
      static_assert(command_name_count(commands, entry.name) == 1,
                    "OBCX_COMMAND_DUPLICATE_NAME");
    }
    return true;
  }
}

template <typename Derived>
inline constexpr bool command_contract_valid =
    validate_command_contract<Derived>();

template <typename Derived> auto command_registrations() -> common::json {
  static_assert(command_contract_valid<Derived>);
  auto document = common::json::array();
  if constexpr (requires { Derived::command_contract(); }) {
    auto commands = Derived::command_contract();
    std::ranges::sort(commands, {}, &command::Observation::name);
    for (const auto &entry : commands) {
      common::json registration = {
          {"name", entry.name},
          {"description", entry.description},
          {"request_type", entry.request_type},
      };
      if (entry.matcher_kind == command::MatcherKind::Re2) {
        registration["matcher"] = {
            {"kind", "re2"},
            {"pattern", entry.matcher_pattern},
            {"mode", "full"},
        };
      }
      document.push_back(std::move(registration));
    }
  }
  return document;
}

inline auto invalid_payload_failure(const std::string_view actor,
                                    const MessageEnvelope &envelope)
    -> ActorResult {
  return ActorResult::failed("invalid_message_payload",
                             "actor " + std::string{actor} +
                                 " could not decode canonical input " +
                                 envelope.type + " for message " + envelope.id,
                             false);
}

inline auto unsupported_type_failure(const std::string_view actor,
                                     const MessageEnvelope &envelope)
    -> ActorResult {
  return ActorResult::failed("unsupported_message_type",
                             "actor " + std::string{actor} +
                                 " does not accept canonical input " +
                                 envelope.type + " for message " + envelope.id,
                             false);
}

template <auto Handler>
using handler_message_type = typename[:normalized_handler_input<Handler>():];

template <auto Handler>
inline constexpr bool handler_returns_sync =
    std::meta::return_type_of(Handler) == ^^ActorResult;

template <typename Derived, auto Handler>
auto dispatch_reflected_handler(Derived &actor, const MessageEnvelope &envelope,
                                ActorContext &context)
    -> ActorTask<ActorResult> {
  using message_type = handler_message_type<Handler>;
  std::shared_ptr<message_type> decoded;
  try {
    decoded = std::make_shared<message_type>(
        envelope.payload.template get<message_type>());
  } catch (...) {
    co_return invalid_payload_failure(Derived::actor_name, envelope);
  }

  if constexpr (handler_returns_sync<Handler>) {
    co_return actor.[:Handler:](*decoded, envelope, context);
  } else {
    auto nested = actor.[:Handler:](*decoded, envelope, context);
    nested.attach_runtime(co_await current_actor_task_runtime());
    while (!nested.done()) {
      nested.resume();
      if (nested.done()) {
        break;
      }
      const auto suspension = nested.suspension();
      if (suspension != ActorTaskSuspension::Yielded &&
          suspension != ActorTaskSuspension::AwaitingIo) {
        co_return ActorResult::failed(
            "actor_invalid_suspension",
            "reflected async handler suspended outside a supported actor "
            "runtime boundary",
            false);
      }
      co_await forward_actor_task_suspension(suspension,
                                             nested.io_suspension_epoch());
    }
    co_return nested.take_result();
  }
}

template <typename Derived>
auto select_reflected_handler(Derived &actor, const MessageEnvelope &envelope,
                              ActorContext &context)
    -> std::optional<ActorTask<ActorResult>> {
  // Do not cache actor-specific dispatch metadata in a function-local static.
  // GCC emits that storage as STB_GNU_UNIQUE, so separately staged copies of
  // the same actor DSO can share a table whose string views and function
  // pointers belong to a retired generation.
  static_assert(reflected_handlers_valid<Derived>);
  template for (constexpr auto handler : reflected_members<Derived>) {
    if constexpr (std::meta::is_function(handler) &&
                  std::meta::has_identifier(handler) &&
                  std::meta::identifier_of(handler) == "handle") {
      constexpr auto parameters =
          std::define_static_array(std::meta::parameters_of(handler));
      if constexpr (parameters.size() == 3) {
        using message_type = handler_message_type<handler>;
        if (envelope.type == canonical_message_name_storage<message_type>) {
          return dispatch_reflected_handler<Derived, handler>(actor, envelope,
                                                              context);
        }
      }
    }
  }
  return std::nullopt;
}

template <typename Derived>
auto dispatch_reflected_message(Derived &actor, const MessageEnvelope &envelope,
                                ActorContext &context)
    -> ActorTask<ActorResult> {
  auto selected = select_reflected_handler(actor, envelope, context);
  if (!selected) {
    co_return unsupported_type_failure(Derived::actor_name, envelope);
  }

  auto dispatched = std::move(*selected);
  dispatched.attach_runtime(co_await current_actor_task_runtime());
  while (!dispatched.done()) {
    dispatched.resume();
    if (dispatched.done()) {
      break;
    }
    const auto suspension = dispatched.suspension();
    if (suspension != ActorTaskSuspension::Yielded &&
        suspension != ActorTaskSuspension::AwaitingIo) {
      co_return ActorResult::failed(
          "actor_invalid_suspension",
          "generated reflected dispatch suspended outside a supported actor "
          "runtime boundary",
          false);
    }
    co_await forward_actor_task_suspension(suspension,
                                           dispatched.io_suspension_epoch());
  }
  co_return dispatched.take_result();
}

} // namespace detail

namespace detail {

template <typename Derived>
concept GenerationPreparable = requires(Derived &actor, ActorContext &context) {
  { actor.prepare_generation(context) } -> std::same_as<ActorPreparationResult>;
};

template <typename Derived>
auto prepare_actor_generation_export(void *actor_ptr, ActorContext *context)
    -> ActorPreparationExportResult {
  static thread_local std::string diagnostic;
  diagnostic.clear();
  if (actor_ptr == nullptr || context == nullptr) {
    diagnostic = "actor generation preparation received a null argument";
    return {.status =
                static_cast<std::uint32_t>(ActorPreparationStatus::Failed),
            .message = diagnostic.c_str()};
  }

  if constexpr (!GenerationPreparable<Derived>) {
    return {};
  } else {
    try {
      auto *base = static_cast<IActorV2 *>(actor_ptr);
      auto result = static_cast<Derived *>(base)->prepare_generation(*context);
      switch (result.status) {
      case ActorPreparationStatus::Ready:
      case ActorPreparationStatus::Failed:
      case ActorPreparationStatus::RestartRequired:
        break;
      default:
        result = ActorPreparationResult::failed(
            "actor generation preparation returned an invalid status");
        break;
      }
      diagnostic = std::move(result.message);
      return {.status = static_cast<std::uint32_t>(result.status),
              .message = diagnostic.empty() ? nullptr : diagnostic.c_str()};
    } catch (const std::exception &error) {
      diagnostic = error.what();
    } catch (...) {
      diagnostic = "actor generation preparation threw an unknown exception";
    }
    return {.status =
                static_cast<std::uint32_t>(ActorPreparationStatus::Failed),
            .message = diagnostic.c_str()};
  }
}

} // namespace detail

template <typename Derived> class ReflectedActor : public IActorV2 {
public:
  [[nodiscard]] auto get_name() const -> std::string final {
    return std::string{Derived::actor_name};
  }

  [[nodiscard]] auto get_version() const -> std::string final {
    return std::string{Derived::actor_version};
  }

  auto handle_message(const MessageEnvelope &envelope, ActorContext &context)
      -> ActorTask<ActorResult> final {
    static_assert(detail::reflected_handlers_valid<Derived>);
    static_assert(detail::command_contract_valid<Derived>);
    return detail::dispatch_reflected_message(static_cast<Derived &>(*this),
                                              envelope, context);
  }

  [[nodiscard]] static auto input_contract_json() -> const std::string & {
    static const std::string contract = [] {
      common::json document = {
          {"schema_version", 2},
          {"actor", Derived::actor_name},
          {"accepted_inputs", detail::accepted_inputs<Derived>()},
      };
      if constexpr (requires { Derived::configuration_contract(); }) {
        document["configuration"] = Derived::configuration_contract();
      }
      if constexpr (requires { Derived::command_contract(); }) {
        document["commands"] = detail::command_registrations<Derived>();
      }
      return document.dump();
    }();
    return contract;
  }
};

#define OBCX_ACTOR_EXPORT_V2(ActorClass)                                       \
  static_assert(                                                               \
      std::is_base_of_v<::obcx::core::ReflectedActor<ActorClass>, ActorClass>, \
      "OBCX_ACTOR_EXPORT_V2_REQUIRES_REFLECTED_ACTOR");                        \
  extern "C" {                                                                 \
  auto obcx_get_actor_abi_generation() -> std::uint32_t {                      \
    return ::obcx::core::OBCX_ACTOR_ABI_GENERATION_V2;                         \
  }                                                                            \
  void *obcx_create_actor_v2() {                                               \
    try {                                                                      \
      return static_cast<::obcx::core::IActorV2 *>(new ActorClass());          \
    } catch (...) {                                                            \
      return nullptr;                                                          \
    }                                                                          \
  }                                                                            \
  void obcx_destroy_actor_v2(void *actor) {                                    \
    if (actor != nullptr) {                                                    \
      try {                                                                    \
        delete static_cast<::obcx::core::IActorV2 *>(actor);                   \
      } catch (...) {                                                          \
      }                                                                        \
    }                                                                          \
  }                                                                            \
  auto obcx_prepare_actor_generation_v2(void *actor,                           \
                                        ::obcx::core::ActorContext *context)   \
      -> ::obcx::core::ActorPreparationExportResult {                          \
    return ::obcx::core::detail::prepare_actor_generation_export<ActorClass>(  \
        actor, context);                                                       \
  }                                                                            \
  auto obcx_get_actor_name_v2() -> const char * {                              \
    static const std::string name{ActorClass::actor_name};                     \
    return name.c_str();                                                       \
  }                                                                            \
  auto obcx_get_actor_version_v2() -> const char * {                           \
    static const std::string version{ActorClass::actor_version};               \
    return version.c_str();                                                    \
  }                                                                            \
  auto obcx_get_actor_contract() -> const char * {                             \
    return ActorClass::input_contract_json().c_str();                          \
  }                                                                            \
  }

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_REFLECTED_ACTOR_HPP_
