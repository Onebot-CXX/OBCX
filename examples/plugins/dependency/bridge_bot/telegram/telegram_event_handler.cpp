#include "telegram_event_handler.hpp"
#include "common/logger.hpp"

#include <nlohmann/json.hpp>

namespace bridge::telegram {

TelegramEventHandler::TelegramEventHandler(
    std::shared_ptr<obcx::storage::DatabaseManager> db_manager,
    std::function<boost::asio::awaitable<void>(
        obcx::core::IBot &, obcx::core::IBot &, obcx::common::MessageEvent)>
        forward_function)
    : db_manager_(db_manager), forward_function_(forward_function) {}

auto TelegramEventHandler::handle_message_deleted(
    obcx::core::IBot &telegram_bot, obcx::core::IBot &qq_bot,
    obcx::common::Event event) -> boost::asio::awaitable<void> {

  try {
    // Telegram的删除事件通常也是MessageEvent，但需要特殊的标识
    // 这里假设删除事件会在context中包含"deleted": true标识
    // 具体实现需要根据Telegram adapter的事件格式调整

    // 暂时跳过，因为需要先了解Telegram删除事件的具体格式
    OBCX_DEBUG("Telegram消息删除事件处理尚未完全实现");
    co_return;

  } catch (const std::exception &e) {
    OBCX_ERROR("处理Telegram删除事件时出错: {}", e.what());
  }
}

auto TelegramEventHandler::handle_message_edited(
    obcx::core::IBot &telegram_bot, obcx::core::IBot &qq_bot,
    obcx::common::MessageEvent event) -> boost::asio::awaitable<void> {

  try {
    // 确保是群消息编辑
    if (event.message_type != "group" || !event.group_id.has_value()) {
      co_return;
    }

    const std::string telegram_group_id = event.group_id.value();
    OBCX_INFO("处理Telegram群 {} 中消息 {} 的编辑事件", telegram_group_id,
              event.message_id);

    // 查找对应的QQ消息ID
    auto target_message_id =
        db_manager_->get_target_message_id("telegram", event.message_id, "qq");

    if (!target_message_id.has_value()) {
      OBCX_DEBUG("未找到Telegram消息 {} 对应的QQ消息映射", event.message_id);
      co_return;
    }

    bool recall_success = false;

    try {
      // 先尝试撤回QQ上的原消息
      auto recall_response =
          co_await qq_bot.delete_message(target_message_id.value());

      // 解析撤回响应
      nlohmann::json recall_json = nlohmann::json::parse(recall_response);

      if (recall_json.contains("status") && recall_json["status"] == "ok") {
        OBCX_INFO("成功在QQ撤回消息: {}", target_message_id.value());
        recall_success = true;
      } else {
        OBCX_WARN("QQ撤回消息失败: {}, 响应: {}", target_message_id.value(),
                  recall_response);
      }

    } catch (const std::exception &e) {
      OBCX_WARN("尝试在QQ撤回消息时出错: {}", e.what());
    }

    // 无论撤回是否成功，都尝试重发编辑后的消息
    OBCX_INFO("开始重发编辑后的消息到QQ (撤回状态: {})",
              recall_success ? "成功" : "失败");

    try {
      // 如果撤回成功，先删除旧的消息映射
      if (recall_success) {
        db_manager_->delete_message_mapping("telegram", event.message_id, "qq");
        OBCX_INFO("撤回成功，已删除旧的消息映射");
      }

      // 使用传入的转发函数重发编辑后的内容
      co_await forward_function_(telegram_bot, qq_bot, event);

      OBCX_INFO("成功重发编辑后的消息");

    } catch (const std::exception &e) {
      OBCX_ERROR("重发编辑后的消息时出错: {}", e.what());

      // 如果是撤回成功但重发失败的情况，需要恢复映射或处理
      if (recall_success) {
        OBCX_WARN("撤回成功但重发失败，原QQ消息已被撤回但新消息发送失败");
      } else {
        // 撤回失败且重发也失败时，删除映射避免数据不一致
        db_manager_->delete_message_mapping("telegram", event.message_id, "qq");
        OBCX_WARN("撤回和重发都失败，已删除消息映射");
      }
      co_return;
    }

  } catch (const std::exception &e) {
    OBCX_ERROR("处理Telegram编辑事件时出错: {}", e.what());
  }
}

} // namespace bridge::telegram