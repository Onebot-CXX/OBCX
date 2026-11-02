#include "onebot11/bot/command_adapter.hpp"
#include "core/bot/domain_events.hpp"
#include "core/bot/gateway_codec.hpp"
#include "core/command/command_detection.hpp"
#include "core/command/command_source_projector.hpp"
#include "onebot11/adapter/native_fields.hpp"
#include "onebot11/bot/domain_binding.hpp"
#include <algorithm>
#include <cstdint>
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
class QqCommandPlatformAdapter final : public ICommandPlatformAdapter,
                                       public core::CommandSourceProjector {
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

  [[nodiscard]] auto valid_acl_sender(
      const obcx::bot::BotInstallationRef &installation,
      std::string_view sender) const -> bool override {
    try {
      QqDomainBinding binding;
      binding.codec().validate(installation);
      if (sender.empty() || sender.size() > 20)
        return false;
      // Match the live native sender grammar, including zero/negative IDs
      // retained as UnknownSender. This is not genuine user conversion.
      return adapter::onebot11::numeric_identity(
                 common::json{{"user_id", std::string{sender}}}, "user_id")
          .has_value();
    } catch (...) {
      return false;
    }
  }

  [[nodiscard]] auto project_source(
      const MessageEnvelope &event,
      const obcx::bot::BotInstallationRef &installation) const
      -> obcx::bot::BotOperationResult<core::CommandSourceProjection> override {
    namespace domain = obcx::bot;
    try {
      if (event.source_platform != platform() ||
          event.source_bot != installation.installation_id ||
          !event.payload.is_object() ||
          !event.payload.at("source_bot_configured").is_boolean() ||
          !event.payload.at("source_bot_configured").get<bool>())
        throw std::invalid_argument("unconfigured command source");
      const auto field = [&event](std::string_view key) {
        const auto &value =
            event.payload.at(key).get_ref<const std::string &>();
        if (value.size() > 1024 ||
            !std::ranges::all_of(value, [](unsigned char c) {
              return c >= 0x20U && c != 0x7FU;
            }))
          throw std::invalid_argument("invalid command compatibility field");
        return value;
      };
      const auto kind = field("message_type");
      const auto sender = field("sender");
      const auto group = field("group_id");
      const auto chat =
          event.payload.contains("chat_id") ? field("chat_id") : std::string{};
      const auto message_id = field("message_id");
      if (sender.empty() || field("conversation_id") != event.conversation_id)
        throw std::invalid_argument("command source identity mismatch");
      auto source = event.payload.at("message").get<domain::MessageReceived>();
      domain::detail::validate_domain_wire(event.raw);
      domain::detail::validate_domain_wire(event.payload.at("payload"));
      if (!event.raw.is_object() || event.payload.at("payload") != event.raw ||
          event.raw.at("message_type") != kind ||
          event.raw.at("user_id") != sender ||
          event.raw.at("message_id") != message_id)
        throw std::invalid_argument("command archive identity mismatch");
      QqDomainBinding binding;
      binding.codec().validate(installation);
      domain::binding::validate_native_event(binding.codec(), source);
      if (event.payload.contains("topic_id") ||
          event.raw.contains("message_thread_id"))
        throw std::invalid_argument("QQ command cannot have a topic");
      if (!((kind == "group" && !group.empty() &&
             (chat.empty() || chat == group) &&
             event.conversation_id == "group:" + group) ||
            (kind == "private" && group.empty() &&
             (chat.empty() || chat == sender) &&
             event.conversation_id == "private:" + sender)))
        throw std::invalid_argument("QQ command scope mismatch");
      for (const auto key : {"guild_id", "channel_id"})
        if (event.raw.contains(key) && !event.raw.at(key).is_null())
          throw std::invalid_argument("unsupported QQ command scope");
      if ((kind == "group" && event.raw.at("group_id") != group) ||
          (kind == "private" && event.raw.contains("group_id") &&
           !event.raw.at("group_id").is_null()))
        throw std::invalid_argument("QQ command group mismatch");
      // The canonical archive may contain an empty self_id overlay. It is
      // not sender evidence and is deliberately not reinterpreted here.
      if (!adapter::onebot11::numeric_identity(event.raw, "user_id"))
        throw std::invalid_argument("invalid QQ command sender identity");
      if (event.raw.contains("sender") && event.raw.at("sender").is_object()) {
        const auto &native_sender = event.raw.at("sender");
        if (native_sender.contains("user_id") &&
            adapter::onebot11::numeric_identity(native_sender, "user_id") !=
                std::optional{sender})
          throw std::invalid_argument("QQ sender evidence mismatch");
      }
      const auto conversation = binding.configured_conversation(
          installation,
          {kind == "group" ? domain::ConfiguredConversationKind::Group
                           : domain::ConfiguredConversationKind::Private,
           kind == "group" ? group : sender, std::nullopt});
      const domain::MessageRef expected{
          conversation,
          binding.codec().make<domain::MessageId>("message", {message_id})};
      if (source.message != expected)
        throw std::invalid_argument("QQ typed command source mismatch");
      const bool anonymous = event.raw.contains("anonymous") &&
                             !event.raw.at("anonymous").is_null();
      std::optional<domain::UserRef> expected_user;
      if (!anonymous) {
        try {
          expected_user = domain::UserRef{
              installation,
              binding.codec().make<domain::UserId>("user", {sender})};
        } catch (const std::invalid_argument &) {
          // The live binding also retains unknown/non-user sender evidence.
        }
      }
      const auto *known = std::get_if<domain::KnownUser>(&source.sender);
      if ((expected_user && (!known || known->user != *expected_user)) ||
          (!expected_user &&
           !std::holds_alternative<domain::UnknownSender>(source.sender)))
        throw std::invalid_argument("QQ typed sender mismatch");
      return domain::BotOperationResult<core::CommandSourceProjection>::success(
          {std::move(source),
           {std::string{platform()}, event.source_bot,
            kind == "group" ? command::ConversationKind::Group
                            : command::ConversationKind::Private,
            group, sender, std::nullopt}});
    } catch (...) {
      return domain::failed_operation<core::CommandSourceProjection>(
          domain::BotOperationErrorCode::InvalidRequest,
          "command source has missing, inconsistent or unsupported evidence");
    }
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
    try {
      if (!((kind == "private" && !sender.empty() && group.empty() &&
             event.conversation_id == "private:" + sender) ||
            (kind == "group" && !sender.empty() && !group.empty() &&
             event.conversation_id == "group:" + group)))
        return failure("command_reply_scope_invalid");
      const auto source =
          event.payload.at("message").get<obcx::bot::MessageReceived>();
      QqDomainBinding binding;
      const auto expected = binding.configured_conversation(
          installation,
          {kind == "group" ? obcx::bot::ConfiguredConversationKind::Group
                           : obcx::bot::ConfiguredConversationKind::Private,
           kind == "group" ? group : sender, std::nullopt});
      if (source.message.conversation != expected)
        return failure("command_reply_scope_invalid");
      return {.request = obcx::bot::SendMessageRequest{
                  expected,
                  {obcx::bot::RichText{obcx::bot::Text{std::move(text), {}}}},
                  source.message,
                  obcx::bot::DeliveryPolicy::SingleMessageOnly,
                  {1, 0, 0}}};
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
