#include "core/runtime/orchestrator.hpp"

#include "common/logger.hpp"
#include "core/actor/actor_work_stealing_executor.hpp"

#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace obcx::core {

struct OrchestratorRoutingConfig {
  std::unordered_set<std::string> actor_names;
  std::unordered_map<std::string, std::string> actor_partition_expressions;
  std::unordered_map<std::string, common::ActorConfig> actor_configs;
  std::vector<common::PipelineConfig> pipelines;
  size_t routing_hop_limit = 32;
};

struct OrchestratorState {
  std::atomic<std::shared_ptr<const OrchestratorRoutingConfig>> routing_config =
      std::make_shared<const OrchestratorRoutingConfig>();
  std::mutex routing_config_mutex;
  std::shared_ptr<NativeActorScheduler> scheduler;
  std::atomic_bool accepting_terminal_tasks = true;
  std::atomic_size_t terminal_tasks = 0;

  explicit OrchestratorState(
      std::shared_ptr<NativeActorScheduler> native_scheduler)
      : scheduler(std::move(native_scheduler)) {}
};

namespace {

struct RoutingNode {
  std::string pipeline;
  std::string stage;
  std::string message_type;

  auto operator==(const RoutingNode &) const -> bool = default;
};

struct RoutingLink {
  RoutingNode node;
  std::shared_ptr<const RoutingLink> parent;
};

struct RoutingContext {
  size_t hops = 0;
  std::shared_ptr<const RoutingLink> tail;
  std::shared_ptr<void> lifetime;
};

struct RoutedMessage {
  MessageEnvelope envelope;
  RoutingContext route;
};

auto normalized_mode(const common::PipelineStageConfig &stage) -> std::string {
  if (stage.mode.empty()) {
    return "await";
  }
  return stage.mode;
}

auto has_completed_dependencies(
    const common::PipelineStageConfig &stage,
    const std::unordered_set<std::string> &completed) -> bool {
  return std::all_of(stage.after.begin(), stage.after.end(),
                     [&completed](const auto &dependency) {
                       return completed.contains(dependency);
                     });
}

auto find_message(const std::vector<RoutedMessage> &messages,
                  const std::string &type) -> const RoutedMessage * {
  const auto it =
      std::find_if(messages.begin(), messages.end(), [&type](const auto &msg) {
        return msg.envelope.type == type;
      });
  if (it == messages.end()) {
    return nullptr;
  }
  return &*it;
}

auto bounded_route_trace(const RoutingContext &route,
                         const RoutingNode &attempted) -> std::string {
  constexpr size_t max_trace_nodes = 8;
  std::array<const RoutingNode *, max_trace_nodes> reverse_nodes{};
  size_t count = 0;
  reverse_nodes[count++] = &attempted;
  auto cursor = route.tail;
  while (cursor && count < max_trace_nodes) {
    reverse_nodes[count++] = &cursor->node;
    cursor = cursor->parent;
  }

  std::string trace;
  if (cursor) {
    trace = "... -> ";
  }
  for (size_t offset = count; offset != 0; --offset) {
    const auto &node = *reverse_nodes[offset - 1];
    if (!trace.empty()) {
      if (!trace.ends_with(" -> ")) {
        trace += " -> ";
      }
    }
    trace += node.pipeline + "." + node.stage + "(" + node.message_type + ")";
  }
  return trace;
}

auto routing_failure(const std::string &code, const RoutingContext &route,
                     const RoutingNode &attempted,
                     const MessageEnvelope &message) -> ActorFailure {
  const auto identity =
      message.correlation_id.empty() ? message.id : message.correlation_id;
  return ActorFailure{
      .code = code,
      .message = "route rejected at " + attempted.pipeline + "." +
                 attempted.stage + " for " + attempted.message_type +
                 " (message=" + identity +
                 ", hops=" + std::to_string(route.hops) +
                 ", trace=" + bounded_route_trace(route, attempted) + ")",
      .retryable = false,
  };
}

auto enter_route_node(RoutingContext &route, const RoutingNode &node,
                      const MessageEnvelope &message, const size_t hop_limit)
    -> std::optional<ActorFailure> {
  for (auto cursor = route.tail; cursor; cursor = cursor->parent) {
    if (cursor->node == node) {
      return routing_failure("message_routing_cycle", route, node, message);
    }
  }
  if (route.hops >= hop_limit) {
    return routing_failure("message_routing_hop_limit", route, node, message);
  }
  ++route.hops;
  route.tail = std::make_shared<const RoutingLink>(node, route.tail);
  return std::nullopt;
}

auto stage_has_downstream_dependency(const common::PipelineConfig &pipeline,
                                     const common::PipelineStageConfig &stage)
    -> bool {
  return std::any_of(pipeline.stages.begin(), pipeline.stages.end(),
                     [&stage](const auto &candidate) {
                       const auto waits_for_stage =
                           std::find(candidate.after.begin(),
                                     candidate.after.end(),
                                     stage.name) != candidate.after.end();
                       const auto consumes_stage_output =
                           std::find(stage.outputs.begin(), stage.outputs.end(),
                                     candidate.input) != stage.outputs.end();
                       return waits_for_stage || consumes_stage_output;
                     });
}

auto is_terminal_async(const common::PipelineConfig &pipeline,
                       const common::PipelineStageConfig &stage) -> bool {
  return normalized_mode(stage) == "async" &&
         !stage_has_downstream_dependency(pipeline, stage);
}

auto pipeline_json_scalar_to_string(const common::json &value) -> std::string {
  if (value.is_string()) {
    return value.get<std::string>();
  }
  if (value.is_number_integer()) {
    return std::to_string(value.get<std::int64_t>());
  }
  if (value.is_number_unsigned()) {
    return std::to_string(value.get<std::uint64_t>());
  }
  if (value.is_boolean()) {
    return value.get<bool>() ? "true" : "false";
  }
  return {};
}

auto message_field_value(const MessageEnvelope &message,
                         const std::string &field) -> std::string {
  if (field == "id") {
    return message.id;
  }
  if (field == "type") {
    return message.type;
  }
  if (field == "source_platform") {
    return message.source_platform;
  }
  if (field == "source_bot") {
    return message.source_bot;
  }
  if (field == "conversation_id") {
    return message.conversation_id;
  }
  if (field == "correlation_id") {
    return message.correlation_id;
  }
  if (field == "causation_id") {
    return message.causation_id;
  }

  if (auto header = message.headers.find(field);
      header != message.headers.end()) {
    return header->second;
  }

  if (message.payload.is_object() && message.payload.contains(field)) {
    return pipeline_json_scalar_to_string(message.payload.at(field));
  }

  if (message.raw.is_object() && message.raw.contains(field)) {
    return pipeline_json_scalar_to_string(message.raw.at(field));
  }

  return {};
}

auto resolve_partition_key(const std::string &expression,
                           const MessageEnvelope &message) -> std::string {
  if (expression.empty() || expression == "global") {
    return "global";
  }

  std::stringstream stream(expression);
  std::string token;
  std::vector<std::string> values;
  while (std::getline(stream, token, ':')) {
    values.push_back(message_field_value(message, token));
  }

  std::string partition_key;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      partition_key += ':';
    }
    partition_key += values[i];
  }

  return partition_key.empty() ? "global" : partition_key;
}

