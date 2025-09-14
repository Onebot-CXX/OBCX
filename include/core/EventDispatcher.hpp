#pragma once

#include "common/Logger.hpp"
#include "common/MessageType.hpp"
#include <any>
#include <boost/asio.hpp>
#include <boost/core/demangle.hpp>
#include <functional>
#include <map>
#include <typeindex>
#include <vector>

namespace obcx::core {
namespace asio = boost::asio;

class IBot;

/**
 * @brief 事件分发器，负责类型安全地注册和调用事件处理器
 */
class EventDispatcher {
public:
  /**
   * @brief 构造函数
   * @param io_context Asio的io_context引用，用于启动协程
   */
  explicit EventDispatcher(asio::io_context &io_context)
      : io_context_(io_context) {}

  /**
   * @brief 注册一个事件处理器 (新版本，支持Bot引用)
   * @tparam EventType 要处理的事件类型 (如: common::MessageEvent)
   * @param handler 一个接受Bot引用和事件，返回 asio::awaitable<void>
   * 的协程函数
   */
  template <typename EventType>
  void on(std::function<asio::awaitable<void>(IBot &, EventType)> handler) {
    const auto type_idx = std::type_index(typeid(EventType));

    auto wrapper = [handler](IBot *bot,
                             std::any event_any) -> asio::awaitable<void> {
      try {
        if (auto *event = std::any_cast<EventType>(&event_any)) {
          co_await handler(*bot, *event);
        } else {
          OBCX_WARN("EventDispatcher: 在包装器中类型转换失败 for type {}",
                    typeid(EventType).name());
        }
      } catch (const std::bad_any_cast &e) {
        OBCX_ERROR("EventDispatcher: any_cast 异常: {}", e.what());
      }
      co_return;
    };

    handlers_[type_idx].push_back(std::move(wrapper));
    OBCX_DEBUG("已为事件类型 {} 注册处理函数",
               boost::core::demangle(typeid(EventType).name()));
  }

  /**
   * @brief 分发一个事件给所有已注册的处理器
   * @param bot Bot实例的引用，会传递给事件处理器
   * @param event 从适配器层传入的事件变体
   */
  void dispatch(IBot *bot, const common::Event &event) {
    std::visit(
        [this, bot](const auto &concrete_event) {
          using ConcreteEventType = std::decay_t<decltype(concrete_event)>;
          const auto type_idx = std::type_index(typeid(ConcreteEventType));

          if (handlers_.contains(type_idx)) {
            auto &handlers_for_type = handlers_.at(type_idx);
            OBCX_DEBUG("事件 {} 调用 {} 个处理函数",
                       boost::core::demangle(typeid(ConcreteEventType).name()),
                       handlers_for_type.size());

            for (const auto &handler : handlers_for_type) {
              // 使用 co_spawn 启动用户的协程事件处理器
              // 我们传递 concrete_event 的一个拷贝以确保生命周期
              asio::co_spawn(
                  io_context_,
                  [handler, bot, concrete_event]() -> asio::awaitable<void> {
                    co_await handler(bot, concrete_event);
                  },
                  asio::detached // 目前，我们不关心处理器的返回结果
              );
            }
          } else {
            OBCX_DEBUG("没有为事件类型 {} 注册的处理函数",
                       boost::core::demangle(typeid(ConcreteEventType).name()));
          }
        },
        event);
  }

private:
  asio::io_context &io_context_;
  std::map<std::type_index,
           std::vector<std::function<asio::awaitable<void>(IBot *, std::any)>>>
      handlers_;
};

} // namespace obcx::core