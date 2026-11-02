#include "core/bot/operation_registry.hpp"
#include "core/bot/self_identity_cache.hpp"
#include "core/bot/structured_operation_binding.hpp"

#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>

namespace obcx::core {
namespace {

template <typename T>
auto reject(const bot::BotOperationErrorCode code, const std::string &message)
    -> bot::BotOperationResult<T> {
  return bot::BotOperationResult<T>::failure(
      {.code = code,
       .message = message,
       .retryable = false,
       .submission_safety = bot::SubmissionSafety::DefinitelyNotSubmitted});
}

} // namespace

struct OperationRegistry::State {
  explicit State(bot::BotInstallationRef identity)
      : installation(std::move(identity)) {}

  bot::BotInstallationRef installation;
  mutable std::shared_mutex mutex;
  std::map<bot::ActionId, std::shared_ptr<const OperationBinding>> bindings;
  std::shared_ptr<SelfIdentityCache> self_identity;
  std::shared_ptr<const PlatformDomainBinding> domain_binding;
  std::shared_ptr<const LiveMessageNormalizer> live_message_normalizer;
  std::shared_ptr<ReplyEvidenceStore> reply_evidence;
  bool sealed = false;
  bool closed = false;
};

OperationRegistry::OperationRegistry(
    bot::BotInstallationRef installation,
    std::shared_ptr<const PlatformDomainBinding> domain_binding) {
  installation.validate();
  if (!domain_binding ||
      domain_binding->codec().surface() != installation.surface)
    throw std::invalid_argument(
        "endpoint requires its registered domain binding");
  auto evidence = std::make_shared<ReplyEvidenceStore>(installation);
  state_ = std::make_shared<State>(std::move(installation));
  state_->domain_binding = std::move(domain_binding);
  state_->reply_evidence = std::move(evidence);
}

OperationRegistry::~OperationRegistry() { close(); }

void OperationRegistry::install(OperationBinding binding) {
  std::unique_lock lock(state_->mutex);
  if (state_->sealed || state_->closed) {
    throw std::logic_error("operation registry no longer accepts registration");
  }
  binding.description_.action.validate();
  const bool valid_callable =
      (binding.kind_ == OperationBinding::InvocationKind::Legacy &&
       binding.invoke_ && !binding.structured_invoke_ &&
       !binding.owned_structured_invoke_) ||
      (binding.kind_ == OperationBinding::InvocationKind::Structured &&
       binding.structured_invoke_ && !binding.invoke_ &&
       !binding.owned_structured_invoke_) ||
      (binding.kind_ == OperationBinding::InvocationKind::OwnedStructured &&
       binding.owned_structured_invoke_ && !binding.invoke_ &&
       !binding.structured_invoke_ && binding.request_type_ != typeid(void));
  if (!valid_callable || !binding.supports_surface_ ||
      !binding.supports_surface_(state_->installation.surface)) {
    throw std::invalid_argument(
        "operation binding is incomplete or belongs to another surface");
  }
  auto entry = std::make_shared<const OperationBinding>(std::move(binding));
  const auto action = entry->description_.action;
  if (!state_->bindings.emplace(action, std::move(entry)).second) {
    throw std::invalid_argument("duplicate operation action: " +
                                action.value());
  }
}

void OperationRegistry::seal(
    const bot::BotInstallationRef &capability_owner,
    const std::span<const std::string> available_capabilities) {
  capability_owner.validate();
  std::unique_lock lock(state_->mutex);
  if (state_->sealed || state_->closed) {
    throw std::logic_error("operation registry is already sealed or closed");
  }
  if (capability_owner != state_->installation) {
    throw std::invalid_argument(
        "operation capabilities belong to another installation");
  }
  std::set<std::string> available;
  for (const auto &capability : available_capabilities) {
    (void)bot::ActionId{capability};
    available.insert(capability);
  }
  for (const auto &[action, binding] : state_->bindings) {
    (void)action;
    for (const auto &dependency : binding->description_.required_capabilities) {
      if (!available.contains(dependency)) {
        throw std::invalid_argument("missing operation capability: " +
                                    dependency);
      }
    }
  }
  state_->sealed = true;
}

void OperationRegistry::close() noexcept {
  // Release callback captures outside the lock; admitted calls retain an owner.
  std::shared_ptr<const LiveMessageNormalizer> retired_normalizer;
  std::unique_lock lock(state_->mutex);
  state_->closed = true;
  retired_normalizer.swap(state_->live_message_normalizer);
  // Invocations already admitted retain their own binding/handler lease.
  state_->bindings.clear();
  state_->reply_evidence->close();
  if (state_->self_identity)
    state_->self_identity->close();
}

void OperationRegistry::set_live_message_normalizer(
    LiveMessageNormalizer normalizer) {
  if (!normalizer)
    throw std::invalid_argument("live message normalizer requires a callable");
  auto owner =
      std::make_shared<const LiveMessageNormalizer>(std::move(normalizer));
  std::unique_lock lock(state_->mutex);
  if (state_->sealed || state_->closed || state_->live_message_normalizer)
    throw std::logic_error(
        "live message normalizer must be installed once before sealing");
  state_->live_message_normalizer = std::move(owner);
}

auto OperationRegistry::normalize_live_message(
    const common::MessageEvent &event, bot::TimePoint received_at,
    std::string_view observation_id,
    const std::optional<SelfIdentityOrigin> &origin) const
    -> bot::MessageReceived {
  const auto state = state_;
  std::shared_ptr<const LiveMessageNormalizer> normalizer;
  std::shared_ptr<const PlatformDomainBinding> binding;
  {
    std::shared_lock lock(state->mutex);
    if (state->closed)
      throw std::logic_error("message normalization is closed");
    if (!state->sealed)
      throw std::logic_error("message normalization endpoint is not prepared");
    normalizer = state->live_message_normalizer;
    binding = state->domain_binding;
  }
  if (normalizer && !origin)
    throw std::invalid_argument(
        "live message normalization requires an origin");
  // Use only the supplied origin; failures must not retry pure normalization.
  auto message = normalizer
                     ? (*normalizer)(state->installation, event, received_at,
                                     observation_id, *origin)
                     : binding->normalize_message(state->installation, event,
                                                  received_at, observation_id);
  bot::binding::validate_native_event(binding->codec(), message);
  if (message.message.conversation.installation != state->installation ||
      message.received_at.unix_milliseconds != received_at.unix_milliseconds ||
      (message.reply && message.reply->reference() &&
       message.reply->reference()->observation_id() != observation_id))
    throw std::invalid_argument(
        "normalized event scope or observation mismatch");
  return message;
}

void OperationRegistry::set_self_identity_cache(
    std::shared_ptr<SelfIdentityCache> cache) {
  std::unique_lock lock(state_->mutex);
  if (!cache || state_->sealed || state_->closed || state_->self_identity)
    throw std::logic_error(
        "self identity cache must be installed once before sealing");
  cache->set_generation_invalidation_handler(
      [evidence = state_->reply_evidence]() noexcept {
        evidence->invalidate();
      });
  state_->self_identity = std::move(cache);
}
void OperationRegistry::refresh_self_identity() {
  std::shared_lock lock(state_->mutex);
  if (!state_->closed && state_->self_identity)
    state_->self_identity->refresh();
}
auto OperationRegistry::reply_evidence() const
    -> std::shared_ptr<ReplyEvidenceStore> {
  return state_->reply_evidence;
}
auto OperationRegistry::admit_message(const bot::MessageReceived &message,
                                      std::uint64_t expected_generation)
    -> bool {
  bot::binding::validate_native_event(state_->domain_binding->codec(), message);
  std::shared_lock lock(state_->mutex);
  return !state_->closed &&
         state_->reply_evidence->admit(message, expected_generation);
}
auto OperationRegistry::capture_origin() const
    -> std::optional<SelfIdentityOrigin> {
  std::shared_lock lock(state_->mutex);
  if (state_->closed || !state_->self_identity)
    return std::nullopt;
  return state_->self_identity->capture_origin();
}
auto OperationRegistry::observe_self_user(const SelfIdentityOrigin &origin,
                                          bot::UserRef user)
    -> SelfObservationResult {
  return capture_message_admission_generation(origin, std::move(user))
      .observation;
}
auto OperationRegistry::capture_message_admission_generation(
    const SelfIdentityOrigin &origin, std::optional<bot::UserRef> observed_self)
    -> MessageAdmissionGeneration {
  if (observed_self) {
    try {
      state_->domain_binding->codec().validate(*observed_self);
    } catch (...) {
      return {{SelfObservationStatus::InvalidIdentity, std::nullopt},
              std::nullopt};
    }
    if (observed_self->installation != state_->installation)
      return {{SelfObservationStatus::InvalidIdentity, std::nullopt},
              std::nullopt};
  }
  std::shared_lock lock(state_->mutex);
  if (state_->closed)
    return {{SelfObservationStatus::Closed, std::nullopt}, std::nullopt};
  if (!state_->self_identity)
    return {{SelfObservationStatus::RejectedOrigin, std::nullopt},
            std::nullopt};
  std::optional<std::uint64_t> generation;
  auto observation = state_->self_identity->observe_user_and_capture(
      origin, std::move(observed_self),
      [&] { generation = state_->reply_evidence->generation(); });
  return {std::move(observation), generation};
}

auto OperationRegistry::installation() const -> bot::BotInstallationRef {
  return state_->installation;
}

auto OperationRegistry::declared_actions() const -> std::vector<bot::ActionId> {
  std::shared_lock lock(state_->mutex);
  std::vector<bot::ActionId> actions;
  for (const auto &[action, binding] : state_->bindings) {
    (void)binding;
    actions.push_back(action);
  }
  return actions;
}

auto OperationRegistry::descriptions() const
    -> std::vector<OperationDescription> {
  std::shared_lock lock(state_->mutex);
  std::vector<OperationDescription> descriptions;
  descriptions.reserve(state_->bindings.size());
  for (const auto &[action, binding] : state_->bindings) {
    (void)action;
    descriptions.push_back(binding->description_);
  }
  return descriptions;
}

auto OperationRegistry::supported_actions(
    const bot::BotInstallationRef &installation) const
    -> bot::BotOperationResult<bot::SupportedActions> {
  try {
    installation.validate();
  } catch (...) {
    return reject<bot::SupportedActions>(
        bot::BotOperationErrorCode::InvalidRequest,
        "operation installation is invalid");
  }
  std::shared_lock lock(state_->mutex);
  if (installation.installation_id != state_->installation.installation_id) {
    return reject<bot::SupportedActions>(
        bot::BotOperationErrorCode::RouteNotFound,
        "operation installation was not found");
  }
  if (installation.surface != state_->installation.surface) {
    return reject<bot::SupportedActions>(
        bot::BotOperationErrorCode::SurfaceMismatch,
        "operation installation surface does not match");
  }
  if (state_->closed) {
    return reject<bot::SupportedActions>(bot::BotOperationErrorCode::Cancelled,
                                         "operation endpoint is closed");
  }
  if (!state_->sealed) {
    return reject<bot::SupportedActions>(
        bot::BotOperationErrorCode::RouteNotFound,
        "operation endpoint is not prepared");
  }
  bot::SupportedActions result{.installation = state_->installation};
  for (const auto &[action, binding] : state_->bindings) {
    (void)binding;
    result.actions.push_back(action);
  }
  return bot::BotOperationResult<bot::SupportedActions>::success(
      std::move(result));
}

auto OperationRegistry::capture_send_evidence(
    const std::shared_ptr<State> &state,
    const bot::BotInstallationRef &installation,
    const bot::ActionId &action) noexcept
    -> std::optional<SendEvidenceAdmission> {
  try {
    std::shared_lock lock(state->mutex);
    if (state->closed || !state->sealed ||
        installation != state->installation || !state->self_identity)
      return std::nullopt;
    const auto binding = state->bindings.find(action);
    if (binding == state->bindings.end() ||
        !binding->second->description_.message_producing)
      return std::nullopt;
    const auto &cache = state->self_identity;
    const auto &evidence = state->reply_evidence;
    // Generation MUST precede origin/read. A transition anywhere in this
    // non-atomic sequence either fails the atomic identity check or leaves an
    // older evidence generation which publication cannot admit. Never observe
    // the cached user back into the cache: that could rebase an old account.
    const auto generation = evidence->generation();
    const auto origin = cache->capture_origin();
    const auto self = cache->read();
    if (!generation || !origin || !self.ok() ||
        !cache->matches_observed_user(*origin, self.value->user))
      return std::nullopt;
    return SendEvidenceAdmission{cache, evidence, *generation, *origin,
                                 self.value->user};
  } catch (...) {
    // Evidence is best effort, including allocation during cache reads.
    return std::nullopt;
  }
}

void OperationRegistry::admit_send_evidence(
    const std::shared_ptr<State> &state,
    const std::optional<SendEvidenceAdmission> &admission,
    const std::optional<bot::SendOutcome> &outcome,
    const std::vector<ConfirmedAuthorEvidence> &authors) noexcept {
  if (!admission || !outcome)
    return;
  try {
    std::shared_lock lock(state->mutex);
    if (state->closed || state->self_identity != admission->cache ||
        state->reply_evidence != admission->evidence ||
        !admission->cache->matches_observed_user(admission->origin,
                                                 admission->initiating_user))
      return;
    // Only binding-normalized, scope-validated physical outputs reach here.
    // A failure's confirmed IDs count even when content associations do not.
    for (const auto &message : confirmed_author_detail::messages(*outcome)) {
      try {
        const auto found = std::ranges::find(authors, message,
                                             &ConfirmedAuthorEvidence::message);
        if (found == authors.end())
          continue;
        const auto &codec = state->domain_binding->codec();
        codec.validate(message);
        const bot::Sender *native = nullptr;
        if (found->native_author &&
            confirmed_author_detail::known_in_installation(
                *found->native_author, state->installation)) {
          bot::binding::validate_native_sender(codec, *found->native_author);
          native = &*found->native_author;
        }
        // The self tag resolves ONLY against the original admission identity.
        // Native sender-chat/user evidence neither reads nor updates self.
        bool conflict = found->author_conflicted;
        if (found->initiating_self_proven && native) {
          const auto *user = std::get_if<bot::KnownUser>(native);
          conflict |= !user || user->user != admission->initiating_user;
        }
        // The synchronous cache invalidation hook advances this same store.
        // A transition after the match rejects insertion or clears it before
        // the transition completes. Never substitute a fresh identity here.
        if (conflict)
          (void)admission->evidence->admit_confirmation_conflict(
              message, admission->generation);
        else if (native)
          (void)admission->evidence->admit_confirmation(message, *native,
                                                        admission->generation);
        else if (found->initiating_self_proven) {
          codec.validate(admission->initiating_user);
          (void)admission->evidence->admit_confirmation(
              message, bot::KnownUser{admission->initiating_user},
              admission->generation);
        }
      } catch (...) {
        // One failed optional proof cannot affect this outcome or other IDs.
      }
    }
  } catch (...) {
    // Publication cannot replace outcomes or bypass notification admission.
  }
}

auto OperationRegistry::invoke(bot::OperationEnvelope envelope)
    -> boost::asio::awaitable<bot::OperationReply> {
  // Capture ownership synchronously, before a lazily-started coroutine can
  // outlive this registry object. No raw `this` survives in the coroutine.
  auto admission =
      capture_send_evidence(state_, envelope.installation, envelope.action);
  return reply_only(state_, std::move(envelope), std::move(admission));
}

auto OperationRegistry::invoke_completed(bot::OperationEnvelope envelope)
    -> boost::asio::awaitable<OperationCompletion> {
  auto admission =
      capture_send_evidence(state_, envelope.installation, envelope.action);
  return invoke_owned(state_, std::move(envelope), std::move(admission));
}

auto OperationRegistry::reply_only(
    std::shared_ptr<State> state, bot::OperationEnvelope envelope,
    std::optional<SendEvidenceAdmission> admission)
    -> boost::asio::awaitable<bot::OperationReply> {
  auto completion = co_await invoke_owned(std::move(state), std::move(envelope),
                                          std::move(admission));
  co_return std::move(completion.reply);
}

auto OperationRegistry::invoke_owned(
    std::shared_ptr<State> state, bot::OperationEnvelope envelope,
    std::optional<SendEvidenceAdmission> admission)
    -> boost::asio::awaitable<OperationCompletion> {
  try {
    envelope.validate();
  } catch (...) {
    co_return OperationCompletion{
        reject<bot::Json>(bot::BotOperationErrorCode::InvalidRequest,
                          "operation envelope is invalid"),
        std::nullopt};
  }
  std::shared_ptr<const OperationBinding> binding;
  {
    std::shared_lock lock(state->mutex);
    if (envelope.installation.installation_id !=
        state->installation.installation_id) {
      co_return OperationCompletion{
          reject<bot::Json>(bot::BotOperationErrorCode::RouteNotFound,
                            "operation installation was not found"),
          std::nullopt};
    }
    if (envelope.installation.surface != state->installation.surface) {
      co_return OperationCompletion{
          reject<bot::Json>(bot::BotOperationErrorCode::SurfaceMismatch,
                            "operation installation surface does not match"),
          std::nullopt};
    }
    if (state->closed) {
      co_return OperationCompletion{
          reject<bot::Json>(bot::BotOperationErrorCode::Cancelled,
                            "operation endpoint is closed"),
          std::nullopt};
    }
    if (!state->sealed) {
      co_return OperationCompletion{
          reject<bot::Json>(bot::BotOperationErrorCode::RouteNotFound,
                            "operation endpoint is not prepared"),
          std::nullopt};
    }
    const auto found = state->bindings.find(envelope.action);
    if (found == state->bindings.end()) {
      co_return OperationCompletion{
          reject<bot::Json>(bot::BotOperationErrorCode::UnsupportedAction,
                            "operation action is not installed"),
          std::nullopt};
    }
    binding = found->second;
    if (binding->kind_ != OperationBinding::InvocationKind::Legacy) {
      co_return OperationCompletion{
          reject<bot::Json>(bot::BotOperationErrorCode::InvalidRequest,
                            "operation requires structured invocation"),
          std::nullopt};
    }
  }
  try {
    validate_native_admission(*state, envelope);
  } catch (...) {
    co_return OperationCompletion{
        reject<bot::Json>(bot::BotOperationErrorCode::InvalidRequest,
                          "native scope or reply evidence is invalid"),
        std::nullopt};
  }
  auto completion = co_await binding->invoke_(std::move(envelope));
  if (binding->description_.message_producing)
    admit_send_evidence(state, admission, completion.message_outcome,
                        completion.confirmed_authors);
  std::vector<ConfirmedAuthorEvidence>{}.swap(completion.confirmed_authors);
  co_return std::move(completion);
}

auto OperationRegistry::invoke_structured_completed(
    bot::OperationEnvelope envelope, std::type_index result_type)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  auto admission =
      capture_send_evidence(state_, envelope.installation, envelope.action);
  return invoke_structured_owned(state_, std::move(envelope), result_type,
                                 std::move(admission));
}

