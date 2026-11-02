#include "standard_login_coordinator.hpp"

#include "core/bot/operation_response_helpers.hpp"
#include "onebot11/adapter/native_fields.hpp"
#include "onebot11/bot/response_parser.hpp"

namespace obcx::onebot11::bot::standard::detail {
namespace domain = obcx::bot;

LoginCoordinator::LoginCoordinator(
    std::shared_ptr<core::SelfIdentityCache> identity,
    std::shared_ptr<const core::PlatformDomainBinding> binding,
    domain::BotInstallationRef installation)
    : identity_(std::move(identity)), binding_(std::move(binding)),
      installation_(std::move(installation)) {
  if (!identity_ || !binding_)
    throw std::invalid_argument("login coordinator requires identity owners");
  binding_->codec().validate(installation_);
}
auto LoginCoordinator::capture_origin() const
    -> std::optional<core::SelfIdentityOrigin> {
  return identity_->capture_origin();
}
auto LoginCoordinator::send(core::OneBot11TransportCapability &transport,
                            ExecutionMode mode, std::string parameters,
                            std::uint64_t echo,
                            std::shared_ptr<StandardCallState> call,
                            core::SelfIdentityOrigin origin)
    -> CapturedStandardSend {
  // No joining refresh or another explicit request: controls and proof belong
  // to this invocation, and the captured origin is never replaced.
  return StandardTransportAccess::send(
      transport, NativeAction::GetLoginInfo, mode, std::move(parameters), echo,
      std::move(call), identity_, std::move(origin));
}
auto LoginCoordinator::publish(const core::SelfIdentityOrigin &origin,
                               const domain::SelfInfo &info)
    -> core::SelfObservationResult {
  try {
    domain::binding::validate_native_self(binding_->codec(), info);
    if (info.user.installation != installation_)
      return {core::SelfObservationStatus::InvalidIdentity, std::nullopt};
  } catch (...) {
    return {core::SelfObservationStatus::InvalidIdentity, std::nullopt};
  }
  return identity_->publish_self(origin, info);
}
auto LoginCoordinator::publish_completed(const core::SelfIdentityOrigin &origin,
                                         const LoginInfo &value)
    -> core::SelfObservationResult {
  if (value.installation != installation_ || value.user_id <= 0)
    return {core::SelfObservationStatus::InvalidIdentity, std::nullopt};
  domain::UserRef user{value.installation,
                       binding_->codec().make<domain::UserId>(
                           "user", {std::to_string(value.user_id)})};
  binding_->codec().validate(user);
  // Standard nickname text is not restricted to SelfInfo's identifier shape.
  // Check only that optional projection before copying it. A valid native
  // account change must still invalidate the old snapshot/evidence even if
  // its profile cannot be cached; same-account failure retains the snapshot.
  try {
    if (!value.nickname.empty())
      domain::detail::validate_identifier(value.nickname, "self display_name",
                                          2048);
  } catch (const std::invalid_argument &) {
    return identity_->observe_user(origin, std::move(user));
  }
  domain::SelfInfo info{.user = std::move(user),
                        .display_name = value.nickname.empty()
                                            ? std::nullopt
                                            : std::optional{value.nickname},
                        .username = std::nullopt};
  return publish(origin, info);
}
auto LoginCoordinator::refresh(
    std::shared_ptr<core::OneBot11Transport> transport, std::string payload,
    std::uint64_t echo, core::SelfIdentityOrigin origin)
    -> boost::asio::awaitable<core::SelfIdentityCache::Result> {
  return run_refresh(shared_from_this(), std::move(transport),
                     std::move(payload), echo, std::move(origin));
}
auto LoginCoordinator::run_refresh(
    std::shared_ptr<LoginCoordinator> self,
    std::shared_ptr<core::OneBot11Transport> transport, std::string payload,
    std::uint64_t echo, core::SelfIdentityOrigin origin)
    -> boost::asio::awaitable<core::SelfIdentityCache::Result> {
  // Automatic refresh retains its existing configured profile-transport
  // policy. It does not fabricate standard call limits or standard evidence.
  co_return co_await transport->send_profile_action(
      std::move(payload), echo, std::move(origin),
      [self](const core::SelfIdentityOrigin &source) {
        return self->identity_->observe_user_and_capture(source, std::nullopt,
                                                         [] {});
      },
      [self](std::string_view response,
             const core::SelfIdentityOrigin &source) {
        const auto parsed = parse_onebot11_operation_response(response, false);
        if (!parsed.ok())
          return core::operation_detail::provider_failure<domain::SelfInfo>(
              parsed);
        try {
          const auto &data = core::operation_detail::parsed_value(parsed);
          const auto id = adapter::onebot11::numeric_identity(data, "user_id");
          if (!id)
            return core::operation_detail::malformed_read<domain::SelfInfo>(
                "OneBot self ID is missing");
          auto name = core::operation_detail::optional_string(data, "nickname");
          domain::SelfInfo info{
              .user = {self->installation_,
                       self->binding_->codec().make<domain::UserId>("user",
                                                                    {*id})},
              .display_name = name.empty() ? std::nullopt : std::optional{name},
              .username = std::nullopt};
          const auto publication = self->publish(source, info);
          if (publication.accepted())
            return domain::BotOperationResult<domain::SelfInfo>::success(
                std::move(info));
          return network::onebot11::retired_profile();
        } catch (...) {
          return core::operation_detail::malformed_read<domain::SelfInfo>(
              "OneBot self profile is invalid");
        }
      });
}
} // namespace obcx::onebot11::bot::standard::detail
