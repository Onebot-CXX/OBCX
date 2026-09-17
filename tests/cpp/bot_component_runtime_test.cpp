#include "core/bot/bot_component_runtime.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <ranges>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using obcx::core::BotComponent;
using obcx::core::BotComponentRuntimeError;
using obcx::core::BotInstallation;
using obcx::core::BotInstallationState;
using obcx::core::CapabilityId;
using obcx::core::CapabilityRegistry;
using obcx::core::ComponentDescriptor;
using obcx::core::ComponentId;

struct ProbeCapability {
  std::string provider;
};

struct OtherCapability {};

enum class FailurePhase : std::uint8_t {
  None,
  Install,
  Prepare,
  Start,
  Stop,
};

class ProbeComponent final : public BotComponent {
public:
  ProbeComponent(std::string id, std::vector<std::string> provides,
                 std::vector<std::string> required,
                 std::shared_ptr<std::vector<std::string>> events,
                 FailurePhase failure = FailurePhase::None,
                 std::string undeclared_capability = {})
      : id_(std::move(id)), provides_(std::move(provides)),
        required_(std::move(required)), events_(std::move(events)),
        failure_(failure),
        undeclared_capability_(std::move(undeclared_capability)) {}

  ~ProbeComponent() override {
    if (destruction_observed != nullptr) {
      *destruction_observed = true;
    }
    if (executor_to_observe != nullptr && executor_observed_alive != nullptr) {
      (void)executor_to_observe->get_executor();
      *executor_observed_alive = true;
    }
  }

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override {
    std::vector<CapabilityId> provides;
    std::vector<CapabilityId> required;
    for (const auto &id : provides_) {
      provides.emplace_back(id);
    }
    for (const auto &id : required_) {
      required.emplace_back(id);
    }
    return {.id = ComponentId{id_},
            .provides = std::move(provides),
            .required = std::move(required)};
  }

  void install_capabilities(CapabilityRegistry &registry) override {
    events_->push_back("install:" + id_);
    if (failure_ == FailurePhase::Install) {
      throw BotComponentRuntimeError("injected install failure");
    }
    for (const auto &provided : provides_) {
      registry.install(
          ComponentId{id_}, CapabilityId{provided},
          std::make_shared<ProbeCapability>(ProbeCapability{.provider = id_}));
    }
    if (!undeclared_capability_.empty()) {
      registry.install(
          ComponentId{id_}, CapabilityId{undeclared_capability_},
          std::make_shared<ProbeCapability>(ProbeCapability{.provider = id_}));
    }
  }

  void prepare(const CapabilityRegistry &registry) override {
    events_->push_back("prepare:" + id_);
    for (const auto &required : required_) {
      const auto capability =
          registry.get<ProbeCapability>(CapabilityId{required});
      if (capability == nullptr) {
        throw BotComponentRuntimeError("missing probe capability");
      }
    }
    if (failure_ == FailurePhase::Prepare) {
      throw BotComponentRuntimeError("injected prepare failure");
    }
  }

  void start() override {
    events_->push_back("start:" + id_);
    if (failure_ == FailurePhase::Start) {
      throw BotComponentRuntimeError("injected start failure");
    }
  }

  void stop() override {
    events_->push_back("stop:" + id_);
    ++stop_calls;
    if (failure_ == FailurePhase::Stop) {
      throw BotComponentRuntimeError("injected stop failure");
    }
  }

  std::atomic_size_t stop_calls{};
  bool *destruction_observed{};
  boost::asio::io_context *executor_to_observe{};
  bool *executor_observed_alive{};

private:
  std::string id_;
  std::vector<std::string> provides_;
  std::vector<std::string> required_;
  std::shared_ptr<std::vector<std::string>> events_;
  FailurePhase failure_;
  std::string undeclared_capability_;
};

class CancellableOperation {
public:
  explicit CancellableOperation(boost::asio::io_context &io)
      : timer_(io, std::chrono::steady_clock::time_point::max()) {}

