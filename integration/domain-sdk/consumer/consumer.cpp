#include "../evidence.hpp"
#include "core/actor/actor_messages.hpp"
#include "core/actor/reflected_actor.hpp"
#include "onebot11/bot/standard_actor_events.hpp"
#include "onebot11/bot/standard_client.hpp"
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace obcx::integration {
namespace standard = obcx::onebot11::bot::standard;
class StandardConsumer final : public core::ReflectedActor<StandardConsumer> {
public:
  auto prepare_generation(core::ActorContext &context)
      -> core::ActorPreparationResult {
    auto events = context.get_service<standard::ActorEvents>();
    auto gateway = context.get_service<bot::BotOperationGateway>();
    auto evidence = context.get_service<Evidence>();
    if (!events || !gateway || !evidence)
      return core::ActorPreparationResult::failed("offline services missing");
    const bot::BotInstallationRef installation{"offline-standard",
                                               bot::SurfaceId{"onebot11.qq"}};
    subscription_ = events->subscribe(
        installation,
        [gateway, evidence, installation](
            std::shared_ptr<const standard::StandardEventV1> event)
            -> boost::asio::awaitable<void> {
          try {
            require(event->installation() == installation,
                    "standard event changed installation");
            const auto *heartbeat =
                std::get_if<standard::HeartbeatObservation>(&event->observation());
            require(heartbeat && heartbeat->header.self_id == 12345 &&
                        heartbeat->header.time == 1700000000 &&
                        heartbeat->interval == 1000 && heartbeat->status.good &&
                        !heartbeat->status.online.has_value() &&
                        !event->action_handle(),
                    "standard heartbeat fields or authority changed");
            ++evidence->events;
            if (evidence->block_next.exchange(false)) {
              ++evidence->entered_wait;
              boost::asio::steady_timer timer(
                  co_await boost::asio::this_coro::executor);
              timer.expires_after(std::chrono::seconds{10});
              try {
                co_await timer.async_wait(boost::asio::use_awaitable);
                ++evidence->errors;
              } catch (const boost::system::system_error &error) {
                if (error.code() == boost::asio::error::operation_aborted)
                  ++evidence->cancelled_wait;
                else
                  ++evidence->errors;
              }
              co_return;
            }
            // Exercise the real installed standard Client and the real process
            // dispatcher. No endpoint is installed: rejection must precede I/O.
            standard::Client client{*gateway};
            auto result = co_await client.get_status(standard::GetStatusRequest{
                installation, standard::GetStatusParameters{},
                standard::CallControl{
                    standard::ExecutionMode::Direct,
                    standard::CallLimits{4096, 65536, std::chrono::milliseconds{500}},
                    std::nullopt},
                standard::ValueLimits{65536, 4096, 32, 8192}});
            require(!result.ok() && result.error &&
                        result.error->code == bot::BotOperationErrorCode::RouteNotFound &&
                        result.error->submission_safety ==
                            bot::SubmissionSafety::DefinitelyNotSubmitted,
                    "standard Client did not retain route rejection");
            ++evidence->route_rejections;
          } catch (...) {
            ++evidence->errors;
          }
        });
    return core::ActorPreparationResult::ready();
  }

  auto handle(const core::events::RawNoticeEvent &,
              const core::MessageEnvelope &, core::ActorContext &)
      -> core::ActorResult {
    return core::ActorResult::success();
  }

private:
  standard::ActorEvents::Subscription subscription_;
};
} // namespace obcx::integration

OBCX_ACTOR_EXPORT_V2(obcx::integration::StandardConsumer)
