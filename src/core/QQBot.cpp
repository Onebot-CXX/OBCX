#include <string>

#include "common/Logger.hpp"
#include "core/QQBot.hpp"
#include "network/IConnectionManager.hpp"
#include "onebot11/network/websocket/ConnectionManager.hpp"

namespace obcx::core {

QQBot::QQBot(adapter::onebot11::ProtocolAdapter adapter)
    : IBot{std::make_unique<adapter::onebot11::ProtocolAdapter>(
          std::move(adapter))} {
  OBCX_INFO("QQBot 实例已创建，所有核心组件已初始化。");
}

QQBot::~QQBot() { OBCX_INFO("QQBot 实例已销毁。"); }

void QQBot::connect(network::ConnectionManagerFactory::ConnectionType type,
                    const common::ConnectionConfig &config) {
  connection_manager_ =
      network::ConnectionManagerFactory::create(type, *io_context_, *adapter_);

  connection_manager_->set_event_callback([this](const common::Event &event) {
    dispatcher_->dispatch(this, event);
  });

  connection_manager_->connect(config);

  OBCX_INFO("使用{}连接类型连接到 {}:{}",
            connection_manager_->get_connection_type(), config.host,
            config.port);
}

void QQBot::connect_ws(std::string_view host, uint16_t port,
                       std::string_view access_token) {
  common::ConnectionConfig config;
  config.host = host;
  config.port = port;
  config.access_token = access_token;

  connect(network::ConnectionManagerFactory::ConnectionType::Onebot11WebSocket,
          config);
}

void QQBot::connect_http(std::string_view host, uint16_t port,
                         std::string_view access_token) {
  common::ConnectionConfig config;
  config.host = host;
  config.port = port;
  config.access_token = access_token;

  connect(network::ConnectionManagerFactory::ConnectionType::Onebot11HTTP,
          config);
}

void QQBot::run() {
  if (io_context_->stopped()) {
    io_context_->restart();
  }
  OBCX_INFO("QQBot 开始运行事件循环...");
  io_context_->run();
  OBCX_INFO("QQBot 事件循环已结束。");
}

void QQBot::stop() {
  OBCX_INFO("正在请求停止 Bot...");

  // 首先断开连接
  if (connection_manager_) {
    connection_manager_->disconnect();
  }

  if (task_scheduler_) {
    task_scheduler_->stop();
  }

  io_context_->stop();
}

auto QQBot::send_private_message(std::string_view user_id,
                                 const common::Message &message)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_send_private_message_request(
      user_id, message, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::send_group_message(std::string_view group_id,
                               const common::Message &message)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_send_group_message_request(
      group_id, message, echo_id);

  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

// --- 消息管理 API ---

auto QQBot::delete_message(std::string_view message_id)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload =
      adapter_->serialize_delete_message_request("", message_id, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::get_message(std::string_view message_id)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload =
      get_onebot_adapter().serialize_get_message_request(message_id, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::get_forward_msg(std::string_view forward_id)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_get_forward_msg_request(
      forward_id, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

// --- 好友管理 API ---

auto QQBot::get_friend_list() -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload =
      get_onebot_adapter().serialize_get_friend_list_request(echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::get_stranger_info(std::string_view user_id, bool no_cache)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload =
      adapter_->serialize_get_user_info_request("", user_id, no_cache, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

// --- 群组管理 API ---

auto QQBot::get_group_list() -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_get_group_list_request(echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::get_group_info(std::string_view group_id, bool no_cache)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload =
      adapter_->serialize_get_chat_info_request(group_id, no_cache, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::get_group_member_list(std::string_view group_id)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = adapter_->serialize_get_chat_admins_request(group_id, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::get_group_member_info(std::string_view group_id,
                                  std::string_view user_id, bool no_cache)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = adapter_->serialize_get_chat_member_info_request(
      group_id, user_id, no_cache, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::set_group_kick(std::string_view group_id, std::string_view user_id,
                           bool reject_add_request)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = adapter_->serialize_kick_chat_member_request(
      group_id, user_id, reject_add_request, false, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::set_group_ban(std::string_view group_id, std::string_view user_id,
                          int32_t duration) -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = adapter_->serialize_ban_chat_member_request(group_id, user_id,
                                                             duration, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::set_group_whole_ban(std::string_view group_id, bool enable)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload =
      adapter_->serialize_ban_all_members_request(group_id, enable, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::set_group_card(std::string_view group_id, std::string_view user_id,
                           std::string_view card)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload =
      adapter_->serialize_set_chat_title_request(group_id, card, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::set_group_leave(std::string_view group_id, bool is_dismiss)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload =
      adapter_->serialize_leave_chat_request(group_id, is_dismiss, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

// --- 状态获取 API ---

auto QQBot::get_login_info() -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = adapter_->serialize_get_self_info_request(echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::get_status() -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_get_status_request(echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::get_version_info() -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload =
      get_onebot_adapter().serialize_get_version_info_request(echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

// --- 请求处理 API ---

auto QQBot::set_friend_add_request(std::string_view flag, bool approve,
                                   std::string_view remark)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_set_friend_add_request(
      flag, approve, remark, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::set_group_add_request(std::string_view flag,
                                  std::string_view sub_type, bool approve,
                                  std::string_view reason)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_set_group_add_request(
      flag, sub_type, approve, reason, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::generate_echo_id() -> uint64_t {
  static std::atomic<uint64_t> counter{0};
  return counter.fetch_add(1);
}

// --- 群组管理扩展 API ---

auto QQBot::set_group_name(std::string_view group_id,
                           std::string_view group_name)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_set_group_name_request(
      group_id, group_name, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::set_group_admin(std::string_view group_id, std::string_view user_id,
                            bool enable) -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_set_group_admin_request(
      group_id, user_id, enable, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::set_group_anonymous_ban(std::string_view group_id,
                                    const std::string &anonymous,
                                    int32_t duration)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_set_group_anonymous_ban_request(
      group_id, anonymous, duration, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::set_group_anonymous(std::string_view group_id, bool enable)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_set_group_anonymous_request(
      group_id, enable, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::set_group_portrait(std::string_view group_id, std::string_view file,
                               bool cache) -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_set_group_portrait_request(
      group_id, file, cache, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::get_group_honor_info(std::string_view group_id,
                                 std::string_view type)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_get_group_honor_info_request(
      group_id, type, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

// --- 资源管理 API ---

auto QQBot::get_image(std::string_view file) -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload =
      get_onebot_adapter().serialize_get_image_request(file, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::get_record(std::string_view file, std::string_view out_format)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_get_record_request(
      file, out_format, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::get_group_file_url(std::string_view group_id,
                               std::string_view file_id)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_get_group_file_url_request(
      group_id, file_id, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::get_private_file_url(std::string_view user_id,
                                 std::string_view file_id)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_get_private_file_url_request(
      user_id, file_id, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

// --- 能力检查 API ---

auto QQBot::can_send_image() -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_can_send_image_request(echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::can_send_record() -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload =
      get_onebot_adapter().serialize_can_send_record_request(echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

// --- QQ相关接口凭证 API ---

auto QQBot::get_cookies(std::string_view domain)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload =
      get_onebot_adapter().serialize_get_cookies_request(domain, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::get_csrf_token() -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_onebot_adapter().serialize_get_csrf_token_request(echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::get_credentials(std::string_view domain)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload =
      get_onebot_adapter().serialize_get_credentials_request(domain, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto QQBot::is_connected() const -> bool {
  return connection_manager_ && connection_manager_->is_connected();
}

void QQBot::ensure_connection_manager() const {
  if (!connection_manager_) {
    throw std::runtime_error("Bot未连接，请先调用connect*方法");
  }
}

void QQBot::error_notify(std::string_view target_id, std::string_view message,
                         bool is_group) {

  common::ErrorEvent error_event{.error_type = "message_error",
                                 .error_message = std::string(message),
                                 .target_id = std::string(target_id),
                                 .is_group = is_group,
                                 .time = std::chrono::system_clock::now(),
                                 .context = {{"source", "bot_error_handler"}}};

  if (dispatcher_) {
    dispatcher_->dispatch(this, error_event);
  } else {
    OBCX_WARN("事件分发器未初始化，无法分发异常事件");
  }
}

auto QQBot::get_onebot_adapter() const -> adapter::onebot11::ProtocolAdapter & {
  return *static_cast<adapter::onebot11::ProtocolAdapter *>(&*adapter_);
}

} // namespace obcx::core