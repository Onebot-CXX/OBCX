#include "interfaces/bot.hpp"
#include "core/event_dispatcher.hpp"
#include "core/task_scheduler.hpp"

#include <boost/asio/io_context.hpp>

namespace obcx::core {

IBot::IBot(std::unique_ptr<adapter::BaseProtocolAdapter> adapter,
           std::shared_ptr<TaskScheduler> task_scheduler)
    : io_context_(std::make_shared<asio::io_context>()),
      adapter_{std::move(adapter)},
      task_scheduler_{task_scheduler ? std::move(task_scheduler)
                                     : std::make_shared<TaskScheduler>()},
      dispatcher_{
          std::make_unique<EventDispatcher>(task_scheduler_->get_io_context())},
      connection_manager_{nullptr} {}

IBot::~IBot() {
  // Disconnect first (cancels timers) while io_context is still alive.
  if (connection_manager_) {
    connection_manager_->disconnect();
  }

  if (task_scheduler_ && task_scheduler_.use_count() == 1) {
    // Only stop if this is the last owner (solo mode)
    task_scheduler_->stop();
  }
  task_scheduler_.reset();

  if (dispatcher_) {
    dispatcher_.reset();
  }

  // Destroy io_context before connection_manager_: the io_context destructor
  // tears down any pending coroutine frames, and those frames capture 'this'
  // pointers into objects owned by connection_manager_ (e.g. ProxyHttpClient).
  // Keeping connection_manager_ alive here prevents use-after-free inside the
  // coroutine frame destructors.
  if (io_context_) {
    io_context_->stop();
    io_context_.reset();
  }

  // Safe to destroy connection_manager_ now that all coroutine frames are gone.
  connection_manager_.reset();
}

} // namespace obcx::core
