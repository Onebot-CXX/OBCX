#ifndef OBCX_INCLUDE_CORE_ORCHESTRATOR_HPP_
#define OBCX_INCLUDE_CORE_ORCHESTRATOR_HPP_

#include "common/config_snapshot.hpp"
#include "core/actor/actor.hpp"
#include "core/actor/native_actor_scheduler.hpp"

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace obcx::core {

struct OrchestratorStageExecution {
  std::string pipeline;
  std::string name;
  std::string actor;
  std::string input;
  std::string mode;
  std::string partition_key = "global";
  bool terminal_async = false;
};

struct OrchestratorFailure {
  std::string pipeline;
  std::string stage;
  std::string actor;
  ActorFailure failure;
};

struct OrchestratorResult {
  std::vector<OrchestratorStageExecution> stages;
  std::vector<MessageEnvelope> emitted;
  std::vector<OrchestratorFailure> failures;

  [[nodiscard]] auto ok() const -> bool { return failures.empty(); }
};

struct OrchestratorState;

class Orchestrator {
public:
  Orchestrator();
  explicit Orchestrator(NativeActorSchedulerOptions scheduler_options);
  Orchestrator(std::shared_ptr<NativeActorScheduler> scheduler,
               std::shared_ptr<ActorServices> runtime_services);
  Orchestrator(const Orchestrator &) = delete;
  auto operator=(const Orchestrator &) -> Orchestrator & = delete;
  Orchestrator(Orchestrator &&) = delete;
  auto operator=(Orchestrator &&) -> Orchestrator & = delete;

  void register_actor(std::shared_ptr<IActorV2> actor);
  template <typename Service>
  void register_service(std::shared_ptr<Service> service) {
    runtime_services_->register_service<Service>(std::move(service));
  }
  void configure_actors(std::vector<common::ActorConfig> actors);
  void configure_pipelines(std::vector<common::PipelineConfig> pipelines);
  void set_routing_hop_limit(size_t hop_limit);

  auto process(MessageEnvelope message,
               std::shared_ptr<void> route_lifetime = {})
      -> boost::asio::awaitable<OrchestratorResult>;

  void shutdown();
  [[nodiscard]] auto pending_terminal_tasks() const noexcept -> size_t;

private:
  std::shared_ptr<ActorServices> runtime_services_;
  std::shared_ptr<OrchestratorState> state_;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_ORCHESTRATOR_HPP_