  auto execute() -> boost::asio::awaitable<bool> {
    entered = true;
    boost::system::error_code error;
    co_await timer_.async_wait(
        boost::asio::redirect_error(boost::asio::use_awaitable, error));
    co_return error == boost::asio::error::operation_aborted;
  }

  void cancel() { timer_.cancel(); }
  bool entered{};

private:
  boost::asio::steady_timer timer_;
};

class CancellableOperationComponent final : public BotComponent {
public:
  explicit CancellableOperationComponent(boost::asio::io_context &io)
      : operation_(std::make_shared<CancellableOperation>(io)) {}

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override {
    return {.id = ComponentId{"cancellable-operation"},
            .provides = {CapabilityId{"cancellable.operation"}},
            .required = {}};
  }
  void install_capabilities(CapabilityRegistry &registry) override {
    registry.install(ComponentId{"cancellable-operation"},
                     CapabilityId{"cancellable.operation"}, operation_);
  }
  void prepare(const CapabilityRegistry &) override {}
  void start() override {}
  void stop() override { operation_->cancel(); }

private:
  std::shared_ptr<CancellableOperation> operation_;
};

class BlockingStopComponent final : public BotComponent {
public:
  BlockingStopComponent(std::shared_ptr<std::promise<void>> entered,
                        std::shared_future<void> release)
      : entered_(std::move(entered)), release_(std::move(release)) {}

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override {
    return {.id = ComponentId{"blocking-stop"}, .provides = {}, .required = {}};
  }
  void install_capabilities(CapabilityRegistry &) override {}
  void prepare(const CapabilityRegistry &) override {}
  void start() override {}
  void stop() override {
    entered_->set_value();
    release_.wait();
  }

private:
  std::shared_ptr<std::promise<void>> entered_;
  std::shared_future<void> release_;
};

auto installation() -> BotInstallation {
  return BotInstallation{"test-installation",
                         obcx::bot::SurfaceId{"onebot11.qq"}};
}

TEST(BotComponentRuntimeTest, StableIdsRejectInvalidValues) {
  EXPECT_THROW((void)ComponentId{""}, BotComponentRuntimeError);
  EXPECT_THROW((void)ComponentId{"Uppercase"}, BotComponentRuntimeError);
  EXPECT_THROW((void)CapabilityId{"contains space"}, BotComponentRuntimeError);
  EXPECT_NO_THROW((void)CapabilityId{"onebot11.transport.websocket"});
}

TEST(BotComponentRuntimeTest,
     DescriptorValidationUsesStableRecipeOrderForIndependentComponents) {
  const std::vector<ComponentDescriptor> descriptors = {
      {.id = ComponentId{"protocol"},
       .provides = {CapabilityId{"protocol.api"}},
       .required = {}},
      {.id = ComponentId{"metrics"},
       .provides = {CapabilityId{"metrics.api"}},
       .required = {}},
      {.id = ComponentId{"operations"},
       .provides = {CapabilityId{"operations.api"}},
       .required = {CapabilityId{"protocol.api"}}},
  };
  const auto first = obcx::core::validate_component_recipe(descriptors);
  const auto second = obcx::core::validate_component_recipe(descriptors);
  EXPECT_EQ(first.lifecycle_order, (std::vector<std::size_t>{0, 1, 2}));
  EXPECT_EQ(second.lifecycle_order, first.lifecycle_order);
}

TEST(BotComponentRuntimeTest, DescriptorValidationRejectsInvalidGraphs) {
  EXPECT_THROW((void)obcx::core::validate_component_recipe(
                   {{.id = ComponentId{"consumer"},
                     .provides = {},
                     .required = {CapabilityId{"missing"}}}}),
               BotComponentRuntimeError);
  EXPECT_THROW((void)obcx::core::validate_component_recipe(
                   {{.id = ComponentId{"a"},
                     .provides = {CapabilityId{"cap.a"}},
                     .required = {CapabilityId{"cap.b"}}},
                    {.id = ComponentId{"b"},
                     .provides = {CapabilityId{"cap.b"}},
                     .required = {CapabilityId{"cap.a"}}}}),
               BotComponentRuntimeError);
  EXPECT_THROW((void)obcx::core::validate_component_recipe(
                   {{.id = ComponentId{"a"},
                     .provides = {CapabilityId{"same"}},
                     .required = {}},
                    {.id = ComponentId{"b"},
                     .provides = {CapabilityId{"same"}},
                     .required = {}}}),
               BotComponentRuntimeError);
}

