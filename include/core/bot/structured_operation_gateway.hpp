#pragma once

#include "core/bot/typed_operation.hpp"

#include <functional>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <utility>

namespace obcx::bot {

// Process-owned, non-serializable result storage. No Json, formatter,
// reflection factory or secret-ID registry is provided. The matching core/SDK
// must keep the concrete payload's code loaded until its owned value has been
// destroyed.
class StructuredOperationValue {
public:
  StructuredOperationValue(const StructuredOperationValue &) = delete;
  auto operator=(const StructuredOperationValue &)
      -> StructuredOperationValue & = delete;
  StructuredOperationValue(StructuredOperationValue &&) noexcept = default;
  auto operator=(StructuredOperationValue &&) noexcept
      -> StructuredOperationValue & = default;
  ~StructuredOperationValue() = default;

  template <typename T>
  [[nodiscard]] static auto own(BotInstallationRef installation,
                                ActionId action, std::unique_ptr<T> value)
      -> StructuredOperationValue {
    installation.validate();
    action.validate();
    if (!value)
      throw std::invalid_argument("structured result requires owned storage");
    return StructuredOperationValue{
        std::move(installation), std::move(action),
        std::make_unique<Payload<T>>(std::move(value))};
  }

  template <typename T>
  [[nodiscard]] auto contains(const BotInstallationRef &installation,
                              const ActionId &action) const noexcept -> bool {
    return payload_ && installation == installation_ && action == action_ &&
           payload_->type() == std::type_index(typeid(Payload<T>)) &&
           static_cast<const Payload<T> &>(*payload_).value != nullptr;
  }

  // Synchronous borrowed inspection only: no ownership transfer and no
  // reference-returning visitor. The callback must not retain the reference.
  template <typename T, typename Visitor>
  requires std::same_as<std::invoke_result_t<Visitor, const T &>, void>
  void visit(const BotInstallationRef &installation, const ActionId &action,
             Visitor &&visitor) const {
    if (!contains<T>(installation, action))
      throw std::invalid_argument("structured result type or scope mismatch");
    const T &value = *static_cast<const Payload<T> &>(*payload_).value;
    std::invoke(std::forward<Visitor>(visitor), value);
  }

  // A mismatch leaves ownership intact for independent completion recovery.
  // Taking the unique_ptr never copies or moves the concrete sensitive value.
  template <typename T>
  [[nodiscard]] auto take(const BotInstallationRef &installation,
                          const ActionId &action) -> std::unique_ptr<T> {
    if (!payload_ || installation != installation_ || action != action_ ||
        payload_->type() != std::type_index(typeid(Payload<T>)))
      throw std::invalid_argument("structured result type or scope mismatch");
    auto &value = static_cast<Payload<T> &>(*payload_).value;
    if (!value)
      throw std::invalid_argument("structured result was already consumed");
    return std::move(value);
  }

private:
  struct ErasedPayload {
    virtual ~ErasedPayload() = default;
    [[nodiscard]] virtual auto type() const noexcept -> std::type_index = 0;
  };
  template <typename T> struct Payload final : ErasedPayload {
    explicit Payload(std::unique_ptr<T> owned) : value(std::move(owned)) {}
    [[nodiscard]] auto type() const noexcept -> std::type_index override {
      return typeid(Payload<T>);
    }
    std::unique_ptr<T> value;
  };
  StructuredOperationValue(BotInstallationRef installation, ActionId action,
                           std::unique_ptr<ErasedPayload> payload)
      : installation_(std::move(installation)), action_(std::move(action)),
        payload_(std::move(payload)) {}

  BotInstallationRef installation_;
  ActionId action_;
  std::unique_ptr<ErasedPayload> payload_;
};

using StructuredOperationReply = BotOperationResult<StructuredOperationValue>;

// Additive interface, not another route to native transport. Implementations
// must share installed registry, routing, cancellation and completion handling
// with BotOperationGateway. Generic invocation MUST reject structured-only
// bindings before provider I/O. This interface alone cannot enforce that gate.
class StructuredOperationGateway {
public:
  StructuredOperationGateway() = default;
  StructuredOperationGateway(const StructuredOperationGateway &) = delete;
  auto operator=(const StructuredOperationGateway &)
      -> StructuredOperationGateway & = delete;
  StructuredOperationGateway(StructuredOperationGateway &&) = delete;
  auto operator=(StructuredOperationGateway &&)
      -> StructuredOperationGateway & = delete;
  virtual ~StructuredOperationGateway() = default;

  // Explicit markers also exclude these requests from existing generic invoke.
  // The opaque reply retains the whole outcome, not just its completed T.
  template <TypedOperation Request>
  requires requires {
    OperationTraits<Request>::requires_structured_result;
    OperationTraits<Request>::requires_outcome_client;
  } && (!requires { OperationTraits<Request>::requires_owned_request; } ||
        !OperationTraits<Request>::requires_owned_request)
  auto invoke(Request request, std::optional<OperationCorrelation> correlation)
      -> boost::asio::awaitable<StructuredOperationReply> {
    using Traits = OperationTraits<Request>;
    std::optional<OperationEnvelope> envelope;
    try {
      request.validate();
      envelope.emplace(
          OperationEnvelope{.installation = Traits::installation(request),
                            .action = Traits::action(),
                            .payload = GatewayCodec<Request>::encode(request),
                            .correlation = std::move(correlation)});
      envelope->validate();
    } catch (...) {
      co_return StructuredOperationReply::failure(
          {.code = BotOperationErrorCode::InvalidRequest,
           .message = "structured request failed validation or encoding",
           .retryable = false,
           .submission_safety = SubmissionSafety::DefinitelyNotSubmitted});
    }
    // The backend must retain its independent checkpoint before any fallible
    // result construction. Do not catch here and erase a completed/pending
    // operation into a fabricated pre-submission error.
    co_return co_await invoke_structured(std::move(*envelope),
                                         typeid(typename Traits::result_type));
  }

protected:
  // Registry checks this type against the installed typed binding; a caller's
  // type assertion is not authority to install a codec or select native action.
  virtual auto invoke_structured(OperationEnvelope envelope,
                                 std::type_index result_type)
      -> boost::asio::awaitable<StructuredOperationReply> = 0;
};

} // namespace obcx::bot