auto build_failure_envelope(const MessageEnvelope &input,
                            const std::string &pipeline,
                            const std::string &stage, const std::string &actor,
                            const ActorFailure &failure) -> MessageEnvelope {
  MessageEnvelope envelope;
  envelope.id = "failure:" + pipeline + ":" + stage + ":" + input.id;
  envelope.type = "ActorFailed";
  envelope.source_platform = input.source_platform;
  envelope.source_bot = input.source_bot;
  envelope.conversation_id = input.conversation_id;
  envelope.correlation_id =
      input.correlation_id.empty() ? input.id : input.correlation_id;
  envelope.causation_id = input.id;
  envelope.headers = input.headers;
  envelope.payload = {
      {"pipeline", pipeline},
      {"stage", stage},
      {"actor", actor},
      {"code", failure.code},
      {"message", failure.message},
      {"retryable", failure.retryable},
  };
  return envelope;
}

void merge_result(OrchestratorResult &target, OrchestratorResult source) {
  for (auto &stage : source.stages) {
    target.stages.push_back(std::move(stage));
  }
  for (auto &emitted : source.emitted) {
    target.emitted.push_back(std::move(emitted));
  }
  for (auto &failure : source.failures) {
    target.failures.push_back(std::move(failure));
  }
}

auto process_message(
    std::shared_ptr<OrchestratorState> state,
    std::shared_ptr<const OrchestratorRoutingConfig> routing_config,
    MessageEnvelope message, RoutingContext route)
    -> boost::asio::awaitable<OrchestratorResult>;

