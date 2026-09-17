#include "telegram/bot/command_adapter.hpp"
#include "core/bot/gateway_codec.hpp"
#include "core/bot/messaging.hpp"
#include "core/command/command_detection.hpp"
#include "telegram/bot/operations.hpp"
#include <utility>

namespace obcx::telegram::bot {
namespace {
using core::CommandCatalogEntry;
using core::CommandCatalogPublisher;
using core::CommandCatalogPublishResult;
using core::CommandReplyBuildResult;
using core::DetectedCommand;
using core::ICommandPlatformAdapter;
using core::MessageEnvelope;
namespace command = obcx::command;
using core::command_detail::command_from_token;
using core::command_detail::raw_text;
class TelegramCommandPlatformAdapter final : public ICommandPlatformAdapter {
public:
  explicit TelegramCommandPlatformAdapter(std::string bot_target)
      : bot_target_(std::move(bot_target)) {}
  [[nodiscard]] auto platform() const noexcept -> std::string_view override {
    return "telegram";
  }

  [[nodiscard]] auto detect(const MessageEnvelope &event) const
      -> std::optional<DetectedCommand> override {
    auto text = raw_text(event, "text");
    const common::json *entities = nullptr;
    if (!text.empty() && event.raw.contains("entities")) {
      entities = &event.raw.at("entities");
    } else {
      text = raw_text(event, "caption");
      if (!text.empty() && event.raw.contains("caption_entities")) {
        entities = &event.raw.at("caption_entities");
      }
    }
    if (text.empty() || entities == nullptr || !entities->is_array()) {
      return std::nullopt;
    }
    for (const auto &entity : *entities) {
      if (!entity.is_object() || entity.value("type", "") != "bot_command" ||
          entity.value("offset", -1) != 0) {
        continue;
      }
      const auto length = entity.value("length", 0);
      if (length <= 1 || static_cast<std::size_t>(length) > text.size()) {
        return std::nullopt;
      }
      return command_from_token(
          text.substr(0, static_cast<std::size_t>(length)),
          std::string_view{text}.substr(static_cast<std::size_t>(length)),
          bot_target_, true);
    }
    return std::nullopt;
  }

  [[nodiscard]] auto validate_catalog(
      const std::vector<CommandCatalogEntry> &catalog) const
      -> std::optional<std::string> override {
    if (catalog.size() > 100) {
      return "Telegram accepts at most 100 bot commands";
    }
    for (const auto &entry : catalog) {
      if (!command::valid_name(entry.name)) {
        return "Telegram command name is invalid";
      }
      if (entry.description.empty() || entry.description.size() > 256) {
        return "Telegram command description must contain 1 to 256 bytes";
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] auto supports_catalog_publication() const noexcept
      -> bool override {
    return true;
  }

  [[nodiscard]] auto build_text_reply(const MessageEnvelope &event,
                                      std::string text) const
      -> CommandReplyBuildResult override {
    const auto failure = [](std::string code) {
      return CommandReplyBuildResult{
          .code = std::move(code),
          .message = "Telegram command reply identity is invalid"};
    };
    if (event.source_platform != platform() || event.source_bot.empty() ||
        !event.payload.is_object() || text.empty()) {
      return failure("command_reply_scope_invalid");
    }
    const auto string_field = [&event](const std::string_view key) {
      const auto field = event.payload.find(key);
      return field != event.payload.end() && field->is_string()
                 ? field->get<std::string>()
                 : std::string{};
    };
    const auto sender = string_field("sender");
    const auto group = string_field("group_id");
    const auto chat = string_field("chat_id");
    const auto kind = string_field("message_type");
    const obcx::bot::BotInstallationRef installation{
        .installation_id = event.source_bot,
        .surface = obcx::bot::SurfaceId{"telegram.bot_api"}};
    const common::Message message = {
        {.type = "text", .data = {{"text", std::move(text)}}}};

    try {
      if (kind == "private" && !sender.empty() && group.empty() &&
          (chat.empty() || chat == sender) &&
          (event.conversation_id == "private:" + sender ||
           event.conversation_id == "chat:" + sender)) {
        obcx::bot::SendPrivateMessageRequest request{
            .target = {.installation = installation, .native_user_id = sender},
            .message = message};
        return {
            .operation = obcx::bot::OperationEnvelope{
                .installation = installation,
                .action = decltype(request)::action,
                .payload = obcx::bot::GatewayCodec<decltype(request)>::encode(
                    request)}};
      }
      if (kind != "group" || sender.empty() || group.empty() ||
          (!chat.empty() && chat != group) ||
          (event.conversation_id != "chat:" + group &&
           event.conversation_id != "group:" + group)) {
        return failure("command_reply_scope_invalid");
      }
      if (event.payload.contains("topic_id")) {
        const auto &topic = event.payload.at("topic_id");
        if (!topic.is_number_integer() || topic.get<std::int64_t>() <= 0) {
          return failure("command_reply_topic_invalid");
        }
        obcx::telegram::bot::SendTelegramTopicMessageRequest request{
            .target = {.group = {.installation = installation,
                                 .native_group_id = group},
                       .topic_id = topic.get<std::int64_t>()},
            .message = message};
        return {
            .operation = obcx::bot::OperationEnvelope{
                .installation = installation,
                .action = decltype(request)::action,
                .payload = obcx::bot::GatewayCodec<decltype(request)>::encode(
                    request)}};
      }
      obcx::bot::SendGroupMessageRequest request{
          .target = {.installation = installation, .native_group_id = group},
          .message = message};
      return {.operation = obcx::bot::OperationEnvelope{
                  .installation = installation,
                  .action = decltype(request)::action,
                  .payload = obcx::bot::GatewayCodec<decltype(request)>::encode(
                      request)}};
    } catch (...) {
      return failure("command_reply_encoding_failed");
    }
  }

  auto publish_catalog(CommandCatalogPublisher *catalog,
                       const std::vector<CommandCatalogEntry> &entries)
      -> boost::asio::awaitable<CommandCatalogPublishResult> override {
    if (catalog == nullptr) {
      co_return CommandCatalogPublishResult{
          .supported = true,
          .succeeded = false,
          .code = "command_catalog_capability_missing",
          .message = "configured Telegram installation lacks command catalog "
                     "capability",
      };
    }
    try {
      co_return co_await catalog->publish(entries);
    } catch (...) {
      co_return CommandCatalogPublishResult{
          .supported = true,
          .succeeded = false,
          .code = "command_catalog_publish_failed",
          .message = "Telegram command catalog publication failed",
      };
    }
  }

private:
  const std::string bot_target_;
};

} // namespace
auto make_command_adapter(std::string bot_target)
    -> std::shared_ptr<core::ICommandPlatformAdapter> {
  return std::make_shared<TelegramCommandPlatformAdapter>(
      std::move(bot_target));
}
} // namespace obcx::telegram::bot