void OperationRegistry::validate_native_admission(
    const State &state, const bot::OperationEnvelope &envelope) {
  const auto &codec = state.domain_binding->codec();
  const auto validate = [&]<typename Request>() {
    if (envelope.action != bot::OperationTraits<Request>::action())
      return false;
    const auto request = bot::GatewayCodec<Request>::decode(envelope.payload);
    if (bot::OperationTraits<Request>::installation(request) !=
        state.installation)
      throw std::invalid_argument("request belongs to another installation");
    bot::binding::validate_native_request(codec, request);
    if constexpr (std::same_as<Request, bot::ResolveReplyRequest>)
      bot::binding::validate_reply_admission(codec, *state.reply_evidence,
                                             request);
    return true;
  };
  (void)(validate.template operator()<bot::SendMessageRequest>() ||
         validate.template operator()<bot::DeleteMessageRequest>() ||
         validate.template operator()<bot::GetSelfRequest>() ||
         validate.template operator()<bot::GetMemberRequest>() ||
         validate.template operator()<bot::ResolveReplyRequest>() ||
         validate.template operator()<bot::ReadMediaRequest>() ||
         validate.template operator()<bot::GetCapabilitiesRequest>());
}

auto OperationRegistry::invoke_structured_owned(
    std::shared_ptr<State> state, bot::OperationEnvelope envelope,
    std::type_index result_type, std::optional<SendEvidenceAdmission> admission)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  try {
    envelope.validate();
  } catch (...) {
    co_return StructuredOperationCompletion{
        reject<bot::StructuredOperationValue>(
            bot::BotOperationErrorCode::InvalidRequest,
            "operation envelope is invalid"),
        StructuredCompletionDisposition::Failed};
  }
  std::shared_ptr<const OperationBinding> binding;
  {
    std::shared_lock lock(state->mutex);
    if (envelope.installation.installation_id !=
        state->installation.installation_id) {
      co_return StructuredOperationCompletion{
          reject<bot::StructuredOperationValue>(
              bot::BotOperationErrorCode::RouteNotFound,
              "operation installation was not found"),
          StructuredCompletionDisposition::Failed};
    }
    if (envelope.installation.surface != state->installation.surface) {
      co_return StructuredOperationCompletion{
          reject<bot::StructuredOperationValue>(
              bot::BotOperationErrorCode::SurfaceMismatch,
              "operation installation surface does not match"),
          StructuredCompletionDisposition::Failed};
    }
    if (state->closed) {
      co_return StructuredOperationCompletion{
          reject<bot::StructuredOperationValue>(
              bot::BotOperationErrorCode::Cancelled,
              "operation endpoint is closed"),
          StructuredCompletionDisposition::Failed};
    }
    if (!state->sealed) {
      co_return StructuredOperationCompletion{
          reject<bot::StructuredOperationValue>(
              bot::BotOperationErrorCode::RouteNotFound,
              "operation endpoint is not prepared"),
          StructuredCompletionDisposition::Failed};
    }
    const auto found = state->bindings.find(envelope.action);
    if (found == state->bindings.end()) {
      co_return StructuredOperationCompletion{
          reject<bot::StructuredOperationValue>(
              bot::BotOperationErrorCode::UnsupportedAction,
              "operation action is not installed"),
          StructuredCompletionDisposition::Failed};
    }
    binding = found->second;
    if (binding->kind_ != OperationBinding::InvocationKind::Structured ||
        binding->result_type_ != result_type) {
      co_return StructuredOperationCompletion{
          reject<bot::StructuredOperationValue>(
              bot::BotOperationErrorCode::InvalidRequest,
              "operation invocation kind or result type does not match"),
          StructuredCompletionDisposition::Failed};
    }
  }
  try {
    validate_native_admission(*state, envelope);
  } catch (...) {
    co_return StructuredOperationCompletion{
        reject<bot::StructuredOperationValue>(
            bot::BotOperationErrorCode::InvalidRequest,
            "native scope or reply evidence is invalid"),
        StructuredCompletionDisposition::Failed};
  }
  auto completion = co_await binding->structured_invoke_(std::move(envelope));
  if (!binding->description_.message_producing && completion.message_outcome) {
    completion.message_outcome.reset();
    completion.disposition = StructuredCompletionDisposition::Failed;
  }
  if (binding->description_.message_producing &&
      completion.disposition !=
          StructuredCompletionDisposition::AcceptedPending)
    admit_send_evidence(state, admission, completion.message_outcome,
                        completion.confirmed_authors);
  std::vector<ConfirmedAuthorEvidence>{}.swap(completion.confirmed_authors);
  co_return std::move(completion);
}