template <typename CompletionToken>
auto async_enqueue_native(std::shared_ptr<OrchestratorState> state,
                          ActorInvocation invocation, CompletionToken &&token) {
  return boost::asio::async_initiate<CompletionToken, void(ActorResult)>(
      [state = std::move(state),
       invocation = std::move(invocation)](auto &&handler) mutable {
        using handler_type = std::decay_t<decltype(handler)>;
        auto executor = boost::asio::any_io_executor{
            boost::asio::get_associated_executor(handler)};
        auto shared_handler = std::make_shared<handler_type>(
            std::forward<decltype(handler)>(handler));
        auto work_guard = std::make_shared<
            boost::asio::executor_work_guard<boost::asio::any_io_executor>>(
            boost::asio::make_work_guard(executor));
        state->scheduler->enqueue(
            std::move(invocation),
            [state = std::move(state), shared_handler, executor,
             work_guard](ActorResult result) mutable {
              boost::asio::post(
                  executor, [state = std::move(state), shared_handler,
                             result = std::move(result), work_guard]() mutable {
                    (*shared_handler)(std::move(result));
                    work_guard->reset();
                    state.reset();
                  });
            });
      },
      token);
}

struct TerminalStageWork {
  ActorInvocation invocation;
  std::string pipeline;
  std::string stage;
  std::string actor;
  RoutingContext route;
  std::shared_ptr<const OrchestratorRoutingConfig> routing_config;
};

auto run_terminal_stage(std::shared_ptr<OrchestratorState> state,
                        TerminalStageWork work)
    -> boost::asio::awaitable<void> {
  const auto actor_input = work.invocation.message;
  ActorResult actor_result;
  try {
    actor_result = co_await async_enqueue_native(
        state, std::move(work.invocation), boost::asio::use_awaitable);
  } catch (const std::exception &error) {
    actor_result = ActorResult::failed("actor_exception", error.what(), true);
  } catch (...) {
    actor_result =
        ActorResult::failed("actor_exception", "unknown actor exception", true);
  }

  if (!actor_result.ok()) {
    auto failure_envelope =
        build_failure_envelope(actor_input, work.pipeline, work.stage,
                               work.actor, *actor_result.failure);
    auto routed = co_await process_message(
        state, work.routing_config, std::move(failure_envelope), work.route);
    if (routed.stages.empty()) {
      OBCX_ERROR("Terminal async actor {} failed in {}.{} with {}: {}; no "
                 "ActorFailed pipeline handled the failure",
                 work.actor, work.pipeline, work.stage,
                 actor_result.failure->code, actor_result.failure->message);
    } else if (!routed.ok()) {
      OBCX_ERROR(
          "ActorFailed pipeline could not handle terminal async failure from "
          "{}.{} ({})",
          work.pipeline, work.stage, actor_result.failure->code);
    }
  }

  for (auto &emitted : actor_result.emitted) {
    (void)co_await process_message(state, work.routing_config,
                                   std::move(emitted), work.route);
  }
}

void log_terminal_exception(std::exception_ptr exception) {
  if (!exception) {
    return;
  }

  try {
    std::rethrow_exception(exception);
  } catch (const std::exception &error) {
    OBCX_ERROR("Unhandled terminal async orchestration exception: {}",
               error.what());
  } catch (...) {
    OBCX_ERROR("Unhandled unknown terminal async orchestration exception");
  }
}

