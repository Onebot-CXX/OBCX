#ifndef OBCX_INCLUDE_CORE_BOT_OPERATION_REGISTRY_HPP_
#define OBCX_INCLUDE_CORE_BOT_OPERATION_REGISTRY_HPP_

// Process-only infrastructure. Never installed as an Actor SDK header.
#include "core/bot/typed_operation.hpp"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace obcx::core {

struct OperationDescription {
  bot::ActionId action;
  bool side_effecting;
  std::vector<std::string> required_capabilities;

  auto operator==(const OperationDescription &) const -> bool = default;
};

template <bot::TypedOperation Request> class OperationDefinition;
class OperationRegistry;

class OperationBinding {
public:
  [[nodiscard]] auto description() const -> const OperationDescription & {
    return description_;
  }

private:
  template <bot::TypedOperation Request> friend class OperationDefinition;
  friend class OperationRegistry;
  using Invoke = std::function<boost::asio::awaitable<bot::OperationReply>(
      bot::OperationEnvelope)>;
  using SupportsSurface = bool (*)(const bot::SurfaceId &);

  OperationBinding(OperationDescription description,
                   SupportsSurface supports_surface, Invoke invoke)
      : description_(std::move(description)),
        supports_surface_(supports_surface), invoke_(std::move(invoke)) {}

  OperationDescription description_;
  SupportsSurface supports_surface_;
  Invoke invoke_;
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

  explicit OperationDefinition(std::vector<std::string> required_capabilities)
      : description_{Traits::action(), Traits::side_effecting,
                     std::move(required_capabilities)} {
    for (const auto &dependency : description_.required_capabilities) {
      (void)bot::ActionId{
          dependency}; // Stable capability syntax, not action lookup.
    }
    std::ranges::sort(description_.required_capabilities);
    if (std::ranges::adjacent_find(description_.required_capabilities) !=
        description_.required_capabilities.end()) {
      throw std::invalid_argument(
          "operation has duplicate capability dependencies");
    }
  }

  [[nodiscard]] auto description() const -> const OperationDescription & {
    return description_;
  }

  [[nodiscard]] auto bind(Handler handler) const -> OperationBinding {
    if (!handler) {
      throw std::invalid_argument("operation requires an executable handler");
    }
    return OperationBinding{
        description_, &Traits::supports_surface,
        [handler = std::move(handler)](bot::OperationEnvelope envelope)
            -> boost::asio::awaitable<bot::OperationReply> {
          std::optional<Request> request;
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
          } catch (...) {
            co_return bot::OperationReply::failure(
                {.code = bot::BotOperationErrorCode::InvalidRequest,
                 .message =
                     "operation payload failed codec or scope validation",
                 .retryable = false,
                 .submission_safety =
                     bot::SubmissionSafety::DefinitelyNotSubmitted});
          }
          bool returned = false;
          try {
            auto result = co_await handler(*request);
            returned = true;
            result.validate();
            if (!result.ok()) {
              co_return bot::OperationReply::failure(std::move(*result.error));
            }
            Traits::validate_result(*request, *result.value);
            co_return bot::OperationReply::success(
                bot::GatewayCodec<Result>::encode(*result.value));
          } catch (...) {
            // Owning module handlers return proven pre-submission failures as
            // typed errors. Unclassified exceptions remain conservative here.
            co_return bot::OperationReply::failure(
                {.code =
                     Traits::side_effecting
                         ? bot::BotOperationErrorCode::OutcomeUnknown
                         : (returned
                                ? bot::BotOperationErrorCode::MalformedResponse
                                : bot::BotOperationErrorCode::TransportFailure),
                 .message =
                     "operation handler did not produce a valid SDK result",
                 .retryable = !Traits::side_effecting && !returned,
                 .submission_safety =
                     Traits::side_effecting
                         ? bot::SubmissionSafety::PossiblySubmitted
                         : bot::SubmissionSafety::DefinitelyNotSubmitted});
          }
        }};
  }

private:
  OperationDescription description_;
};

class OperationRegistry final : public bot::BotOperationGateway {
public:
  explicit OperationRegistry(bot::BotInstallationRef installation);
  ~OperationRegistry() override;

  void install(OperationBinding binding);
  // Only same-installation prepared capabilities can satisfy dependencies.
  void seal(const bot::BotInstallationRef &capability_owner,
            std::span<const std::string> available_capabilities);
  void close() noexcept;

  [[nodiscard]] auto installation() const -> bot::BotInstallationRef;
  [[nodiscard]] auto declared_actions() const -> std::vector<bot::ActionId>;
  [[nodiscard]] auto descriptions() const -> std::vector<OperationDescription>;
  [[nodiscard]] auto supported_actions(
      const bot::BotInstallationRef &installation) const
      -> bot::BotOperationResult<bot::SupportedActions> override;
  auto invoke(bot::OperationEnvelope envelope)
      -> boost::asio::awaitable<bot::OperationReply> override;

private:
  struct State;
  static auto invoke_owned(std::shared_ptr<State> state,
                           bot::OperationEnvelope envelope)
      -> boost::asio::awaitable<bot::OperationReply>;
  std::shared_ptr<State> state_;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_BOT_OPERATION_REGISTRY_HPP_
