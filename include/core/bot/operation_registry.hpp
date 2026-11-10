#pragma once

// Process-only infrastructure. Never installed as an Actor SDK header.
#include "core/bot/message_operation_binding.hpp"
#include "core/bot/owned_structured_operation_gateway.hpp"
#include "core/bot/platform_adapter.hpp"
#include "core/bot/reply_evidence_store.hpp"
#include "core/bot/self_identity_cache.hpp"
#include "core/bot/send_gateway_codec.hpp"
#include "core/bot/structured_operation_gateway.hpp"
#include "core/bot/typed_operation.hpp"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <typeindex>
#include <vector>

namespace obcx::core {

struct OperationDescription {
  bot::ActionId action;
  bool side_effecting;
  std::vector<std::string> required_capabilities;
  bool message_producing;

  auto operator==(const OperationDescription &) const -> bool = default;
};

enum class StructuredCompletionDisposition {
  Completed,
  AcceptedPending,
  Failed
};

struct StructuredOperationCompletion {
  bot::StructuredOperationReply reply;
  StructuredCompletionDisposition disposition;
  std::optional<bot::SendOutcome> message_outcome;
  std::vector<ConfirmedAuthorEvidence> confirmed_authors;
};

template <bot::TypedOperation Request> class OperationDefinition;
template <bot::TypedOperation Request> class StructuredOperationDefinition;
class OperationRegistry;
struct MessageAdmissionGeneration {
  SelfObservationResult observation;
  std::optional<std::uint64_t> evidence_generation;
};

class OperationBinding {
public:
  [[nodiscard]] auto description() const -> const OperationDescription & {
    return description_;
  }

private:
  template <bot::TypedOperation Request> friend class OperationDefinition;
  template <bot::TypedOperation Request>
  friend class StructuredOperationDefinition;
  template <bot::TypedOperation Request>
  friend class OwnedStructuredOperationDefinition;
  template <bot::TypedOperation Request>
  friend class StructuredMessageOperationDefinition;
  template <bot::TypedOperation Request>
  friend class OwnedStructuredMessageOperationDefinition;
  friend class OperationRegistry;
  enum class InvocationKind { Legacy, Structured, OwnedStructured };
  using Invoke = std::function<boost::asio::awaitable<OperationCompletion>(
      bot::OperationEnvelope)>;
  using StructuredInvoke =
      std::function<boost::asio::awaitable<StructuredOperationCompletion>(
          bot::OperationEnvelope)>;
  using OwnedStructuredInvoke =
      std::function<boost::asio::awaitable<StructuredOperationCompletion>(
          bot::OwnedStructuredOperationEnvelope)>;
  using SupportsSurface = bool (*)(const bot::SurfaceId &);

  OperationBinding(OperationDescription description,
                   SupportsSurface supports_surface,
                   std::type_index result_type, Invoke invoke)
      : description_(std::move(description)),
        supports_surface_(supports_surface), kind_(InvocationKind::Legacy),
        result_type_(result_type), invoke_(std::move(invoke)) {}

  OperationBinding(OperationDescription description,
                   SupportsSurface supports_surface,
                   std::type_index result_type, StructuredInvoke invoke)
      : description_(std::move(description)),
        supports_surface_(supports_surface), kind_(InvocationKind::Structured),
        result_type_(result_type), structured_invoke_(std::move(invoke)) {}

  OperationBinding(OperationDescription description,
                   SupportsSurface supports_surface,
                   std::type_index request_type, std::type_index result_type,
                   OwnedStructuredInvoke invoke)
      : description_(std::move(description)),
        supports_surface_(supports_surface),
        kind_(InvocationKind::OwnedStructured), result_type_(result_type),
        request_type_(request_type),
        owned_structured_invoke_(std::move(invoke)) {}

