#ifndef OBCX_INCLUDE_CORE_ACTOR_HPP_
#define OBCX_INCLUDE_CORE_ACTOR_HPP_

#include "common/config_snapshot.hpp"
#include "common/json_utils.hpp"
#include "core/actor/actor_asio.hpp"
#include "core/actor/actor_task.hpp"
#include "core/actor/blocking_executor.hpp"

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <concepts>
#include <cstdint>
#include <exception>
#include <memory>
#include <meta>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#if !defined(__cpp_impl_reflection) || __cpp_impl_reflection < 202506L
#error                                                                         \
    "OBCX requires GCC 16 C++26 reflection (__cpp_impl_reflection >= 202506L)"
#endif

namespace obcx::core {

enum class ActorAbiGeneration : std::uint32_t {
  Unknown = 0,
  V2 = 2,
};

inline constexpr auto OBCX_ACTOR_ABI_GENERATION_V2 =
    static_cast<std::uint32_t>(ActorAbiGeneration::V2);

namespace detail {

consteval auto canonical_message_name(std::meta::info type) -> std::string {
  type = std::meta::dealias(type);
  if (!std::meta::is_class_type(type) && !std::meta::is_enum_type(type)) {
    throw std::meta::exception("OBCX_REFLECTED_MESSAGE_NOT_CLASS_OR_ENUM",
                               type);
  }

  std::vector<std::string_view> reversed;
  auto current = type;
  while (true) {
    if (!std::meta::has_identifier(current)) {
      throw std::meta::exception("OBCX_REFLECTED_MESSAGE_UNSTABLE_IDENTITY",
                                 current);
    }
    reversed.push_back(std::meta::identifier_of(current));
    if (!std::meta::has_parent(current)) {
      break;
    }
    current = std::meta::parent_of(current);
    if (!std::meta::is_namespace(current) &&
        (!std::meta::is_type(current) || !std::meta::is_class_type(current))) {
      throw std::meta::exception("OBCX_REFLECTED_MESSAGE_LOCAL_TYPE", current);
    }
    if (!std::meta::has_parent(current)) {
      break;
    }
  }

  std::string result;
  for (auto it = reversed.rbegin(); it != reversed.rend(); ++it) {
    if (!result.empty()) {
      result += "::";
    }
    result += *it;
  }
  return result;
}

template <typename Message>
inline constexpr auto canonical_message_name_storage =
    std::define_static_string(
        canonical_message_name(std::meta::dealias(^^Message)));

template <typename Message>
concept JsonEncodable =
    requires(const Message &message) { common::json(message); };

} // namespace detail

template <typename Message>
[[nodiscard]] consteval auto canonical_message_type_name() -> std::string_view {
  return detail::canonical_message_name_storage<std::remove_cvref_t<Message>>;
}

using ActorId = std::string;

enum class ActorGenerationPurpose {
  Startup,
  ValidationOnly,
  ReloadCandidate,
};

struct ActorGenerationInfo {
  ActorGenerationPurpose purpose = ActorGenerationPurpose::Startup;
  std::uint64_t generation_id = 0;
};

enum class ActorPreparationStatus : std::uint32_t {
  Ready = 0,
  Failed = 1,
  RestartRequired = 2,
};

struct ActorPreparationResult {
  ActorPreparationStatus status = ActorPreparationStatus::Ready;
  std::string message;

  [[nodiscard]] static auto ready() -> ActorPreparationResult { return {}; }

  [[nodiscard]] static auto failed(std::string message)
      -> ActorPreparationResult {
    return {.status = ActorPreparationStatus::Failed,
            .message = std::move(message)};
  }

  [[nodiscard]] static auto restart_required(std::string message)
      -> ActorPreparationResult {
    return {.status = ActorPreparationStatus::RestartRequired,
            .message = std::move(message)};
  }

