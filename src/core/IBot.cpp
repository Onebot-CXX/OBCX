#include "../../include/interface/IBot.hpp"

#include "common/Logger.hpp"
#include "core/EventDispatcher.hpp"
#include "core/TaskScheduler.hpp"
#include "onebot11/adapter/ProtocolAdapter.hpp"

#include <boost/asio/io_context.hpp>

namespace obcx::core {

IBot::IBot(std::unique_ptr<adapter::BaseProtocolAdapter> adapter)
    : io_context_(std::make_shared<asio::io_context>()),
      adapter_{std::move(adapter)},
      dispatcher_{std::make_unique<EventDispatcher>(*io_context_)},
      task_scheduler_{std::make_unique<TaskScheduler>()},
      connection_manager_{nullptr} {}

IBot::~IBot() {
  if (task_scheduler_) {
    task_scheduler_->stop();
  }
}

} // namespace obcx::core
