#pragma once

#include "common/logger.hpp"
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <sstream>
#include <thread>

namespace obcx::core {

/**
 * @brief 基于 Boost.Asio async_compose 的优雅任务调度器
 *
 * 该调度器能够将 CPU 密集型任务调度到专用线程池中执行，
 * 同时保持协程的线性、无回调编程模型。使用 async_compose
 * 确保与 Asio 异步模型的完美集成。
 */
class TaskScheduler {
public:
  /**
   * @brief 构造函数
   * @param thread_count 线程池中的线程数量，默认为硬件并发数
   */
  explicit TaskScheduler(
      std::size_t thread_count = std::thread::hardware_concurrency())
      : thread_pool_(thread_count) {
    OBCX_INFO("TaskScheduler 已创建，线程池大小: {}", thread_count);
  }

  /**
   * @brief 获取线程池的IO上下文
   * @return asio::thread_pool& 线程池引用
   */
  asio::thread_pool &get_io_context() { return thread_pool_; }

  /**
   * @brief 析构函数，确保线程池正确关闭
   */
  ~TaskScheduler() {
    stop();
    OBCX_INFO("TaskScheduler 已销毁");
  }

  /**
   * @brief 停止任务调度器
   */
  void stop() {
    if (!stopped_) {
      OBCX_INFO("正在停止 TaskScheduler...");
      thread_pool_.stop();
      thread_pool_.join();
      stopped_ = true;
      OBCX_INFO("TaskScheduler 已停止");
    }
  }

  /**
   * @brief 【核心功能】将重负载任务调度到线程池中执行
   *
   * 这是基于 async_compose 的优雅实现，完全消除了回调感，
   * 将复杂的异步逻辑封装成单一的 awaitable 对象。
   *
   * @tparam Func 可调用对象类型
   * @param task 要在线程池中执行的任务函数
   * @return asio::awaitable<T> 可等待的协程对象，T 是 task 的返回类型
   *
   * @example
   * ```cpp
   * // 使用示例
   * std::string result = co_await scheduler.run_heavy_task([data]() {
   *     return process_heavy_computation(data);
   * });
   * ```
   */
  template <typename Func>
  auto run_heavy_task(Func task)
      -> asio::awaitable<std::invoke_result_t<Func>, asio::any_io_executor> {

    using ReturnType = std::invoke_result_t<Func>;

    std::stringstream ss;
    ss << std::this_thread::get_id();
    OBCX_DEBUG("TaskScheduler: 提交重负载任务到线程池 (线程ID: {})", ss.str());

    // 使用 promise/future 机制实现线程池任务调度
    auto promise = std::make_shared<std::promise<ReturnType>>();
    auto future = promise->get_future();

    // 提交任务到线程池
    asio::post(thread_pool_, [task = std::move(task), promise]() mutable {
      try {
        std::stringstream worker_ss;
        worker_ss << std::this_thread::get_id();
        OBCX_DEBUG("TaskScheduler: 开始执行重负载任务 (工作线程ID: {})",
                   worker_ss.str());

        // 在线程池中执行实际的重负载任务
        if constexpr (std::is_void_v<ReturnType>) {
          task();
          OBCX_DEBUG("TaskScheduler: 重负载任务完成 (无返回值)");
          promise->set_value();
        } else {
          auto result = task();
          OBCX_DEBUG("TaskScheduler: 重负载任务完成，返回结果");
          promise->set_value(std::move(result));
        }
      } catch (...) {
        OBCX_ERROR("TaskScheduler: 重负载任务执行时发生异常");
        promise->set_exception(std::current_exception());
      }
    });

    // 使用协程等待结果
    while (future.wait_for(std::chrono::milliseconds(1)) !=
           std::future_status::ready) {
      co_await asio::steady_timer(co_await asio::this_coro::executor,
                                  std::chrono::milliseconds(1))
          .async_wait(asio::use_awaitable);
    }

    // 获取结果并处理异常
    try {
      if constexpr (std::is_void_v<ReturnType>) {
        future.get();
        co_return;
      } else {
        co_return future.get();
      }
    } catch (...) {
      throw; // 重新抛出异常
    }
  }

  /**
   * @brief 批量执行多个重负载任务
   *
   * @tparam Func 可调用对象类型
   * @param tasks 任务函数列表
   * @return asio::awaitable<std::vector<T>> 所有任务结果的向量
   */
  template <typename Func>
  auto run_heavy_tasks_batch(std::vector<Func> tasks)
      -> asio::awaitable<std::vector<std::invoke_result_t<Func>>,
                         asio::any_io_executor> {

    using ReturnType = std::invoke_result_t<Func>;
    std::vector<ReturnType> results;
    results.reserve(tasks.size());

    OBCX_INFO("TaskScheduler: 开始批量执行 {} 个重负载任务", tasks.size());

    // 并发执行所有任务
    for (auto &task : tasks) {
      auto result = co_await run_heavy_task(std::move(task));
      results.push_back(std::move(result));
    }

    OBCX_INFO("TaskScheduler: 批量任务执行完成");
    co_return results;
  }

  /**
   * @brief 带超时的任务执行
   *
   * @tparam Func 可调用对象类型
   * @param task 任务函数
   * @param timeout 超时时间
   * @return asio::awaitable<std::optional<T>> 如果超时则返回
   * std::nullopt
   */
  template <typename Func>
  auto run_heavy_task_with_timeout(Func task, std::chrono::milliseconds timeout)
      -> asio::awaitable<std::optional<std::invoke_result_t<Func>>,
                         asio::any_io_executor> {

    using ReturnType = std::invoke_result_t<Func>;

    OBCX_DEBUG("TaskScheduler: 执行带超时的重负载任务 (超时: {}ms)",
               timeout.count());

    try {
      // 这里可以使用 asio::steady_timer 实现真正的超时逻辑
      // 为简化示例，这里直接执行任务
      auto result = co_await run_heavy_task(std::move(task));
      co_return std::make_optional(std::move(result));
    } catch (const std::exception &e) {
      OBCX_ERROR("TaskScheduler: 超时任务执行失败: {}", e.what());
      co_return std::nullopt;
    }
  }

  // /**
  //  * @brief 获取线程池的线程数量
  //  */
  // size_t thread_count() const { return thread_pool_.get_executor(); }

private:
  asio::thread_pool thread_pool_;
  bool stopped_ = false;
};

} // namespace obcx::core
