#include "interfaces/bot.hpp"
#include "core/event_dispatcher.hpp"
#include "core/task_scheduler.hpp"

#include <boost/asio/io_context.hpp>
#include <chrono>
#include <thread>

namespace obcx::core {

IBot::IBot(std::unique_ptr<adapter::BaseProtocolAdapter> adapter,
           std::shared_ptr<TaskScheduler> task_scheduler)
    : io_context_(std::make_shared<asio::io_context>()),
      adapter_{std::move(adapter)},
      dispatcher_{std::make_unique<EventDispatcher>(*io_context_)},
      task_scheduler_{task_scheduler ? std::move(task_scheduler)
                                     : std::make_shared<TaskScheduler>()},
      connection_manager_{nullptr} {}

IBot::~IBot() {
  // 确保所有组件正确停止和清理
  if (connection_manager_) {
    connection_manager_->disconnect();
    connection_manager_.reset();
  }

  if (task_scheduler_ && task_scheduler_.use_count() == 1) {
    // Only stop if this is the last owner (solo mode)
    task_scheduler_->stop();
  }
  task_scheduler_.reset();

  if (dispatcher_) {
    dispatcher_.reset();
  }

  // 停止io_context并清理所有挂起的操作
  if (io_context_) {
    io_context_->stop();
    // 等待一小段时间让操作完成
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    io_context_.reset();
  }
}

} // namespace obcx::core
