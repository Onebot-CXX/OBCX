#include "common/config_loader.hpp"
#include "core/actor/actor.hpp"
#include "core/runtime/orchestrator.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using obcx::common::PipelineConfig;
using obcx::common::PipelineStageConfig;
using obcx::core::ActorContext;
using obcx::core::ActorResult;
using obcx::core::ActorTask;
using obcx::core::IActorV2;
using obcx::core::MessageEnvelope;
using obcx::core::Orchestrator;

auto route_type(const size_t depth) -> std::string {
  return "RoutingBenchmark" + std::to_string(depth);
}

auto parse_size(const char *value, const std::string_view option) -> size_t {
  const auto parsed = std::strtoull(value, nullptr, 10);
  if (parsed == 0) {
    throw std::invalid_argument(std::string(option) + " must be positive");
  }
  return static_cast<size_t>(parsed);
}

class RoutingBenchmarkActor final : public IActorV2 {
public:
  RoutingBenchmarkActor(const size_t depth, const size_t fan_out,
                        const size_t fan_out_depth)
      : depth_(depth), fan_out_(fan_out), fan_out_depth_(fan_out_depth) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "routing_benchmark";
  }

  [[nodiscard]] auto get_version() const -> std::string override {
    return "cpp26-cow-route";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &)
      -> ActorTask<ActorResult> override {
    const auto marker = message.type.rfind("RoutingBenchmark", 0);
    if (marker != 0) {
      co_return ActorResult::failed("benchmark_input", "unexpected input");
    }
    const auto current = static_cast<size_t>(std::stoull(
        message.type.substr(std::string("RoutingBenchmark").size())));
    ActorResult result = ActorResult::success();
    if (current >= depth_) {
      co_return result;
    }

    const auto branches = current == fan_out_depth_ ? fan_out_ : size_t{1};
    for (size_t branch = 0; branch < branches; ++branch) {
      MessageEnvelope emitted;
      emitted.id = message.id + ":" + std::to_string(current) + ":" +
                   std::to_string(branch);
      emitted.type = route_type(current + 1);
      emitted.correlation_id = message.correlation_id;
      emitted.causation_id = message.id;
      result.emit(std::move(emitted));
    }
    co_return result;
  }

private:
  size_t depth_;
  size_t fan_out_;
  size_t fan_out_depth_;
};

auto make_pipelines(const size_t depth) -> std::vector<PipelineConfig> {
  std::vector<PipelineConfig> pipelines;
  pipelines.reserve(depth + 1);
  for (size_t current = 0; current <= depth; ++current) {
    PipelineStageConfig stage;
    stage.name = "route_stage_" + std::to_string(current);
    stage.actor = "routing_benchmark";
    stage.input = route_type(current);
    stage.mode = "await";
    if (current < depth) {
      stage.outputs = {route_type(current + 1)};
    }
    pipelines.push_back(PipelineConfig{
        .name = "route_pipeline_" + std::to_string(current),
        .source = route_type(current),
        .stages = {std::move(stage)},
    });
  }
  return pipelines;
}

auto run_once(boost::asio::io_context &io, Orchestrator &orchestrator,
              const size_t iteration) -> size_t {
  MessageEnvelope message;
  message.id = "routing-benchmark-" + std::to_string(iteration);
  message.type = route_type(0);
  message.correlation_id = message.id;
  auto future = boost::asio::co_spawn(io, orchestrator.process(message),
                                      boost::asio::use_future);
  io.run();
  io.restart();
  const auto result = future.get();
  if (!result.ok()) {
    throw std::runtime_error("routing benchmark produced a route failure");
  }
  return result.stages.size();
}

} // namespace

auto main(int argc, char **argv) -> int {
  size_t depth = 16;
  size_t fan_out = 32;
  size_t iterations = 100;
  for (int index = 1; index < argc; ++index) {
    const std::string_view option = argv[index];
    if (index + 1 >= argc) {
      throw std::invalid_argument(std::string(option) + " requires a value");
    }
    if (option == "--depth") {
      depth = parse_size(argv[++index], option);
    } else if (option == "--fan-out") {
      fan_out = parse_size(argv[++index], option);
    } else if (option == "--iterations") {
      iterations = parse_size(argv[++index], option);
    } else {
      throw std::invalid_argument("unknown option: " + std::string(option));
    }
  }

  const auto fan_out_depth = depth / 2;
  Orchestrator orchestrator;
  orchestrator.set_routing_hop_limit(depth + 1);
  orchestrator.register_actor(
      std::make_shared<RoutingBenchmarkActor>(depth, fan_out, fan_out_depth));
  orchestrator.configure_pipelines(make_pipelines(depth));
  boost::asio::io_context io;

  (void)run_once(io, orchestrator, 0);
  size_t stages = 0;
  const auto started = std::chrono::steady_clock::now();
  for (size_t iteration = 0; iteration < iterations; ++iteration) {
    stages += run_once(io, orchestrator, iteration + 1);
  }
  const auto elapsed = std::chrono::steady_clock::now() - started;
  const auto elapsed_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();

  std::cout << "route_context_storage=copy-on-write-linked\n"
            << "depth=" << depth << "\n"
            << "fan_out=" << fan_out << "\n"
            << "iterations=" << iterations << "\n"
            << "stage_executions=" << stages << "\n"
            << "elapsed_ns=" << elapsed_ns << "\n"
            << "ns_per_stage="
            << (stages == 0 ? 0 : elapsed_ns / static_cast<long long>(stages))
            << '\n';
  orchestrator.shutdown();
  return 0;
}