auto process_message(
    std::shared_ptr<OrchestratorState> state,
    std::shared_ptr<const OrchestratorRoutingConfig> routing_config,
    MessageEnvelope message, RoutingContext route)
    -> boost::asio::awaitable<OrchestratorResult> {
  OrchestratorResult orchestrator_result;

  for (const auto &pipeline : routing_config->pipelines) {
    if (pipeline.source != message.type) {
      continue;
    }

    std::vector<RoutedMessage> available_messages{
        RoutedMessage{.envelope = message, .route = route}};
    std::unordered_set<std::string> completed_stages;

    for (const auto run_terminal_async : {false, true}) {
      bool made_progress = true;
      while (made_progress) {
        made_progress = false;

        for (const auto &stage : pipeline.stages) {
          if (completed_stages.contains(stage.name) ||
              is_terminal_async(pipeline, stage) != run_terminal_async ||
              !has_completed_dependencies(stage, completed_stages)) {
            continue;
          }

          const auto *stage_input =
              find_message(available_messages, stage.input);
          if (stage_input == nullptr) {
            continue;
          }

          const RoutedMessage routed_input = *stage_input;
          const MessageEnvelope &actor_input = routed_input.envelope;
          auto stage_route = routed_input.route;
          const RoutingNode route_node{.pipeline = pipeline.name,
                                       .stage = stage.name,
                                       .message_type = actor_input.type};
          if (auto failure =
                  enter_route_node(stage_route, route_node, actor_input,
                                   routing_config->routing_hop_limit)) {
            orchestrator_result.failures.push_back(
                OrchestratorFailure{.pipeline = pipeline.name,
                                    .stage = stage.name,
                                    .actor = stage.actor,
                                    .failure = std::move(*failure)});
            completed_stages.insert(stage.name);
            made_progress = true;
            continue;
          }

          if (!routing_config->actor_names.contains(stage.actor)) {
            const auto failure =
                ActorFailure{.code = "actor_not_registered",
                             .message = "actor is not registered",
                             .retryable = false};
            orchestrator_result.failures.push_back(
                OrchestratorFailure{.pipeline = pipeline.name,
                                    .stage = stage.name,
                                    .actor = stage.actor,
                                    .failure = failure});
            auto failure_envelope = build_failure_envelope(
                actor_input, pipeline.name, stage.name, stage.actor, failure);
            available_messages.push_back(RoutedMessage{
                .envelope = failure_envelope, .route = stage_route});
            auto routed = co_await process_message(
                state, routing_config, failure_envelope, stage_route);
            orchestrator_result.emitted.push_back(std::move(failure_envelope));
            merge_result(orchestrator_result, std::move(routed));
            completed_stages.insert(stage.name);
            made_progress = true;
            continue;
          }

          const auto partition_expression =
              routing_config->actor_partition_expressions.contains(stage.actor)
                  ? routing_config->actor_partition_expressions.at(stage.actor)
                  : "global";
          const auto partition_key =
              resolve_partition_key(partition_expression, actor_input);

          orchestrator_result.stages.push_back(
              OrchestratorStageExecution{.pipeline = pipeline.name,
                                         .name = stage.name,
                                         .actor = stage.actor,
                                         .input = stage.input,
                                         .mode = normalized_mode(stage),
                                         .partition_key = partition_key,
                                         .terminal_async = run_terminal_async});

          if (run_terminal_async) {
            if (!state->accepting_terminal_tasks.load(
                    std::memory_order_acquire)) {
              completed_stages.insert(stage.name);
              made_progress = true;
              continue;
            }
            auto executor = co_await boost::asio::this_coro::executor;
            const auto config_it =
                routing_config->actor_configs.find(stage.actor);
            const auto db_instance =
                config_it == routing_config->actor_configs.end()
                    ? std::string{}
                    : config_it->second.db;
            const auto db_namespace =
                config_it == routing_config->actor_configs.end()
                    ? std::string{}
                    : config_it->second.db_namespace;

            state->terminal_tasks.fetch_add(1, std::memory_order_acq_rel);
            try {
              boost::asio::post(
                  executor,
                  [state, executor,
                   work = TerminalStageWork{
                       .invocation =
                           ActorInvocation{.actor_id = stage.actor,
                                           .partition_key = partition_key,
                                           .db_instance = db_instance,
                                           .db_namespace = db_namespace,
                                           .message = actor_input},
                       .pipeline = pipeline.name,
                       .stage = stage.name,
                       .actor = stage.actor,
                       .route = stage_route,
                       .routing_config = routing_config}]() mutable {
                    boost::asio::co_spawn(
                        executor, run_terminal_stage(state, std::move(work)),
                        [state](std::exception_ptr exception) {
                          log_terminal_exception(std::move(exception));
                          state->terminal_tasks.fetch_sub(
                              1, std::memory_order_acq_rel);
                        });
                  });
            } catch (...) {
              state->terminal_tasks.fetch_sub(1, std::memory_order_acq_rel);
              throw;
            }

            completed_stages.insert(stage.name);
            made_progress = true;
            continue;
          }

          ActorResult actor_result;
          try {
            const auto config_it =
                routing_config->actor_configs.find(stage.actor);
            const auto db_instance =
                config_it == routing_config->actor_configs.end()
                    ? std::string{}
                    : config_it->second.db;
            const auto db_namespace =
                config_it == routing_config->actor_configs.end()
                    ? std::string{}
                    : config_it->second.db_namespace;
            actor_result = co_await async_enqueue_native(
                state,
                ActorInvocation{.actor_id = stage.actor,
                                .partition_key = partition_key,
                                .db_instance = db_instance,
                                .db_namespace = db_namespace,
                                .message = actor_input},
                boost::asio::use_awaitable);
          } catch (const std::exception &error) {
            actor_result =
                ActorResult::failed("actor_exception", error.what(), true);
          }

          if (!actor_result.ok()) {
            orchestrator_result.failures.push_back(
                OrchestratorFailure{.pipeline = pipeline.name,
                                    .stage = stage.name,
                                    .actor = stage.actor,
                                    .failure = *actor_result.failure});
            auto failure_envelope =
                build_failure_envelope(actor_input, pipeline.name, stage.name,
                                       stage.actor, *actor_result.failure);
            available_messages.push_back(RoutedMessage{
                .envelope = failure_envelope, .route = stage_route});
            auto routed = co_await process_message(
                state, routing_config, failure_envelope, stage_route);
            orchestrator_result.emitted.push_back(std::move(failure_envelope));
            merge_result(orchestrator_result, std::move(routed));
          }

          for (auto &emitted : actor_result.emitted) {
            available_messages.push_back(
                RoutedMessage{.envelope = emitted, .route = stage_route});
            auto routed = co_await process_message(state, routing_config,
                                                   emitted, stage_route);
            orchestrator_result.emitted.push_back(std::move(emitted));
            merge_result(orchestrator_result, std::move(routed));
          }

          completed_stages.insert(stage.name);
          made_progress = true;
        }
      }
    }
  }

  co_return orchestrator_result;
}

} // namespace