TEST(BotComponentRuntimeTest,
     AssemblyPublishesTypedCapabilitiesAndLifecycleIsTopological) {
  auto events = std::make_shared<std::vector<std::string>>();
  auto runtime = installation();
  runtime.add_component(std::make_unique<ProbeComponent>(
      "operations", std::vector<std::string>{"operations.api"},
      std::vector<std::string>{"transport.api"}, events));
  runtime.add_component(std::make_unique<ProbeComponent>(
      "protocol", std::vector<std::string>{"protocol.api"},
      std::vector<std::string>{}, events));
  runtime.add_component(std::make_unique<ProbeComponent>(
      "transport", std::vector<std::string>{"transport.api"},
      std::vector<std::string>{"protocol.api"}, events));

  runtime.start();
  EXPECT_EQ(runtime.lifecycle_order(),
            (std::vector<std::string>{"protocol", "transport", "operations"}));
  EXPECT_TRUE(runtime.accepting_work());
  EXPECT_EQ(runtime.state(), BotInstallationState::Running);
  EXPECT_EQ(runtime.capability<ProbeCapability>(CapabilityId{"transport.api"})
                ->provider,
            "transport");
  EXPECT_THROW(
      (void)runtime.capability<OtherCapability>(CapabilityId{"transport.api"}),
      BotComponentRuntimeError);

  runtime.stop();
  EXPECT_FALSE(runtime.accepting_work());
  EXPECT_EQ(runtime.state(), BotInstallationState::Stopped);
  EXPECT_EQ(events->at(events->size() - 3), "stop:operations");
  EXPECT_EQ(events->at(events->size() - 2), "stop:transport");
  EXPECT_EQ(events->back(), "stop:protocol");
}

TEST(BotComponentRuntimeTest, AssemblyRejectsUndeclaredCapabilities) {
  auto events = std::make_shared<std::vector<std::string>>();
  auto runtime = installation();
  runtime.add_component(std::make_unique<ProbeComponent>(
      "provider", std::vector<std::string>{"declared"},
      std::vector<std::string>{}, events, FailurePhase::None, "undeclared"));
  EXPECT_THROW(runtime.assemble(), BotComponentRuntimeError);
  EXPECT_EQ(runtime.state(), BotInstallationState::Failed);
}

TEST(BotComponentRuntimeTest, PrepareFailureRollsBackInReverseOrder) {
  auto events = std::make_shared<std::vector<std::string>>();
  auto runtime = installation();
  auto first = std::make_unique<ProbeComponent>(
      "first", std::vector<std::string>{"first.api"},
      std::vector<std::string>{}, events);
  auto *first_observed = first.get();
  auto failing = std::make_unique<ProbeComponent>(
      "failing", std::vector<std::string>{"failing.api"},
      std::vector<std::string>{"first.api"}, events, FailurePhase::Prepare);
  auto *failing_observed = failing.get();
  runtime.add_component(std::move(first));
  runtime.add_component(std::move(failing));

  EXPECT_THROW(runtime.start(), BotComponentRuntimeError);
  EXPECT_EQ(runtime.state(), BotInstallationState::Failed);
  EXPECT_EQ(first_observed->stop_calls.load(), 1U);
  EXPECT_EQ(failing_observed->stop_calls.load(), 1U);
  EXPECT_EQ(events->at(events->size() - 2), "stop:failing");
  EXPECT_EQ(events->back(), "stop:first");
}

