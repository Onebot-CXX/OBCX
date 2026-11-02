#include "telegram/bot/command_adapter.hpp"
#include "core/bot/domain_events.hpp"
#include "core/bot/gateway_codec.hpp"
#include "core/command/command_detection.hpp"
#include "core/command/command_source_projector.hpp"
#include "telegram/bot/domain_binding.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
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
class TelegramCommandPlatformAdapter final
    : public ICommandPlatformAdapter,
      public core::CommandSourceProjector {
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

  [[nodiscard]] auto valid_acl_sender(
      const obcx::bot::BotInstallationRef &installation,
      std::string_view sender) const -> bool override {
    try {
      reference_codec().validate(installation);
      if (sender.empty() || sender.size() > 20)
        return false;
      // The projector retains canonical signed 'from' bytes for ACLs even
      // when sender_chat makes that identity display-only, not a UserRef.
      std::int64_t value{};
      const auto [end, error] =
          std::from_chars(sender.data(), sender.data() + sender.size(), value);
      return error == std::errc{} && end == sender.data() + sender.size() &&
             std::to_string(value) == sender;
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
      const auto binding = domain_binding();
      binding->codec().validate(installation);
      domain::binding::validate_native_event(binding->codec(), source);
      std::optional<std::int64_t> topic;
      if (event.payload.contains("topic_id")) {
        const auto &value = event.payload.at("topic_id");
        if (!value.is_number_integer() ||
            (value.is_number_unsigned() &&
             value.get<std::uint64_t>() > INT32_MAX) ||
            value.get<std::int64_t>() <= 0 ||
            value.get<std::int64_t>() > INT32_MAX)
          throw std::invalid_argument("invalid Telegram command topic");
        topic = value.get<std::int64_t>();
      }
      if (event.raw.contains("message_thread_id") != topic.has_value() ||
          (topic && event.raw.at("message_thread_id") != *topic))
        throw std::invalid_argument("Telegram command topic presence mismatch");
      if (!((kind == "group" && !group.empty() &&
             (chat.empty() || chat == group) &&
             (event.conversation_id == "group:" + group ||
              event.conversation_id == "chat:" + group)) ||
            (kind == "private" && group.empty() && !topic &&
             (chat.empty() || chat == sender) &&
             (event.conversation_id == "private:" + sender ||
              event.conversation_id == "chat:" + sender))))
        throw std::invalid_argument("Telegram command scope mismatch");
      // Keep the original payload/thread equality check above. ACL/routing
      // scope uses forum topics, not ordinary supergroup reply-chain IDs.
      if (!event.raw.value("is_topic_message", false))
        topic.reset();
      else if (!topic)
        throw std::invalid_argument("Telegram command lacks forum topic");
      const auto conversation = binding->configured_conversation(
          installation,
          {kind == "group" ? domain::ConfiguredConversationKind::Group
                           : domain::ConfiguredConversationKind::Private,
           kind == "group" ? group : sender, topic});
      const domain::MessageRef expected{
          conversation,
          binding->codec().make<domain::MessageId>("message", {message_id})};
      if (native_conversation(installation, event.raw) != conversation ||
          source.message != expected ||
          (kind == "group" && event.raw.at("group_id") != group) ||
          (kind == "private" && event.raw.contains("group_id") &&
           !event.raw.at("group_id").is_null()))
        throw std::invalid_argument("Telegram typed command source mismatch");
      const auto native_id = [](const domain::Json &value) {
        if (!value.is_number_integer() ||
            (value.is_number_unsigned() &&
             value.get<std::uint64_t>() > INT64_MAX))
          throw std::invalid_argument("invalid Telegram sender identity");
        return std::to_string(value.get<std::int64_t>());
      };
      // ACL identity follows the observed 'from' bytes even when sender_chat
      // is the author. Never turn a display-only identity into a UserRef.
      const auto display_sender = event.raw.contains("from")
                                      ? native_id(event.raw.at("from").at("id"))
                                      : std::string{};
      if (display_sender != sender)
        throw std::invalid_argument("Telegram compatibility sender mismatch");
      if (event.raw.contains("sender_chat")) {
        const auto *actual =
            std::get_if<domain::ConversationSender>(&source.sender);
        const auto expected = native_conversation(
            installation, domain::Json{{"chat", event.raw.at("sender_chat")}});
        if (!actual || actual->conversation != expected)
          throw std::invalid_argument("Telegram conversation sender mismatch");
      } else if (event.raw.contains("from")) {
        const auto *actual = std::get_if<domain::KnownUser>(&source.sender);
        if (!actual || actual->user != native_user(installation, sender))
          throw std::invalid_argument("Telegram user sender mismatch");
      } else if (!std::holds_alternative<domain::UnknownSender>(
                     source.sender)) {
        throw std::invalid_argument("Telegram unknown sender mismatch");
      }
      return domain::BotOperationResult<core::CommandSourceProjection>::success(
          {std::move(source),
           {std::string{platform()}, event.source_bot,
            kind == "group" ? command::ConversationKind::Group
                            : command::ConversationKind::Private,
            group, sender, topic}});
    } catch (...) {
      return domain::failed_operation<core::CommandSourceProjection>(
          domain::BotOperationErrorCode::InvalidRequest,
          "command source has missing, inconsistent or unsupported evidence");
    }
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
    try {
      const auto message =
          event.payload.at("message").get<obcx::bot::MessageReceived>();
      if (message.message.conversation.installation != installation)
        return failure("command_reply_scope_invalid");
      if (kind == "private") {
        if (sender.empty() || !group.empty() ||
            (!chat.empty() && chat != sender) ||
            (event.conversation_id != "private:" + sender &&
             event.conversation_id != "chat:" + sender))
          return failure("command_reply_scope_invalid");
      } else if (kind != "group" || sender.empty() || group.empty() ||
                 (!chat.empty() && chat != group) ||
                 (event.conversation_id != "chat:" + group &&
                  event.conversation_id != "group:" + group))
        return failure("command_reply_scope_invalid");
      std::optional<std::int64_t> topic;
      if (event.payload.contains("topic_id")) {
        const auto &value = event.payload.at("topic_id");
        if (!value.is_number_integer() ||
            (value.is_number_unsigned() &&
             value.get<std::uint64_t>() > INT32_MAX) ||
            value.get<std::int64_t>() <= 0 ||
            value.get<std::int64_t>() > INT32_MAX)
          return failure("command_reply_topic_invalid");
        topic = value.get<std::int64_t>();
      }
      if (!event.raw.value("is_topic_message", false))
        topic.reset();
      else if (!topic)
        return failure("command_reply_topic_invalid");
      const auto expected = domain_binding()->configured_conversation(
          installation,
          {kind == "group" ? obcx::bot::ConfiguredConversationKind::Group
                           : obcx::bot::ConfiguredConversationKind::Private,
           kind == "group" ? group : sender, topic});
      if (expected != message.message.conversation)
        return failure("command_reply_scope_invalid");
      obcx::bot::SendMessageRequest request{
          .target = message.message.conversation,
          .body = {obcx::bot::RichText{obcx::bot::Text{std::move(text), {}}}},
          .reply_to = message.message,
          .delivery = obcx::bot::DeliveryPolicy::SingleMessageOnly,
          .limits = {1, 0, 0}};
      request.validate();
      return {.request = std::move(request)};
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