  OperationDescription description_;
  SupportsSurface supports_surface_;
  InvocationKind kind_;
  std::type_index result_type_;
  std::type_index request_type_{typeid(void)};
  Invoke invoke_;
  StructuredInvoke structured_invoke_;
  OwnedStructuredInvoke owned_structured_invoke_;
};

// The same definition supplies validation-only manifests and executable
// binding. There is intentionally no API to publish an action string without a
// handler.
template <bot::TypedOperation Request> class OperationDefinition {
public:
  using request_type = Request;
  using Traits = bot::OperationTraits<Request>;
  using Result = typename Traits::result_type;
  using Handler =
      std::function<boost::asio::awaitable<bot::BotOperationResult<Result>>(
          const Request &)>;

  using AuthorEvidenceHandler =
      std::function<boost::asio::awaitable<bot::BotOperationResult<Result>>(
          const Request &, ConfirmedAuthorEvidenceCollector &)>;

  explicit OperationDefinition(std::vector<std::string> required_capabilities)
      : OperationDefinition(std::move(required_capabilities),
                            default_message_result_policy<Request>()) {}

  OperationDefinition(std::vector<std::string> required_capabilities,
                      MessageResultPolicy<Request> message_policy)
      : OperationDefinition(std::move(required_capabilities),
                            std::optional{std::move(message_policy)}) {
    static_assert(
        requires { Traits::requires_outcome_client; },
        "message-producing requests require an outcome-aware client marker");
  }

  [[nodiscard]] auto description() const -> const OperationDescription & {
    return description_;
  }

  [[nodiscard]] auto bind(Handler handler) const -> OperationBinding {
    if (!handler)
      throw std::invalid_argument("operation requires an executable handler");
    return bind_impl(
        [handler = std::move(handler)](const Request &request,
                                       ConfirmedAuthorEvidenceCollector &)
            -> boost::asio::awaitable<bot::BotOperationResult<Result>> {
          return handler(request);
        });
  }

  [[nodiscard]] auto bind_with_author_evidence(
      AuthorEvidenceHandler handler) const -> OperationBinding {
    if (!message_policy_)
      throw std::invalid_argument("author evidence requires a message policy");
    if (!handler)
      throw std::invalid_argument("operation requires an executable handler");
    return bind_impl(std::move(handler));
  }

private:
  auto bind_impl(AuthorEvidenceHandler handler) const -> OperationBinding {
    return OperationBinding{
        description_, &Traits::supports_surface, typeid(Result),
        [handler = std::move(handler),
         policy = message_policy_](bot::OperationEnvelope envelope)
            -> boost::asio::awaitable<OperationCompletion> {
          std::optional<Request> request;
          std::optional<MessageOutcomeContext> context;
          try {
            envelope.validate();
            if (envelope.action != Traits::action()) {
              throw std::invalid_argument("operation binding action mismatch");
            }
            request.emplace(bot::GatewayCodec<Request>::decode(
                std::move(envelope.payload)));
            request->validate();
            if (Traits::installation(*request) != envelope.installation) {
              throw std::invalid_argument(
                  "operation payload installation mismatch");
            }
            if (policy) {
              context.emplace(policy->context(*request));
              context->target.validate();
              if (context->target.installation != envelope.installation ||
                  (context->common_request &&
                   context->common_request->target != context->target))
                throw std::invalid_argument(
                    "message outcome context scope mismatch");
            }
          } catch (...) {
            co_return OperationCompletion{
                bot::OperationReply::failure(
                    {.code = bot::BotOperationErrorCode::InvalidRequest,
                     .message =
                         "operation payload failed codec or scope validation",
                     .retryable = false,
                     .submission_safety =
                         bot::SubmissionSafety::DefinitelyNotSubmitted}),
                std::nullopt};
          }
          ConfirmedAuthorEvidenceCollector authors;
          if (policy) {
            bot::BotOperationResult<Result> result;
            try {
              result = co_await handler(*request, authors);
            } catch (...) {
              // No generic transport exception classification for a send: a
              // later unsubmitted step says nothing about earlier effects.
            }
            auto completion = complete_message_operation(
                *request, *context, *policy, std::move(result));
            completion.confirmed_authors =
                authors.take(completion.message_outcome);
            co_return std::move(completion);
          }
          bool returned = false;
          try {
            auto result = co_await handler(*request, authors);
            returned = true;
            result.validate();
            if (!result.ok()) {
              co_return OperationCompletion{
                  bot::OperationReply::failure(std::move(*result.error)),
                  std::nullopt};
            }
            Traits::validate_result(*request, *result.value);
            co_return OperationCompletion{
                bot::OperationReply::success(
                    bot::GatewayCodec<Result>::encode(*result.value)),
                std::nullopt};
          } catch (...) {
            // Owning module handlers return proven pre-submission failures as
            // typed errors. Unclassified exceptions remain conservative here.
            co_return OperationCompletion{
                bot::OperationReply::failure(
                    {.code = Traits::side_effecting
                                 ? bot::BotOperationErrorCode::OutcomeUnknown
                                 : (returned ? bot::BotOperationErrorCode::
                                                   MalformedResponse
                                             : bot::BotOperationErrorCode::
                                                   TransportFailure),
                     .message =
                         "operation handler did not produce a valid SDK result",
                     .retryable = !Traits::side_effecting && !returned,
                     .submission_safety =
                         Traits::side_effecting
                             ? bot::SubmissionSafety::PossiblySubmitted
                             : bot::SubmissionSafety::DefinitelyNotSubmitted}),
                std::nullopt};
          }
        }};
  }

  OperationDefinition(
      std::vector<std::string> required_capabilities,
      std::optional<MessageResultPolicy<Request>> message_policy)
      : description_{Traits::action(), Traits::side_effecting,
                     std::move(required_capabilities),
                     message_policy.has_value()},
        message_policy_(std::move(message_policy)) {
    if (message_policy_) {
      if (!Traits::side_effecting)
        throw std::invalid_argument(
            "message-producing operation must be side effecting");
      message_policy_->validate();
    } else if constexpr (requires { Traits::requires_outcome_client; } ||
                         std::same_as<Result, bot::SendOutcome>) {
      throw std::invalid_argument(
          "outcome operation requires an explicit message policy");
    }
    for (const auto &dependency : description_.required_capabilities)
      (void)bot::ActionId{dependency};
    std::ranges::sort(description_.required_capabilities);
    if (std::ranges::adjacent_find(description_.required_capabilities) !=
        description_.required_capabilities.end())
      throw std::invalid_argument(
          "operation has duplicate capability dependencies");
  }
  OperationDescription description_;
  std::optional<MessageResultPolicy<Request>> message_policy_;
};

class OperationRegistry final : public bot::BotOperationGateway {
public:
  using LiveMessageNormalizer = std::function<bot::MessageReceived(
      const bot::BotInstallationRef &, const common::MessageEvent &,
      bot::TimePoint, std::string_view, const SelfIdentityOrigin &)>;