auto OperationRegistry::invoke_owned_structured_completed(
    bot::OwnedStructuredOperationEnvelope envelope, std::type_index result_type)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  auto admission =
      capture_send_evidence(state_, envelope.installation, envelope.action);
  return invoke_owned_structured_state(state_, std::move(envelope), result_type,
                                       std::move(admission));
}

auto OperationRegistry::invoke_owned_structured_state(
    std::shared_ptr<State> state,
    bot::OwnedStructuredOperationEnvelope envelope, std::type_index result_type,
    std::optional<SendEvidenceAdmission> admission)
    -> boost::asio::awaitable<StructuredOperationCompletion> {
  try {
    envelope.validate();
  } catch (...) {
    co_return StructuredOperationCompletion{
        reject<bot::StructuredOperationValue>(
            bot::BotOperationErrorCode::InvalidRequest,
            "operation envelope is invalid"),
        StructuredCompletionDisposition::Failed};
  }
  std::shared_ptr<const OperationBinding> binding;
  {
    std::shared_lock lock(state->mutex);
    if (envelope.installation.installation_id !=
        state->installation.installation_id) {
      co_return StructuredOperationCompletion{
          reject<bot::StructuredOperationValue>(
              bot::BotOperationErrorCode::RouteNotFound,
              "operation installation was not found"),
          StructuredCompletionDisposition::Failed};
    }
    if (envelope.installation.surface != state->installation.surface) {
      co_return StructuredOperationCompletion{
          reject<bot::StructuredOperationValue>(
              bot::BotOperationErrorCode::SurfaceMismatch,
              "operation installation surface does not match"),
          StructuredCompletionDisposition::Failed};
    }
    if (state->closed) {
      co_return StructuredOperationCompletion{
          reject<bot::StructuredOperationValue>(
              bot::BotOperationErrorCode::Cancelled,
              "operation endpoint is closed"),
          StructuredCompletionDisposition::Failed};
    }
    if (!state->sealed) {
      co_return StructuredOperationCompletion{
          reject<bot::StructuredOperationValue>(
              bot::BotOperationErrorCode::RouteNotFound,
              "operation endpoint is not prepared"),
          StructuredCompletionDisposition::Failed};
    }
    const auto found = state->bindings.find(envelope.action);
    if (found == state->bindings.end()) {
      co_return StructuredOperationCompletion{
          reject<bot::StructuredOperationValue>(
              bot::BotOperationErrorCode::UnsupportedAction,
              "operation action is not installed"),
          StructuredCompletionDisposition::Failed};
    }
    binding = found->second;
    if (binding->kind_ != OperationBinding::InvocationKind::OwnedStructured ||
        binding->result_type_ != result_type ||
        !envelope.matches(binding->request_type_)) {
      co_return StructuredOperationCompletion{
          reject<bot::StructuredOperationValue>(
              bot::BotOperationErrorCode::InvalidRequest,
              "operation invocation kind or request/result type does not "
              "match"),
          StructuredCompletionDisposition::Failed};
    }
  }
  auto completion =
      co_await binding->owned_structured_invoke_(std::move(envelope));
  if (binding->description_.message_producing &&
      completion.disposition !=
          StructuredCompletionDisposition::AcceptedPending)
    admit_send_evidence(state, admission, completion.message_outcome,
                        completion.confirmed_authors);
  std::vector<ConfirmedAuthorEvidence>{}.swap(completion.confirmed_authors);
  co_return std::move(completion);
}

} // namespace obcx::core
