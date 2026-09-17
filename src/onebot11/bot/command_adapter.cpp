#include "onebot11/bot/command_adapter.hpp"
#include "core/bot/gateway_codec.hpp"
#include "core/bot/messaging.hpp"
#include "core/command/command_detection.hpp"
#include <utility>

namespace obcx::onebot11::bot {
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
class QqCommandPlatformAdapter final : public ICommandPlatformAdapter {
public:
  [[nodiscard]] auto platform() const noexcept -> std::string_view override {
    return "qq";
  }

  [[nodiscard]] auto detect(const MessageEnvelope &event) const
      -> std::optional<DetectedCommand> override {
    const auto text = raw_text(event, "raw_message");
    if (text.empty() || text.front() != '/') {
      return std::nullopt;
    }
    const auto separator = text.find_first_of(" \t\r\n");
    const auto token =
        separator == std::string::npos ? text : text.substr(0, separator);
    const auto arguments = separator == std::string::npos
                               ? std::string_view{}
                               : std::string_view{text}.substr(separator);
    return command_from_token(token, arguments, {}, false);
  }

  [[nodiscard]] auto validate_catalog(
      const std::vector<CommandCatalogEntry> &catalog) const
      -> std::optional<std::string> override {
    for (const auto &entry : catalog) {
      if (!command::valid_name(entry.name) || entry.description.empty()) {
        return "QQ command catalog contains an invalid entry";
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] auto supports_catalog_publication() const noexcept
      -> bool override {
    return false;
  }

  [[nodiscard]] auto build_text_reply(const MessageEnvelope &event,
                                      std::string text) const
      -> CommandReplyBuildResult override {
    const auto failure = [](std::string code) {
      return CommandReplyBuildResult{
          .code = std::move(code),
          .message = "OneBot command reply identity is invalid"};
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
    const auto kind = string_field("message_type");
    const obcx::bot::BotInstallationRef installation{
        .installation_id = event.source_bot,
        .surface = obcx::bot::SurfaceId{"onebot11.qq"}};
    const common::Message message = {
        {.type = "text", .data = {{"text", std::move(text)}}}};

    try {
      if (kind == "private" && !sender.empty() && group.empty() &&
          event.conversation_id == "private:" + sender) {
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
          event.conversation_id != "group:" + group) {
        return failure("command_reply_scope_invalid");
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

  auto publish_catalog(CommandCatalogPublisher *,
                       const std::vector<CommandCatalogEntry> &)
      -> boost::asio::awaitable<CommandCatalogPublishResult> override {
    co_return CommandCatalogPublishResult{
        .supported = false,
        .succeeded = true,
    };
  }
};

} // namespace
auto make_command_adapter() -> std::shared_ptr<core::ICommandPlatformAdapter> {
  return std::make_shared<QqCommandPlatformAdapter>();
}
} // namespace obcx::onebot11::bot
