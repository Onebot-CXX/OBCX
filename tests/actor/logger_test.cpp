#include "common/logger.hpp"

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>

#include <array>
#include <barrier>
#include <exception>
#include <sstream>
#include <thread>
#include <vector>

extern "C" void log_actor_a();
extern "C" void log_actor_b();

TEST(LoggerTest, ConcurrentInitializationAndSourceOwnershipShareHostPolicy) {
  constexpr std::size_t workers = 6;
  std::barrier start{workers};
  std::array<std::shared_ptr<spdlog::logger>, workers> first;
  std::array<std::exception_ptr, workers> errors;
  std::vector<std::jthread> threads;
  for (std::size_t i = 0; i < workers; ++i) {
    threads.emplace_back([&, i] {
      start.arrive_and_wait();
      try {
        obcx::common::Logger::initialize(spdlog::level::info, "", false);
        first[i] = obcx::common::Logger::get("concurrent_first");
      } catch (...) {
        errors[i] = std::current_exception();
      }
    });
  }
  threads.clear();
  for (std::size_t i = 0; i < workers; ++i) {
    ASSERT_EQ(errors[i], nullptr);
    ASSERT_EQ(first[i], first.front());
  }
  auto core = obcx::common::Logger::get();
  EXPECT_EQ(core->name(), "core");
  EXPECT_EQ(first.front()->sinks(), core->sinks());

  std::ostringstream output;
  auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output);
  sink->set_pattern("[%n] [%l] %v");
  core->sinks().push_back(sink);
  obcx::common::Logger::set_level(spdlog::level::info);
  for (std::size_t i = 0; i < workers; ++i) {
    threads.emplace_back([] {
      log_actor_a();
      log_actor_b();
      OBCX_INFO("host ownership probe");
    });
  }
  threads.clear();
  obcx::common::Logger::flush();
  EXPECT_NE(output.str().find("[logging_actor_a] [info]"), std::string::npos);
  EXPECT_NE(output.str().find("[logging_actor_b] [info]"), std::string::npos);
  EXPECT_NE(output.str().find("[core] [info] host ownership probe"),
            std::string::npos);
  EXPECT_EQ(output.str().find("actor debug probe"), std::string::npos);
  for (const auto *name : {"logging_actor_a", "logging_actor_b"}) {
    const auto actor = obcx::common::Logger::get(name);
    EXPECT_EQ(actor->sinks(), core->sinks());
    EXPECT_EQ(actor->flush_level(), core->flush_level());
  }

  obcx::common::Logger::set_level(spdlog::level::debug);
  log_actor_a();
  log_actor_b();
  OBCX_DEBUG("host debug probe");
  auto late = obcx::common::Logger::get("created_after_level_change");
  late->debug("late debug probe");
  obcx::common::Logger::flush();
  EXPECT_NE(output.str().find("[logging_actor_a] [debug]"), std::string::npos);
  EXPECT_NE(output.str().find("[logging_actor_b] [debug]"), std::string::npos);
  EXPECT_NE(output.str().find("[core] [debug] host debug probe"),
            std::string::npos);
  EXPECT_NE(output.str().find(
                "[created_after_level_change] [debug] late debug probe"),
            std::string::npos);

  // Remove the stack-backed test stream from every logger before returning.
  for (const auto *name : {"core", "logging_actor_a", "logging_actor_b",
                           "created_after_level_change"}) {
    auto logger = obcx::common::Logger::get(name);
    std::erase(logger->sinks(), sink);
  }
  obcx::common::Logger::set_level(spdlog::level::info);
}
