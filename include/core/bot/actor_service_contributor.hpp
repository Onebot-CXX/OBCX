#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <memory>
#include <string_view>

namespace obcx::core {
class ActorServices;
class ActorGenerationLifecycle;

// Process-private installation capability. Contribution only installs services;
// live admission belongs to the supplied generation lifecycle, not preparation.
class ActorServiceContributor {
public:
  static constexpr std::string_view capability_id = "bot.actor-services";
  virtual ~ActorServiceContributor() = default;
  virtual void contribute(ActorServices &,
                          std::shared_ptr<ActorGenerationLifecycle>,
                          boost::asio::any_io_executor) = 0;
};
} // namespace obcx::core
