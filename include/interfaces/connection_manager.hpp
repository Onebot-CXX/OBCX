#pragma once
#include "common/message_type.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include <boost/asio/awaitable.hpp>
#include <functional>
#include <string>

namespace obcx::network {
namespace asio = boost::asio;

/**
 * @brief 连接管理器抽象接口
 *
 * 定义了与OneBot11实现通信的通用接口，支持不同的连接方式：
 * - WebSocket正向连接
 * - HTTP轮询连接
 * - 其他可能的连接方式
 */
class IConnectionManager {
public:
  // 事件回调函数类型
  using EventCallback = std::function<void(const common::Event &)>;

  virtual ~IConnectionManager() = default;

  /**
   * @brief 启动连接
   * @param config 连接配置
   */
  virtual void connect(const common::ConnectionConfig &config) = 0;

  /**
   * @brief 断开连接
   */
  virtual void disconnect() = 0;

  /**
   * @brief 检查连接状态
   * @return 是否已连接
   */
  virtual bool is_connected() const = 0;

  /**
   * @brief 发送API请求并等待响应
   * @param action_payload JSON字符串形式的请求负载
   * @param echo_id 用于匹配响应的echo ID
   * @return 包含响应的JSON字符串的awaitable
   */
  virtual asio::awaitable<std::string> send_action_and_wait_async(
      std::string action_payload, uint64_t echo_id) = 0;

  /**
   * @brief 设置事件回调函数
   * @param callback 当收到新事件时的回调函数
   */
  virtual void set_event_callback(EventCallback callback) = 0;

  /**
   * @brief 获取连接类型的描述
   * @return 连接类型字符串（如"WebSocket", "HTTP"等）
   */
  virtual std::string get_connection_type() const = 0;
};

/**
 * @brief 连接管理器工厂
 *
 * 根据配置创建合适的连接管理器实例
 */
class ConnectionManagerFactory {
public:
  /**
   * @brief 连接类型枚举
   */
  enum class ConnectionType {
    Onebot11WebSocket, ///< WebSocket正向连接
    Onebot11HTTP,      ///< HTTP轮询连接
    TelegramHTTP,      ///< Telegram Bot API HTTP轮询连接
    TelegramWebsocket
  };

  /**
   * @brief 创建连接管理器
   * @param type 连接类型
   * @param ioc IO上下文
   * @param adapter 协议适配器
   * @return 连接管理器实例
   */
  static std::unique_ptr<IConnectionManager> create(
      ConnectionType type, asio::io_context &ioc,
      adapter::BaseProtocolAdapter &adapter);
};
} // namespace obcx::network