Orchestrator::Orchestrator() : Orchestrator(NativeActorSchedulerOptions{}) {}

Orchestrator::Orchestrator(NativeActorSchedulerOptions scheduler_options)
    : runtime_services_(std::make_shared<ActorServices>()),
      state_(std::make_shared<OrchestratorState>(
          std::make_shared<NativeActorScheduler>(std::move(scheduler_options),
                                                 runtime_services_))) {}

Orchestrator::Orchestrator(std::shared_ptr<NativeActorScheduler> scheduler,
                           std::shared_ptr<ActorServices> runtime_services)
    : runtime_services_(std::move(runtime_services)),
      state_(std::make_shared<OrchestratorState>(std::move(scheduler))) {
  if (!runtime_services_ || !state_->scheduler) {
    throw std::invalid_argument(
        "Orchestrator requires native scheduler and runtime services");
  }
}

void Orchestrator::register_actor(std::shared_ptr<IActorV2> actor) {
  if (!actor) {
    return;
  }
  const auto actor_name = actor->get_name();
  state_->scheduler->register_actor(std::move(actor));
  std::scoped_lock lock(state_->routing_config_mutex);
  auto updated = std::make_shared<OrchestratorRoutingConfig>(
      *state_->routing_config.load(std::memory_order_acquire));
  updated->actor_names.insert(actor_name);
  state_->routing_config.store(std::move(updated), std::memory_order_release);
}

void Orchestrator::configure_actors(std::vector<common::ActorConfig> actors) {
  std::scoped_lock lock(state_->routing_config_mutex);
  auto updated = std::make_shared<OrchestratorRoutingConfig>(
      *state_->routing_config.load(std::memory_order_acquire));
  updated->actor_partition_expressions.clear();
  updated->actor_configs.clear();
  for (const auto &actor : actors) {
    updated->actor_partition_expressions[actor.name] =
        actor.partition.empty() ? "global" : actor.partition;
    updated->actor_configs[actor.name] = actor;
  }
  state_->routing_config.store(std::move(updated), std::memory_order_release);
}

void Orchestrator::configure_pipelines(
    std::vector<common::PipelineConfig> pipelines) {
  std::scoped_lock lock(state_->routing_config_mutex);
  auto updated = std::make_shared<OrchestratorRoutingConfig>(
      *state_->routing_config.load(std::memory_order_acquire));
  updated->pipelines = std::move(pipelines);
  state_->routing_config.store(std::move(updated), std::memory_order_release);
}

auto Orchestrator::process(MessageEnvelope message,
                           std::shared_ptr<void> route_lifetime)
    -> boost::asio::awaitable<OrchestratorResult> {
  auto routing_config = state_->routing_config.load(std::memory_order_acquire);
  return process_message(state_, std::move(routing_config), std::move(message),
                         RoutingContext{.lifetime = std::move(route_lifetime)});
}

void Orchestrator::set_routing_hop_limit(const size_t hop_limit) {
  if (hop_limit == 0) {
    throw std::invalid_argument("routing hop limit must be greater than zero");
  }
  std::scoped_lock lock(state_->routing_config_mutex);
  auto updated = std::make_shared<OrchestratorRoutingConfig>(
      *state_->routing_config.load(std::memory_order_acquire));
  updated->routing_hop_limit = hop_limit;
  state_->routing_config.store(std::move(updated), std::memory_order_release);
}

void Orchestrator::shutdown() {
  state_->accepting_terminal_tasks.store(false, std::memory_order_release);
  state_->scheduler->shutdown(ActorExecutorShutdownMode::Cancel);
}

auto Orchestrator::pending_terminal_tasks() const noexcept -> size_t {
  return state_->terminal_tasks.load(std::memory_order_acquire);
}

} // namespace obcx::core
