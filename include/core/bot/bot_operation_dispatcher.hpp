#pragma once

#include "core/bot/operation_registry.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <functional>
#include <memory>

namespace obcx::core {

using BotOperationEndpoint = OperationRegistry;

struct SuccessfulBotOperation {
  bot::BotInstallationRef installation;
  bot::ActionId action;
  std::string attempt_id;
  std::optional<bot::SendReceipt> send_receipt;
};

class BotOperationDispatcher final
    : public bot::BotOperationGateway,
      public bot::StructuredOperationGateway,
      public bot::OwnedStructuredOperationGateway {
public:
  using bot::OwnedStructuredOperationGateway::invoke_owned;
  using SurfaceValidator = std::function<bool(const bot::SurfaceId &)>;
  using SuccessHandler = std::function<boost::asio::awaitable<void>(
      SuccessfulBotOperation operation)>;

  using ConfirmationHandler = std::function<boost::asio::awaitable<void>(
      bot::SendConfirmation confirmation)>;

  explicit BotOperationDispatcher(SurfaceValidator surface_registered);
  ~BotOperationDispatcher() override;

  void register_endpoint(std::shared_ptr<OperationRegistry> endpoint);
  void set_success_handler(boost::asio::any_io_executor executor,
                           SuccessHandler handler);
  void set_confirmation_handler(boost::asio::any_io_executor executor,
                                ConfirmationHandler handler);
  // Close BOTH notification paths before draining the process-owned executor.
  // Already accepted notifications retain their callable until completion.
  void close_success_notifications() noexcept;
  void seal_registrations();
  void clear_endpoints() noexcept;
  void refresh_self_identities();
  [[nodiscard]] auto normalize_live_message(
      const bot::BotInstallationRef &installation,
      const common::MessageEvent &event, bot::TimePoint received_at,
      std::string_view observation_id,
      const std::optional<SelfIdentityOrigin> &origin) const
      -> bot::MessageReceived;
  void admit_message(const bot::MessageReceived &message,
                     std::uint64_t expected_generation);
  [[nodiscard]] auto endpoint_count() const noexcept -> std::size_t;

  [[nodiscard]] auto supported_actions(
      const bot::BotInstallationRef &installation) const
      -> bot::BotOperationResult<bot::SupportedActions> override;
  auto invoke(bot::OperationEnvelope envelope)
      -> boost::asio::awaitable<bot::OperationReply> override;

protected:
  auto invoke_owned_structured(bot::OwnedStructuredOperationEnvelope envelope,
                               std::type_index result_type)
      -> boost::asio::awaitable<bot::StructuredOperationReply> override;
  auto invoke_structured(bot::OperationEnvelope envelope,
                         std::type_index result_type)
      -> boost::asio::awaitable<bot::StructuredOperationReply> override;

private:
  struct State;
  using SendEvidenceAdmission = OperationRegistry::SendEvidenceAdmission;
  static auto capture_send_evidence(const std::shared_ptr<State> &state,
                                    const bot::BotInstallationRef &installation,
                                    const bot::ActionId &action) noexcept
      -> std::optional<SendEvidenceAdmission>;
  static void observe_structured_completion(
      const std::shared_ptr<State> &state, SuccessfulBotOperation completed,
      const std::optional<bot::OperationCorrelation> &correlation,
      StructuredOperationCompletion &completion);
  static auto invoke_owned_structured_state(
      std::shared_ptr<State> state,
      bot::OwnedStructuredOperationEnvelope envelope,
      std::type_index result_type,
      std::optional<SendEvidenceAdmission> admission)
      -> boost::asio::awaitable<bot::StructuredOperationReply>;
  static auto invoke_structured_owned(
      std::shared_ptr<State> state, bot::OperationEnvelope envelope,
      std::type_index result_type,
      std::optional<SendEvidenceAdmission> admission)
      -> boost::asio::awaitable<bot::StructuredOperationReply>;
  static auto invoke_owned(std::shared_ptr<State> state,
                           bot::OperationEnvelope envelope,
                           std::optional<SendEvidenceAdmission> admission)
      -> boost::asio::awaitable<bot::OperationReply>;
  std::shared_ptr<State> state_;
};

} // namespace obcx::core