  [[nodiscard]] auto ok() const noexcept -> bool {
    return status == ActorPreparationStatus::Ready;
  }
};

// Additive C-ABI result used by the optional generation-preparation export.
// The pointed-to diagnostic remains owned by the actor DSO and is copied by
// the runtime before the call returns to application code.
struct ActorPreparationExportResult {
  std::uint32_t status =
      static_cast<std::uint32_t>(ActorPreparationStatus::Ready);
  const char *message = nullptr;
};

struct MessageEnvelope {
  std::string id;
  std::string type;
  std::string source_platform;
  std::string source_bot;
  std::string conversation_id;
  std::string correlation_id;
  std::string causation_id;
  std::chrono::system_clock::time_point timestamp =
      std::chrono::system_clock::now();
  common::json payload = common::json::object();
  common::json raw = common::json::object();
  std::unordered_map<std::string, std::string> headers;
};

struct ActorInvocation {
  std::string actor_id;
  std::string partition_key = "global";
  std::string db_instance;
  std::string db_namespace;
  MessageEnvelope message;
};

struct ActorFailure {
  std::string code;
  std::string message;
  bool retryable = false;
};

struct ActorEmitOptions {
  std::optional<std::string> id;
  std::optional<std::string> source_platform;
  std::optional<std::string> source_bot;
  std::optional<std::string> conversation_id;
  std::optional<std::string> correlation_id;
  std::optional<std::string> causation_id;
  std::unordered_map<std::string, std::string> headers;
  bool replace_headers = false;
};

struct ActorResult {
  std::vector<MessageEnvelope> emitted;
  std::optional<ActorFailure> failure;

  [[nodiscard]] static auto success() -> ActorResult { return {}; }

  [[nodiscard]] static auto failed(std::string code, std::string message,
                                   bool retryable = false) -> ActorResult {
    ActorResult result;
    result.failure = ActorFailure{.code = std::move(code),
                                  .message = std::move(message),
                                  .retryable = retryable};
    return result;
  }

  [[nodiscard]] auto ok() const -> bool { return !failure.has_value(); }

  void emit(MessageEnvelope envelope) {
    emitted.push_back(std::move(envelope));
  }

  template <typename Message>
  void emit(const Message &message, const MessageEnvelope &parent,
            ActorEmitOptions options = {});
};

template <typename Message>
void ActorResult::emit(const Message &message, const MessageEnvelope &parent,
                       ActorEmitOptions options) {
  using message_type = std::remove_cvref_t<Message>;
  static_assert(detail::JsonEncodable<message_type>,
                "OBCX_REFLECTED_ACTOR_MISSING_JSON_SERIALIZATION");

  MessageEnvelope envelope;
  envelope.type = canonical_message_type_name<message_type>();
  envelope.id = options.id.value_or(parent.id +
                                    ":emit:" + std::to_string(emitted.size()) +
                                    ":" + envelope.type);
  envelope.source_platform =
      options.source_platform.value_or(parent.source_platform);
  envelope.source_bot = options.source_bot.value_or(parent.source_bot);
  envelope.conversation_id =
      options.conversation_id.value_or(parent.conversation_id);
  envelope.correlation_id = options.correlation_id.value_or(
      parent.correlation_id.empty() ? parent.id : parent.correlation_id);
  envelope.causation_id = options.causation_id.value_or(parent.id);
  envelope.headers =
      options.replace_headers ? decltype(envelope.headers){} : parent.headers;
  for (auto &[key, value] : options.headers) {
    envelope.headers.insert_or_assign(std::move(key), std::move(value));
  }
  envelope.payload = common::json(message);
  emit(std::move(envelope));
}

class ActorServices {
public:
  template <typename Service>
  void register_service(std::shared_ptr<Service> service) {
    services_[std::type_index(typeid(Service))] = std::move(service);
  }

  template <typename Service>
  [[nodiscard]] auto get_service() const -> std::shared_ptr<Service> {
    auto it = services_.find(std::type_index(typeid(Service)));
    if (it == services_.end()) {
      return nullptr;
    }
    return std::static_pointer_cast<Service>(it->second);
  }

private:
  std::unordered_map<std::type_index, std::shared_ptr<void>> services_;
};

class ActorContext {
public:
  ActorContext() = default;
  explicit ActorContext(ActorId actor_id) : actor_id_(std::move(actor_id)) {}
  ActorContext(ActorId actor_id, std::shared_ptr<ActorServices> services)
      : actor_id_(std::move(actor_id)), runtime_services_(std::move(services)) {
  }
  ActorContext(ActorId actor_id, std::shared_ptr<ActorServices> services,
               std::string db_instance, std::string db_namespace,
               std::shared_ptr<ActorCancellationState> cancellation = nullptr,
               std::shared_ptr<void> actor_lifetime = nullptr)
      : actor_id_(std::move(actor_id)), runtime_services_(std::move(services)),
        db_instance_(std::move(db_instance)),
        db_namespace_(std::move(db_namespace)),
        cancellation_(std::move(cancellation)),
        actor_lifetime_(std::move(actor_lifetime)) {}