  OperationRegistry(bot::BotInstallationRef installation,
                    std::shared_ptr<const PlatformAdapter> platform_adapter);
  [[nodiscard]] auto reply_evidence() const
      -> std::shared_ptr<ReplyEvidenceStore>;
  [[nodiscard]] auto admit_message(const bot::MessageReceived &message,
                                   std::uint64_t expected_generation) -> bool;
  ~OperationRegistry() override;

  void install(OperationBinding binding);
  // Only same-installation prepared capabilities can satisfy dependencies.
  void seal(const bot::BotInstallationRef &capability_owner,
            std::span<const std::string> available_capabilities);
  void close() noexcept;
  void set_live_message_normalizer(LiveMessageNormalizer normalizer);
  [[nodiscard]] auto normalize_live_message(
      const common::MessageEvent &event, bot::TimePoint received_at,
      std::string_view observation_id,
      const std::optional<SelfIdentityOrigin> &origin) const
      -> bot::MessageReceived;
  void set_self_identity_cache(std::shared_ptr<SelfIdentityCache>);
  void refresh_self_identity();
  [[nodiscard]] auto capture_origin() const
      -> std::optional<SelfIdentityOrigin>;
  [[nodiscard]] auto observe_self_user(const SelfIdentityOrigin &,
                                       bot::UserRef user)
      -> SelfObservationResult;
  // Origin check, optional self observation and evidence generation capture
  // are atomic in registry -> cache -> evidence lock order.
  [[nodiscard]] auto capture_message_admission_generation(
      const SelfIdentityOrigin &, std::optional<bot::UserRef> observed_self)
      -> MessageAdmissionGeneration;

  [[nodiscard]] auto installation() const -> bot::BotInstallationRef;
  [[nodiscard]] auto declared_actions() const -> std::vector<bot::ActionId>;
  [[nodiscard]] auto descriptions() const -> std::vector<OperationDescription>;
  [[nodiscard]] auto supported_actions(
      const bot::BotInstallationRef &installation) const
      -> bot::BotOperationResult<bot::SupportedActions> override;
  auto invoke(bot::OperationEnvelope envelope)
      -> boost::asio::awaitable<bot::OperationReply> override;
  auto invoke_completed(bot::OperationEnvelope envelope)
      -> boost::asio::awaitable<OperationCompletion>;
  auto invoke_structured_completed(bot::OperationEnvelope envelope,
                                   std::type_index result_type)
      -> boost::asio::awaitable<StructuredOperationCompletion>;

  auto invoke_owned_structured_completed(
      bot::OwnedStructuredOperationEnvelope envelope,
      std::type_index result_type)
      -> boost::asio::awaitable<StructuredOperationCompletion>;

private:
  friend class BotOperationDispatcher;
  struct State;
  struct SendEvidenceAdmission {
    std::shared_ptr<SelfIdentityCache> cache;
    std::shared_ptr<ReplyEvidenceStore> evidence;
    std::uint64_t generation;
    SelfIdentityOrigin origin;
    bot::UserRef initiating_user;
  };
  static auto capture_send_evidence(const std::shared_ptr<State> &state,
                                    const bot::BotInstallationRef &installation,
                                    const bot::ActionId &action) noexcept
      -> std::optional<SendEvidenceAdmission>;
  static void admit_send_evidence(
      const std::shared_ptr<State> &state,
      const std::optional<SendEvidenceAdmission> &admission,
      const std::optional<bot::SendOutcome> &outcome,
      const std::vector<ConfirmedAuthorEvidence> &authors) noexcept;
  static auto invoke_owned_structured_state(
      std::shared_ptr<State> state,
      bot::OwnedStructuredOperationEnvelope envelope,
      std::type_index result_type,
      std::optional<SendEvidenceAdmission> admission)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  static void validate_native_admission(const State &state,
                                        const bot::OperationEnvelope &envelope);
  static auto invoke_structured_owned(
      std::shared_ptr<State> state, bot::OperationEnvelope envelope,
      std::type_index result_type,
      std::optional<SendEvidenceAdmission> admission)
      -> boost::asio::awaitable<StructuredOperationCompletion>;
  static auto invoke_owned(std::shared_ptr<State> state,
                           bot::OperationEnvelope envelope,
                           std::optional<SendEvidenceAdmission> admission)
      -> boost::asio::awaitable<OperationCompletion>;
  static auto reply_only(std::shared_ptr<State> state,
                         bot::OperationEnvelope envelope,
                         std::optional<SendEvidenceAdmission> admission)
      -> boost::asio::awaitable<bot::OperationReply>;
  std::shared_ptr<State> state_;
};

} // namespace obcx::core