TEST(BotComponentRuntimeTest, StartFailureStopsEveryPreparedComponentOnce) {
  auto events = std::make_shared<std::vector<std::string>>();
  auto runtime = installation();
  std::vector<ProbeComponent *> observed;
  for (const auto &[id, failure] :
       std::vector<std::pair<std::string, FailurePhase>>{
           {"first", FailurePhase::None},
           {"second", FailurePhase::Start},
           {"third", FailurePhase::None}}) {
    auto component = std::make_unique<ProbeComponent>(
        id, std::vector<std::string>{id + ".api"}, std::vector<std::string>{},
        events, failure);
    observed.push_back(component.get());
    runtime.add_component(std::move(component));
  }
  EXPECT_THROW(runtime.start(), BotComponentRuntimeError);
  for (const auto *component : observed) {
    EXPECT_EQ(component->stop_calls.load(), 1U);
  }
  EXPECT_EQ(events->at(events->size() - 3), "stop:third");
  EXPECT_EQ(events->at(events->size() - 2), "stop:second");
  EXPECT_EQ(events->back(), "stop:first");
}

TEST(BotComponentRuntimeTest, ConcurrentRepeatedStopIsIdempotent) {
  auto events = std::make_shared<std::vector<std::string>>();
  auto runtime = installation();
  auto component = std::make_unique<ProbeComponent>(
      "single", std::vector<std::string>{"single.api"},
      std::vector<std::string>{}, events);
  auto *observed = component.get();
  runtime.add_component(std::move(component));
  runtime.start();

  std::thread first([&runtime] { runtime.stop(); });
  std::thread second([&runtime] { runtime.stop(); });
  first.join();
  second.join();
  runtime.stop();
  EXPECT_EQ(observed->stop_calls.load(), 1U);
  EXPECT_EQ(runtime.state(), BotInstallationState::Stopped);
}

TEST(BotComponentRuntimeTest, ConcurrentStopDoesNotHaltCancellationDrainOwner) {
  auto runtime = installation();
  runtime.add_component(
      std::make_unique<CancellableOperationComponent>(runtime.executor()));
  auto stop_entered = std::make_shared<std::promise<void>>();
  auto stop_entered_future = stop_entered->get_future();
  std::promise<void> release_stop;
  runtime.add_component(std::make_unique<BlockingStopComponent>(
      stop_entered, release_stop.get_future().share()));
  runtime.start();

  const auto operation = runtime.capability<CancellableOperation>(
      CapabilityId{"cancellable.operation"});
  auto operation_future = boost::asio::co_spawn(
      runtime.executor(), operation->execute(), boost::asio::use_future);
  EXPECT_EQ(runtime.executor().poll_one(), 1U);
  ASSERT_TRUE(operation->entered);

  std::thread drain_owner([&runtime] { runtime.stop(); });
  if (stop_entered_future.wait_for(std::chrono::seconds{1}) !=
      std::future_status::ready) {
    release_stop.set_value();
    drain_owner.join();
    FAIL() << "stop owner did not enter the blocking component";
    return;
  }
  EXPECT_EQ(runtime.state(), BotInstallationState::Stopping);

  std::thread concurrent([&runtime] { runtime.stop(); });
  concurrent.join();
  EXPECT_FALSE(runtime.executor().stopped());

  release_stop.set_value();
  drain_owner.join();
  ASSERT_EQ(operation_future.wait_for(std::chrono::seconds{1}),
            std::future_status::ready);
  EXPECT_TRUE(operation_future.get());
  EXPECT_EQ(runtime.state(), BotInstallationState::Stopped);
}

TEST(BotComponentRuntimeTest, StopDoesNotAbandonCleanupQueuedByRunningHandler) {
  auto runtime = installation();
  runtime.start();
  boost::asio::steady_timer timer(runtime.executor());
  std::promise<void> entered;
  auto entered_future = entered.get_future();
  std::promise<void> release;
  auto release_future = release.get_future();
  std::promise<void> retired;
  auto retired_future = retired.get_future();
  boost::asio::post(runtime.executor(), [&] {
    entered.set_value();
    release_future.wait();
    timer.expires_after(std::chrono::milliseconds{10});
    timer.async_wait(
        [&](const boost::system::error_code &) { retired.set_value(); });
  });
  std::thread runner([&] { runtime.run(); });
  entered_future.wait();
  runtime.stop();
  EXPECT_FALSE(runtime.executor().stopped());
  release.set_value();
  runner.join();
  EXPECT_EQ(retired_future.wait_for(std::chrono::seconds{1}),
            std::future_status::ready);
}

