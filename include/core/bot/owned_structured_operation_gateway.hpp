#pragma once

#include "core/bot/structured_operation_gateway.hpp"

namespace obcx::bot {

// Request-role storage is deliberately distinct from the public result carrier.
// Only a validated concrete request can create it; neither JSON nor a claimed
// type_index can construct or replace its payload.
class OwnedStructuredOperationEnvelope {
public:
  BotInstallationRef installation;
  ActionId action;
  std::optional<OperationCorrelation> correlation;

  OwnedStructuredOperationEnvelope(const OwnedStructuredOperationEnvelope &) =
      delete;
  auto operator=(const OwnedStructuredOperationEnvelope &)
      -> OwnedStructuredOperationEnvelope & = delete;
  OwnedStructuredOperationEnvelope(
      OwnedStructuredOperationEnvelope &&) noexcept = default;
  auto operator=(OwnedStructuredOperationEnvelope &&) noexcept
      -> OwnedStructuredOperationEnvelope & = default;

  template <TypedOperation Request>
  static auto own(Request request,
                  std::optional<OperationCorrelation> correlation)
      -> OwnedStructuredOperationEnvelope {
    using Traits = OperationTraits<Request>;
    static_assert(Traits::requires_owned_request == true);
    static_assert(Traits::requires_structured_result == true);
    static_assert(Traits::requires_outcome_client == true);
    if (correlation)
      correlation->validate();
    request.validate();
    auto installation = Traits::installation(request);
    auto action = Traits::action();
    installation.validate();
    action.validate();
    auto value = StructuredOperationValue::own(
        installation, action, std::make_unique<Request>(std::move(request)));
    return OwnedStructuredOperationEnvelope{
        std::move(installation),
        std::move(action),
        std::move(correlation),
        std::move(value),
        typeid(Request),
        [](const StructuredOperationValue &value,
           const BotInstallationRef &installation, const ActionId &action) {
          return value.contains<Request>(installation, action);
        }};
  }

  void validate() const {
    installation.validate();
    action.validate();
    if (correlation)
      correlation->validate();
    if (!matches(request_type_))
      throw std::invalid_argument("owned request type or scope mismatch");
  }

  [[nodiscard]] auto matches(std::type_index request_type) const noexcept
      -> bool {
    return request_type == request_type_ &&
           contains_(request_, installation, action);
  }

  template <TypedOperation Request> auto take() -> std::unique_ptr<Request> {
    validate();
    return request_.take<Request>(installation, action);
  }

private:
  using Contains = bool (*)(const StructuredOperationValue &,
                            const BotInstallationRef &, const ActionId &);
  OwnedStructuredOperationEnvelope(
      BotInstallationRef installation_value, ActionId action_value,
      std::optional<OperationCorrelation> correlation_value,
      StructuredOperationValue request, std::type_index request_type,
      Contains contains)
      : installation(std::move(installation_value)),
        action(std::move(action_value)),
        correlation(std::move(correlation_value)), request_(std::move(request)),
        request_type_(request_type), contains_(contains) {}

  StructuredOperationValue request_;
  std::type_index request_type_;
  Contains contains_;
};

// Additive interface; callers retain the existing gateway lifetime obligation.
// Implementations use the same registry and dispatcher as the JSON routes.
class OwnedStructuredOperationGateway {
public:
  OwnedStructuredOperationGateway() = default;
  OwnedStructuredOperationGateway(const OwnedStructuredOperationGateway &) =
      delete;
  auto operator=(const OwnedStructuredOperationGateway &)
      -> OwnedStructuredOperationGateway & = delete;
  OwnedStructuredOperationGateway(OwnedStructuredOperationGateway &&) = delete;
  auto operator=(OwnedStructuredOperationGateway &&)
      -> OwnedStructuredOperationGateway & = delete;
  virtual ~OwnedStructuredOperationGateway() = default;

  template <TypedOperation Request>
  requires(OperationTraits<Request>::requires_owned_request &&
           OperationTraits<Request>::requires_structured_result &&
           OperationTraits<Request>::requires_outcome_client)
  auto invoke_owned(Request request,
                    std::optional<OperationCorrelation> correlation)
      -> boost::asio::awaitable<StructuredOperationReply> {
    std::optional<OwnedStructuredOperationEnvelope> envelope;
    try {
      // Check explicit metadata before transferring the request into storage.
      if (correlation)
        correlation->validate();
      request.validate();
      OperationTraits<Request>::installation(request).validate();
      OperationTraits<Request>::action().validate();
      envelope.emplace(OwnedStructuredOperationEnvelope::own(
          std::move(request), std::move(correlation)));
    } catch (...) {
      co_return StructuredOperationReply::failure(
          {.code = BotOperationErrorCode::InvalidRequest,
           .message = "owned request failed validation or allocation",
           .retryable = false,
           .submission_safety = SubmissionSafety::DefinitelyNotSubmitted});
    }
    // No post-invocation catch may erase native outcome evidence.
    co_return co_await invoke_owned_structured(
        std::move(*envelope),
        typeid(typename OperationTraits<Request>::result_type));
  }

protected:
  virtual auto invoke_owned_structured(
      OwnedStructuredOperationEnvelope envelope, std::type_index result_type)
      -> boost::asio::awaitable<StructuredOperationReply> = 0;
};

} // namespace obcx::bot
