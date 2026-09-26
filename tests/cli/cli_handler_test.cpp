#include "common/cli_handler.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace obcx::common {
namespace {

TEST(CliHandlerTest, TuiReloadUsesAsynchronousContextCallback) {
  std::atomic_bool should_stop = false;
  std::condition_variable stop_cv;
  std::vector<std::string> output;
  std::size_t requests = 0;
  CliHandler handler(CliHandler::Context{
      .should_stop = should_stop,
      .stop_cv = stop_cv,
      .output_cb = [&](const std::string &line) { output.push_back(line); },
      .reload_cb =
          [&] {
            ++requests;
            return CliHandler::ReloadRequestStatus::Accepted;
          },
  });

  EXPECT_TRUE(handler.process_command("reload"));
  EXPECT_EQ(requests, 1);
  ASSERT_EQ(output.size(), 1);
  EXPECT_EQ(output.front(),
            "ACTOR RELOAD STARTED: wait for the highlighted ACTOR RELOAD "
            "SUCCEEDED/FAILED result");
  EXPECT_FALSE(should_stop.load());
}

TEST(CliHandlerTest, NoTuiReloadUsesTheSameContextCallback) {
  std::atomic_bool should_stop = false;
  std::condition_variable stop_cv;
  std::size_t requests = 0;
  CliHandler handler(CliHandler::Context{
      .should_stop = should_stop,
      .stop_cv = stop_cv,
      .reload_cb =
          [&] {
            ++requests;
            return CliHandler::ReloadRequestStatus::Accepted;
          },
  });

  testing::internal::CaptureStdout();
  EXPECT_TRUE(handler.process_command("reload"));
  const auto output = testing::internal::GetCapturedStdout();
  EXPECT_EQ(requests, 1);
  EXPECT_EQ(output,
            "ACTOR RELOAD STARTED: wait for the highlighted ACTOR RELOAD "
            "SUCCEEDED/FAILED result\n");
}

TEST(CliHandlerTest, BusyReloadIsRejectedImmediately) {
  std::atomic_bool should_stop = false;
  std::condition_variable stop_cv;
  std::vector<std::string> output;
  CliHandler handler(CliHandler::Context{
      .should_stop = should_stop,
      .stop_cv = stop_cv,
      .output_cb = [&](const std::string &line) { output.push_back(line); },
      .reload_cb = [] { return CliHandler::ReloadRequestStatus::Busy; },
  });

  EXPECT_TRUE(handler.process_command("reload"));
  ASSERT_EQ(output.size(), 1);
  EXPECT_EQ(output.front(),
            "reload_busy: an actor runtime reload is already running");
}

TEST(CliHandlerTest, ReloadWithoutRuntimeReportsUnavailable) {
  std::atomic_bool should_stop = false;
  std::condition_variable stop_cv;
  std::vector<std::string> output;
  CliHandler handler(CliHandler::Context{
      .should_stop = should_stop,
      .stop_cv = stop_cv,
      .output_cb = [&](const std::string &line) { output.push_back(line); },
  });

  EXPECT_TRUE(handler.process_command("reload"));
  ASSERT_EQ(output.size(), 1);
  EXPECT_EQ(output.front(),
            "reload_unavailable: actor runtime reload is unavailable");
}

TEST(CliHandlerTest, RunObservesStopWithoutWaitingForEnter) {
  using namespace std::chrono_literals;

  const int saved_stdin = ::dup(STDIN_FILENO);
  ASSERT_GE(saved_stdin, 0);
  int input_pipe[2] = {-1, -1};
  ASSERT_EQ(::pipe(input_pipe), 0);
  ASSERT_GE(::dup2(input_pipe[0], STDIN_FILENO), 0);
  ::close(input_pipe[0]);

  std::atomic_bool should_stop = false;
  std::condition_variable stop_cv;
  CliHandler handler(CliHandler::Context{
      .should_stop = should_stop,
      .stop_cv = stop_cv,
  });
  auto run = std::async(std::launch::async, [&handler] { handler.run(); });

  std::this_thread::sleep_for(20ms);
  should_stop.store(true, std::memory_order_release);
  stop_cv.notify_one();
  const auto stopped_without_input = run.wait_for(500ms);

  if (stopped_without_input != std::future_status::ready) {
    ::close(input_pipe[1]);
  }
  run.wait();
  if (stopped_without_input == std::future_status::ready) {
    ::close(input_pipe[1]);
  }
  ASSERT_GE(::dup2(saved_stdin, STDIN_FILENO), 0);
  ::close(saved_stdin);

  EXPECT_EQ(stopped_without_input, std::future_status::ready);
}

} // namespace
} // namespace obcx::common