TEST(BotComponentRuntimeTest, DestructorDrainsBeforeReleasingComponents) {
  class DeferredStopComponent final : public BotComponent {
  public:
    DeferredStopComponent(boost::asio::io_context &io, bool &safe)
        : timer_(io), safe_(safe) {}
    ~DeferredStopComponent() override { safe_ = retired_; }
    auto descriptor() const -> ComponentDescriptor override {
      return {
          .id = ComponentId{"deferred-stop"}, .provides = {}, .required = {}};
    }
    void install_capabilities(CapabilityRegistry &) override {}
    void prepare(const CapabilityRegistry &) override {}
    void start() override {}
    void stop() override {
      timer_.expires_after(std::chrono::milliseconds{10});
      timer_.async_wait(
          [this](const boost::system::error_code &) { retired_ = true; });
    }

  private:
    boost::asio::steady_timer timer_;
    bool &safe_;
    bool retired_ = false;
  };
  bool safe = false;
  {
    auto runtime = installation();
    runtime.add_component(
        std::make_unique<DeferredStopComponent>(runtime.executor(), safe));
    runtime.start();
    runtime.executor().poll(); // An already-idle context must also be drained.
  }
  EXPECT_TRUE(safe);
}

TEST(BotComponentRuntimeTest, CapabilitiesAreInstallationScoped) {
  auto events = std::make_shared<std::vector<std::string>>();
  auto first = installation();
  first.add_component(std::make_unique<ProbeComponent>(
      "provider", std::vector<std::string>{"private.api"},
      std::vector<std::string>{}, events));
  first.assemble();

  BotInstallation second{"other-installation",
                         obcx::bot::SurfaceId{"onebot11.qq"}};
  second.assemble();
  EXPECT_EQ(
      first.capability<ProbeCapability>(CapabilityId{"private.api"})->provider,
      "provider");
  EXPECT_THROW(
      (void)second.capability<ProbeCapability>(CapabilityId{"private.api"}),
      BotComponentRuntimeError);
}

TEST(BotComponentRuntimeTest,
     ShutdownCancelsAndCompletesAnInFlightInstallationOperation) {
  auto runtime = installation();
  runtime.add_component(
      std::make_unique<CancellableOperationComponent>(runtime.executor()));
  runtime.start();
  const auto operation = runtime.capability<CancellableOperation>(
      CapabilityId{"cancellable.operation"});
  auto future = boost::asio::co_spawn(runtime.executor(), operation->execute(),
                                      boost::asio::use_future);
  EXPECT_EQ(runtime.executor().poll_one(), 1U);
  EXPECT_TRUE(operation->entered);
  EXPECT_EQ(future.wait_for(std::chrono::milliseconds{0}),
            std::future_status::timeout);

  runtime.stop();
  EXPECT_EQ(future.wait_for(std::chrono::seconds{1}),
            std::future_status::ready);
  EXPECT_TRUE(future.get());
}

TEST(BotComponentRuntimeTest,
     ComponentsAreDestroyedBeforeInstallationExecutor) {
  auto events = std::make_shared<std::vector<std::string>>();
  bool destroyed = false;
  bool executor_alive_during_destruction = false;
  {
    auto runtime = std::make_unique<BotInstallation>(
        "destruction-test", obcx::bot::SurfaceId{"telegram.bot_api"});
    auto component = std::make_unique<ProbeComponent>(
        "single", std::vector<std::string>{"single.api"},
        std::vector<std::string>{}, events, FailurePhase::Stop);
    component->destruction_observed = &destroyed;
    component->executor_to_observe = &runtime->executor();
    component->executor_observed_alive = &executor_alive_during_destruction;
    runtime->add_component(std::move(component));
    runtime->start();
  }
  EXPECT_TRUE(destroyed);
  EXPECT_TRUE(executor_alive_during_destruction);
}

} // namespace
