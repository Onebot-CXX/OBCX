#pragma once

#include "core/bot/platform_domain_binding.hpp"
#include "core/bot/self_identity_cache.hpp"
#include "onebot11/bot/standard_login.hpp"
#include "onebot11/bot/transport.hpp"
#include "standard_transport_access.hpp"

namespace obcx::onebot11::bot::standard::detail {

// One installation-owned coordination boundary. The cache alone owns refresh
// merging, timer and generation; incompatible explicit calls own their proofs.
class LoginCoordinator final
    : public std::enable_shared_from_this<LoginCoordinator> {
public:
  LoginCoordinator(std::shared_ptr<core::SelfIdentityCache>,
                   std::shared_ptr<const core::PlatformDomainBinding>,
                   obcx::bot::BotInstallationRef);
  auto capture_origin() const -> std::optional<core::SelfIdentityOrigin>;
  auto send(core::OneBot11TransportCapability &, ExecutionMode,
            std::string parameters, std::uint64_t echo,
            std::shared_ptr<StandardCallState>, core::SelfIdentityOrigin)
      -> CapturedStandardSend;
  auto refresh(std::shared_ptr<core::OneBot11Transport>, std::string payload,
               std::uint64_t echo, core::SelfIdentityOrigin)
      -> boost::asio::awaitable<core::SelfIdentityCache::Result>;
  auto publish_completed(const core::SelfIdentityOrigin &, const LoginInfo &)
      -> core::SelfObservationResult;

private:
  auto publish(const core::SelfIdentityOrigin &, const obcx::bot::SelfInfo &)
      -> core::SelfObservationResult;
  static auto run_refresh(std::shared_ptr<LoginCoordinator>,
                          std::shared_ptr<core::OneBot11Transport>,
                          std::string payload, std::uint64_t echo,
                          core::SelfIdentityOrigin)
      -> boost::asio::awaitable<core::SelfIdentityCache::Result>;
  std::shared_ptr<core::SelfIdentityCache> identity_;
  std::shared_ptr<const core::PlatformDomainBinding> binding_;
  obcx::bot::BotInstallationRef installation_;
};
} // namespace obcx::onebot11::bot::standard::detail
