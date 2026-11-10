#include "evidence.hpp"
#include "core/actor/actor_generation_lifecycle.hpp"
#include "core/actor/actor_manager.hpp"
#include "core/bot/bot_operation_dispatcher.hpp"
#include "core/bot/self_identity_cache.hpp"
#include "../../src/onebot11/bot/standard_actor_events.hpp"
#include "../../src/onebot11/bot/standard_event_context.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/scope/scope_exit.hpp>
#include <dlfcn.h>
#include <iostream>

namespace core = obcx::core;
namespace bot = obcx::bot;
namespace standard = obcx::onebot11::bot::standard;
using obcx::integration::require;

int main(int argc, char **argv) {
  try {
    require(argc == 3, "expected current and legacy actor paths");
    boost::asio::io_context io;
    auto pump = [&] {
      io.restart();
      while (io.poll() != 0) {}
    };
    core::ActorManager manager;
    void *legacy = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    require(legacy != nullptr, "cannot open inert legacy fixture");
    boost::scope::scope_exit release_legacy{[&] { dlclose(legacy); }};
    auto factory_calls = reinterpret_cast<unsigned (*)()>(
        dlsym(legacy, "obcx_offline_factory_calls"));
    require(factory_calls && factory_calls() == 0, "invalid legacy counter");
    require(!manager.load_actor_from_path(argv[2]), "schema2 was accepted");
    require(manager.last_error().find("schema_version 2") != std::string::npos,
            "legacy rejection was not the schema gate: " + manager.last_error());
    require(factory_calls() == 0, "schema2 factory was called");
    std::cout << "PASS schema2 rejected before factory\n";

    require(manager.load_actor_from_path(argv[1]),
            "schema3 load failed: " + manager.last_error());
    const auto *contract = manager.get_actor_contract("offline_standard");
    require(contract && contract->schema_version == 3,
            "current contract missing or changed");
    std::cout << "PASS schema3 loaded by real ActorManager\n";

    const bot::BotInstallationRef installation{
        "offline-standard", bot::SurfaceId{"onebot11.qq"}};
    auto evidence = std::make_shared<obcx::integration::Evidence>();
    auto services = std::make_shared<core::ActorServices>();
    auto dispatcher = std::make_shared<core::BotOperationDispatcher>(
        [](const bot::SurfaceId &surface) {
          return surface == bot::SurfaceId{"onebot11.qq"};
        });
    dispatcher->seal_registrations(); // No endpoint or network transport exists.
    services->register_service<bot::BotOperationGateway>(dispatcher);
    services->register_service(evidence);
    unsigned leases = 0;
    auto lifecycle = std::make_shared<core::ActorGenerationLifecycle>([&] {
      ++leases;
      return std::shared_ptr<void>(new unsigned{1}, [&](void *p) {
        delete static_cast<unsigned *>(p);
        --leases;
      });
    });
    auto identity = std::make_shared<core::SelfIdentityCache>(
        io.get_executor(), installation, std::chrono::milliseconds{300000});
    auto source = standard::detail::StandardEventAccess::create(
        io.get_executor(), installation);
    standard::detail::StandardEventAccess::configure(
        *source, installation, identity,
        standard::ValueLimits{65536, 4096, 32, 8192});
    auto contributor = standard::detail::make_actor_service_contributor(
        installation, source);
    contributor->contribute(*services, lifecycle, io.get_executor());
    // Retire DSO-owned callbacks and drain admitted work before unloading.
    boost::scope::scope_exit cleanup{[&] {
      lifecycle->retire();
      source->close();
      identity->close();
      pump();
      services.reset();
      manager.unload_all_actors();
      dispatcher->clear_endpoints();
    }};
    core::ActorContext context{"offline_standard", services};
    require(manager.prepare_actor("offline_standard", context).ok(),
            "standard consumer preparation failed");
    const std::string frame =
        R"({"time":1700000000,"self_id":12345,"post_type":"meta_event","meta_event_type":"heartbeat","status":{"online":null,"good":true},"interval":1000})";
    auto admit = [&] {
      auto decoded = standard::detail::StandardEventAccess::receive(*source, frame);
      require(decoded.disposition ==
                  standard::detail::EventIngressDisposition::EventCompatible,
              "production event parser rejected heartbeat fixture");
      standard::detail::StandardEventAccess::admit(
          *source, std::move(decoded.document), std::nullopt);
    };

    // Queued native events cannot be reassigned to a later activation.
    admit();
    lifecycle->activate();
    pump();
    require(evidence->events == 0, "pre-activation event leaked into generation");
    admit();
    pump();
    require(evidence->events == 1 && evidence->route_rejections == 1 &&
                evidence->errors == 0 && leases == 0,
            "typed event/client routing integration failed");
    std::cout << "PASS installed ActorEvents + Client, exact fields and no-route rejection\n";

    admit(); // Captured by the old activation, not yet dispatched.
    lifecycle->invalidate();
    lifecycle->activate();
    pump();
    require(evidence->events == 1, "queued old-activation event was reassigned");
    admit();
    pump();
    require(evidence->events == 2 && evidence->route_rejections == 2,
            "reactivated subscription did not receive new event");
    std::cout << "PASS activation isolation and resumed subscription\n";

    evidence->block_next = true;
    admit();
    pump();
    require(evidence->entered_wait == 1 && leases == 1,
            "callback did not retain the generation work lease");
    lifecycle->invalidate();
    pump();
    require(evidence->cancelled_wait == 1 && leases == 0 && evidence->errors == 0,
            "retirement failed to cancel and drain in-flight callback");
    lifecycle->retire();
    lifecycle->activate();
    admit();
    pump();
    require(evidence->events == 3, "terminal retirement delivered another event");
    std::cout << "PASS in-flight cancellation, lease drain and terminal retirement\n";
    std::cout << "SCOPE offline loader/event-service/client rejection integration; "
                 "not live transport, full API matrix or full ABI proof\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
