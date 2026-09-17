#include "core/actor/reflected_actor.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <chrono>
#include <filesystem>
#include <string>

extern "C" auto obcx_private_generation_value() -> int;

namespace obcx::tests::events {
struct PrivateDependencyProbe {};

inline void from_json(const common::json &, PrivateDependencyProbe &) {}
} // namespace obcx::tests::events

namespace {

class PrivateDependencyActor final
    : public obcx::core::ReflectedActor<PrivateDependencyActor> {
public:
  static constexpr std::string_view actor_name = "private_dependency_actor";
  static constexpr std::string_view actor_version = "1.0.0";

  auto handle(const obcx::tests::events::PrivateDependencyProbe &,
              const obcx::core::MessageEnvelope &message,
              obcx::core::ActorContext &context)
      -> obcx::core::ActorTask<obcx::core::ActorResult> {
    const auto gate_path = message.payload.value("gate_path", std::string{});
    if (!gate_path.empty()) {
      auto executor = context.get_service<boost::asio::any_io_executor>();
      if (!executor) {
        co_return obcx::core::ActorResult::failed(
            "private_dependency_executor_missing",
            "private dependency probe requires an Asio executor");
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

    auto result = obcx::core::ActorResult::success();
    obcx::core::MessageEnvelope emitted;
    emitted.type = "PrivateDependencyObserved";
    emitted.causation_id = message.id;
    emitted.payload = {{"value", obcx_private_generation_value()}};
    result.emit(std::move(emitted));
    co_return result;
  }
};

} // namespace

OBCX_ACTOR_EXPORT_V2(PrivateDependencyActor)
