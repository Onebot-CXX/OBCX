#include "core/actor/actor_messages.hpp"
#include "core/actor/reflected_actor.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

namespace obcx::tests::events {

struct ReloadProbe {};

inline void from_json(const common::json &, ReloadProbe &) {}

struct ReloadEmission {
  std::string generation;
  std::string sink_path;
};

struct ReloadCommand final : command::RequestMessage<ReloadCommand> {};

inline void to_json(common::json &json, const ReloadEmission &message) {
  json = {{"generation", message.generation}, {"sink_path", message.sink_path}};
}

inline void from_json(const common::json &json, ReloadEmission &message) {
  message.generation = json.value("generation", std::string{});
  message.sink_path = json.value("sink_path", std::string{});
}

} // namespace obcx::tests::events

namespace obcx::tests {

static void write_marker(const std::string &path, const std::string &value) {
  if (path.empty()) {
    return;
  }
  std::ofstream output(path);
  output << value;
}

class ReloadLifecycleActor final
    : public core::ReflectedActor<ReloadLifecycleActor> {
public:
  static constexpr std::string_view actor_name = "reload_lifecycle_actor";
  static constexpr std::string_view actor_version = "1.0.0";

  static constexpr auto command_contract() {
    return command::catalog(command::observe<events::ReloadCommand>(
        "reload_probe", "Exercise generation command draining",
        command::re2(R"(^(?:reload_probe|reload_alias)$)")));
  }

  ~ReloadLifecycleActor() override {
    std::shared_ptr<boost::asio::steady_timer> timer;
    {
      std::scoped_lock lock(background_mutex_);
      timer = std::move(background_timer_);
    }
    if (timer) {
      try {
        auto executor = timer->get_executor();
        boost::asio::post(std::move(executor),
                          [timer = std::move(timer)] { timer->cancel(); });
      } catch (...) {
      }
    }
  }

  auto handle(const events::ReloadProbe &, const core::MessageEnvelope &message,
              core::ActorContext &context)
      -> core::ActorTask<core::ActorResult> {
    const auto background_cancel_path =
        message.payload.value("background_cancel_path", std::string{});
    if (!background_cancel_path.empty()) {
      auto executor = context.get_service<boost::asio::any_io_executor>();
      if (!executor) {
        co_return core::ActorResult::failed("reload_test_executor_missing",
                                            "actor I/O executor is missing");
      }
      auto timer = std::make_shared<boost::asio::steady_timer>(*executor);
      timer->expires_at(std::chrono::steady_clock::time_point::max());
      timer->async_wait([timer, background_cancel_path](
                            const boost::system::error_code &error) {
        if (error == boost::asio::error::operation_aborted) {
          write_marker(background_cancel_path, "cancelled");
        }
      });
      {
        std::scoped_lock lock(background_mutex_);
        background_timer_ = std::move(timer);
      }
    }

    const auto gate_path = message.payload.value("gate_path", std::string{});
    if (!gate_path.empty()) {
      auto executor = context.get_service<boost::asio::any_io_executor>();
      if (!executor) {
        co_return core::ActorResult::failed("reload_test_executor_missing",
                                            "actor I/O executor is missing");
      }
      co_await context.await_asio(
          *executor,
          [executor = *executor, gate_path]() -> boost::asio::awaitable<void> {
            boost::asio::steady_timer timer(executor);
            while (!std::filesystem::exists(gate_path)) {
              timer.expires_after(std::chrono::milliseconds(1));
              co_await timer.async_wait(boost::asio::use_awaitable);
            }
          });
    }

    const auto generation = context.config()
                                .get_value<std::string>("generation")
                                .value_or("unknown");
    write_marker(message.payload.value("completion_path", std::string{}),
                 generation);

    auto result = core::ActorResult::success();
    result.emit(events::ReloadEmission{.generation = generation,
                                       .sink_path = message.payload.value(
                                           "sink_path", std::string{})},
                message);
    co_return result;
  }

  auto handle(const events::ReloadCommand &request,
              const core::MessageEnvelope &message, core::ActorContext &context)
      -> core::ActorTask<core::ActorResult> {
    const auto &source = request.invocation.source_event;
    const auto gate_path = source.value("gate_path", std::string{});
    if (!gate_path.empty()) {
      auto executor = context.get_service<boost::asio::any_io_executor>();
      if (!executor) {
        co_return core::ActorResult::failed("reload_test_executor_missing",
                                            "actor I/O executor is missing");
      }
      co_await context.await_asio(
          *executor,
          [executor = *executor, gate_path]() -> boost::asio::awaitable<void> {
            boost::asio::steady_timer timer(executor);
            while (!std::filesystem::exists(gate_path)) {
              timer.expires_after(std::chrono::milliseconds(1));
              co_await timer.async_wait(boost::asio::use_awaitable);
            }
          });
    }

    const auto generation = context.config()
                                .get_value<std::string>("generation")
                                .value_or("unknown");
    write_marker(source.value("completion_path", std::string{}), generation);
    auto result = core::ActorResult::success();
    result.emit(events::ReloadEmission{.generation = generation,
                                       .sink_path = source.value(
                                           "sink_path", std::string{})},
                message);
    result.emit(
        command::CommandCompleted{
            .transaction_id = request.invocation.transaction_id,
            .propagation = request.invocation.arguments == "continue"
                               ? command::Propagation::Continue
                               : command::Propagation::Consume,
        },
        message);
    co_return result;
  }

  auto handle(const events::ReloadEmission &emission,
              const core::MessageEnvelope &, core::ActorContext &)
      -> core::ActorResult {
    write_marker(emission.sink_path, emission.generation);
    return core::ActorResult::success();
  }

private:
  std::mutex background_mutex_;
  std::shared_ptr<boost::asio::steady_timer> background_timer_;
};

} // namespace obcx::tests

OBCX_ACTOR_EXPORT_V2(obcx::tests::ReloadLifecycleActor)