  [[nodiscard]] auto actor_id() const -> const ActorId & { return actor_id_; }
  [[nodiscard]] auto db_instance() const -> const std::string & {
    return db_instance_;
  }
  [[nodiscard]] auto db_namespace() const -> const std::string & {
    return db_namespace_;
  }

  [[nodiscard]] auto config() const -> common::ActorConfigView {
    const auto service = get_service<common::ActorConfigService>();
    return service == nullptr ? common::ActorConfigView{}
                              : service->for_actor(actor_id_);
  }

  template <typename Service>
  void register_service(std::shared_ptr<Service> service) {
    actor_services_.register_service<Service>(std::move(service));
  }

  template <typename Service>
  [[nodiscard]] auto get_service() const -> std::shared_ptr<Service> {
    if (auto local = actor_services_.get_service<Service>()) {
      return local;
    }
    if (runtime_services_) {
      return runtime_services_->get_service<Service>();
    }
    return nullptr;
  }

  [[nodiscard]] auto yield() const -> ActorYieldAwaiter {
    return ActorYieldAwaiter{cancellation_};
  }

  template <typename Factory>
  [[nodiscard]] auto await_asio(boost::asio::any_io_executor executor,
                                Factory &&factory) const {
    using factory_type = std::decay_t<Factory>;
    using awaitable_type = std::invoke_result_t<factory_type>;
    return detail::ActorAsioAwaiter<awaitable_type, factory_type>{
        std::move(executor), std::forward<Factory>(factory), actor_lifetime_};
  }

  template <BlockingCallable Func>
  [[nodiscard]] auto run_blocking(Func &&function) const {
    using function_type = std::decay_t<Func>;
    using result_type = detail::BlockingCallableResult<Func>;
    auto blocking_executor = get_service<BlockingExecutor>();
    if (!blocking_executor) {
      throw BlockingExecutorUnavailable{};
    }
    auto io_executor = get_service<boost::asio::any_io_executor>();
    if (!io_executor) {
      throw std::logic_error(
          "ActorContext::run_blocking requires a runtime I/O executor");
    }

    return await_asio(
        *io_executor,
        [blocking_executor,
         function = function_type(std::forward<Func>(function))]() mutable
            -> boost::asio::awaitable<result_type,
                                      boost::asio::any_io_executor> {
          if constexpr (std::is_void_v<result_type>) {
            co_await blocking_executor->run(std::move(function));
            co_return;
          } else {
            co_return co_await blocking_executor->run(std::move(function));
          }
        });
  }

  [[nodiscard]] auto asio_token(boost::asio::any_io_executor executor) const
      -> ActorAsioCompletionToken {
    return ActorAsioCompletionToken{std::move(executor)};
  }

  [[nodiscard]] auto cancellation_requested() const noexcept -> bool {
    return cancellation_ && cancellation_->cancellation_requested();
  }

  void throw_if_cancelled() const {
    if (cancellation_requested()) {
      throw ActorTaskCancelled{};
    }
  }

  void attach_cancellation_state(
      std::shared_ptr<ActorCancellationState> cancellation) {
    cancellation_ = std::move(cancellation);
  }

private:
  ActorId actor_id_;
  ActorServices actor_services_;
  std::shared_ptr<ActorServices> runtime_services_;
  std::string db_instance_;
  std::string db_namespace_;
  std::shared_ptr<ActorCancellationState> cancellation_;
  std::shared_ptr<void> actor_lifetime_;
};

class IActorV2 {
public:
  IActorV2() = default;
  IActorV2(const IActorV2 &) = delete;
  auto operator=(const IActorV2 &) -> IActorV2 & = delete;
  IActorV2(IActorV2 &&) = delete;
  auto operator=(IActorV2 &&) -> IActorV2 & = delete;
  virtual ~IActorV2() = default;

  [[nodiscard]] virtual auto get_name() const -> std::string = 0;
  [[nodiscard]] virtual auto get_version() const -> std::string = 0;

  virtual auto handle_message(const MessageEnvelope &message,
                              ActorContext &context)
      -> ActorTask<ActorResult> = 0;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_ACTOR_HPP_
