#include "../../core/bot/operation_response_helpers.hpp"
#include "core/bot/operation_handler.hpp"
#include "diagnostics.hpp"
#include "telegram/bot/capability_ids.hpp"
#include "telegram/bot/operation_component.hpp"
#include "telegram/bot/operation_definitions.hpp"
#include "telegram/bot/response_parser.hpp"
#include "telegram/bot/transport.hpp"

#include <atomic>
#include <type_traits>

namespace obcx::core {
namespace {
using obcx::telegram::bot::parse_telegram_operation_response;
using operation_detail::malformed_read;
using operation_detail::malformed_side_effect;
using operation_detail::optional_string;
using operation_detail::parsed_value;
using operation_detail::provider_failure;
using operation_detail::provider_id;
} // namespace

namespace {
enum class TelegramSendResultShape : std::uint8_t {
  SingleMessage,
  MediaGroup,
};

auto telegram_send_result(const bot::BotOperationResult<bot::Json> &parsed,
                          const bot::GroupTarget &target,
                          const std::string_view operation,
                          const TelegramSendResultShape expected_shape,
                          const std::size_t expected_count = 1U)
    -> bot::BotOperationResult<bot::SendMessageResult> {
  if (!parsed.ok()) {
    return provider_failure<bot::SendMessageResult>(parsed);
  }
  if (!parsed.value) {
    return malformed_side_effect<bot::SendMessageResult>(
        std::string{operation} + " response has no result");
  }
  const auto &value = parsed.value.value();
  std::vector<bot::BotMessageRef> messages;
  const auto append = [&](const bot::Json &message) {
    const auto id = provider_id(message, "message_id");
    if (!id) {
      return false;
    }
    messages.push_back({.group = target, .native_message_id = *id});
    return true;
  };
  if (expected_shape == TelegramSendResultShape::SingleMessage &&
      value.is_object()) {
    if (!append(value)) {
      return malformed_side_effect<bot::SendMessageResult>(
          std::string{operation} + " response is missing message_id");
    }
  } else if (expected_shape == TelegramSendResultShape::MediaGroup &&
             value.is_array() && value.size() == expected_count) {
    for (const auto &message : value) {
      if (!message.is_object() || !append(message)) {
        return malformed_side_effect<bot::SendMessageResult>(
            std::string{operation} +
            " response contains an invalid message result");
      }
    }
  } else {
    return malformed_side_effect<bot::SendMessageResult>(
        std::string{operation} + " response has an unexpected result shape");
  }
  return bot::BotOperationResult<bot::SendMessageResult>::success(
      {.messages = std::move(messages)});
}

auto telegram_entities(
    const std::vector<obcx::telegram::bot::TelegramTextEntity> &entities)
    -> std::vector<TelegramTextEntity> {
  std::vector<TelegramTextEntity> converted;
  converted.reserve(entities.size());
  for (const auto &entity : entities) {
    converted.push_back({.type = entity.type,
                         .offset = entity.offset,
                         .length = entity.length});
  }
  return converted;
}

} // namespace
class TelegramMediaUploadComponent::Uploader final
    : public TelegramMediaUploader {
public:
  void configure(std::shared_ptr<TelegramTransport> transport) {
    if (transport == nullptr || transport_ != nullptr) {
      throw BotComponentRuntimeError(
          "Telegram media uploader requires one transport");
    }
    transport_ = std::move(transport);
  }

  auto upload(
      const obcx::telegram::bot::SendTelegramPhotoUploadRequest &request)
      -> boost::asio::awaitable<std::string> override {
    if (transport_ == nullptr) {
      throw BotComponentRuntimeError("Telegram media uploader is not prepared");
    }
    const TelegramMediaUpload photo{
        .type = request.photo.type,
        .filename = request.photo.filename,
        .mime_type = request.photo.mime_type,
        .data = std::string{
            reinterpret_cast<const char *>(request.photo.bytes.data()),
            request.photo.bytes.size()}};
    co_return co_await transport_->upload_photo(request.target.native_group_id,
                                                photo, request.caption,
                                                request.topic_id);
  }

  auto upload(
      const obcx::telegram::bot::SendTelegramMediaGroupUploadsRequest &request)
      -> boost::asio::awaitable<std::string> override {
    if (transport_ == nullptr) {
      throw BotComponentRuntimeError("Telegram media uploader is not prepared");
    }
    std::vector<TelegramMediaUpload> media;
    media.reserve(request.media.size());
    for (const auto &item : request.media) {
      media.push_back({.type = item.type,
                       .filename = item.filename,
                       .mime_type = item.mime_type,
                       .data = std::string{
                           reinterpret_cast<const char *>(item.bytes.data()),
                           item.bytes.size()}});
    }
    const auto reply = request.reply_to
                           ? std::optional{request.reply_to->native_message_id}
                           : std::nullopt;
    co_return co_await transport_->upload_media_group(
        request.target.native_group_id, media, request.caption,
        request.topic_id, reply, telegram_entities(request.caption_entities));
  }

private:
  std::shared_ptr<TelegramTransport> transport_;
};

class TelegramOperationsComponent::Endpoint final {
public:
  explicit Endpoint(std::string installation_id)
      : installation_{.installation_id = std::move(installation_id),
                      .surface = bot::SurfaceId{"telegram.bot_api"}} {
    installation_.validate();
  }

  void configure(std::shared_ptr<adapter::telegram::ProtocolAdapter> protocol,
                 std::shared_ptr<TelegramTransport> transport,
                 std::shared_ptr<TelegramMediaUploader> uploader) {
    if (protocol == nullptr || transport == nullptr || protocol_ != nullptr ||
        transport_ != nullptr) {
      throw BotComponentRuntimeError(
          "Telegram operations require one protocol and transport");
    }
    protocol_ = std::move(protocol);
    transport_ = std::move(transport);
    uploader_ = std::move(uploader);
  }

  [[nodiscard]] auto installation() const -> bot::BotInstallationRef {
    return installation_;
  }

  auto execute(const bot::SendGroupMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_send_message_request(
            request.target.native_group_id, request.message, echo),
        echo);
    const auto parsed = parse_telegram_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::SendMessageResult>(parsed);
    }
    const auto message_id = provider_id(parsed_value(parsed), "message_id");
    if (!message_id) {
      co_return malformed_side_effect<bot::SendMessageResult>(
          "Telegram send response is missing message_id");
    }
    co_return bot::BotOperationResult<bot::SendMessageResult>::success(
        {.messages = {
             {.group = request.target, .native_message_id = *message_id}}});
  }

  auto execute(const bot::SendPrivateMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendPrivateMessageResult>> {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_send_message_request(request.target.native_user_id,
                                                  request.message, echo),
        echo);
    const auto parsed = parse_telegram_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::SendPrivateMessageResult>(parsed);
    }
    const auto &value = parsed_value(parsed);
    const auto message_id = provider_id(value, "message_id");
    const auto chat_id = value.is_object() && value.contains("chat") &&
                                 value.at("chat").is_object()
                             ? provider_id(value.at("chat"), "id")
                             : std::nullopt;
    if (!message_id || !chat_id || *chat_id != request.target.native_user_id) {
      co_return malformed_side_effect<bot::SendPrivateMessageResult>(
          "Telegram private-send response has invalid target or message_id");
    }
    co_return bot::BotOperationResult<bot::SendPrivateMessageResult>::success(
        {.messages = {
             {.target = request.target, .native_message_id = *message_id}}});
  }

  auto execute(const bot::DeleteMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::DeleteMessageResult>> {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_delete_message_request(
            request.message.group.native_group_id,
            request.message.native_message_id, echo),
        echo);
    const auto parsed = parse_telegram_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::DeleteMessageResult>(parsed);
    }
    if (!parsed_value(parsed).is_boolean() ||
        !parsed_value(parsed).get<bool>()) {
      co_return malformed_side_effect<bot::DeleteMessageResult>(
          "Telegram delete response does not confirm deletion");
    }
    co_return bot::BotOperationResult<bot::DeleteMessageResult>::success(
        {.message = request.message});
  }

  auto execute(
      const obcx::telegram::bot::SendTelegramTopicMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_send_topic_message_request(
            request.target.group.native_group_id, request.message, echo,
            request.target.topic_id),
        echo);
    co_return telegram_send_result(
        parse_telegram_operation_response(response, true), request.target.group,
        "Telegram topic send", TelegramSendResultShape::SingleMessage);
  }

  auto execute(
      const obcx::telegram::bot::EditTelegramMessageTextRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<obcx::telegram::bot::EditMessageTextResult>> {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_edit_message_text_request(
            request.message.group.native_group_id,
            request.message.native_message_id, request.text, request.parse_mode,
            echo),
        echo);
    const auto parsed = parse_telegram_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<obcx::telegram::bot::EditMessageTextResult>(
          parsed);
    }
    const auto message_id = provider_id(parsed_value(parsed), "message_id");
    if (!message_id || *message_id != request.message.native_message_id) {
      co_return malformed_side_effect<
          obcx::telegram::bot::EditMessageTextResult>(
          "Telegram edit response has no matching message_id");
    }
    co_return bot::
        BotOperationResult<obcx::telegram::bot::EditMessageTextResult>::success(
            {.message = request.message});
  }

  auto execute(const obcx::telegram::bot::SendTelegramPhotoRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> {
    const auto echo = next_echo();
    nlohmann::json payload{{"method", "sendPhoto"},
                           {"chat_id", request.target.native_group_id},
                           {"photo", request.photo},
                           {"echo", echo}};
    if (!request.caption.empty()) {
      payload["caption"] = request.caption;
    }
    if (!request.caption_entities.empty()) {
      payload["caption_entities"] = request.caption_entities;
    }
    const auto response =
        co_await transport().send_action(payload.dump(), echo);
    co_return telegram_send_result(
        parse_telegram_operation_response(response, true), request.target,
        "Telegram photo send", TelegramSendResultShape::SingleMessage);
  }

  auto execute(
      const obcx::telegram::bot::SendTelegramMediaGroupUrlsRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> {
    const auto echo = next_echo();
    std::vector<std::pair<std::string, std::string>> media;
    media.reserve(request.media.size());
    for (const auto &item : request.media) {
      media.emplace_back(item.type, item.source);
    }
    const auto reply = request.reply_to
                           ? std::optional{request.reply_to->native_message_id}
                           : std::nullopt;
    const auto payload =
        protocol().serialize_send_media_group_request_with_entities(
            request.target.native_group_id, media, request.caption,
            request.topic_id, reply, echo,
            telegram_entities(request.caption_entities));
    const auto response = co_await transport().send_action(payload, echo);
    co_return telegram_send_result(
        parse_telegram_operation_response(response, true), request.target,
        "Telegram URL media-group send", TelegramSendResultShape::MediaGroup,
        request.media.size());
  }

  auto execute(
      const obcx::telegram::bot::SendTelegramPhotoUploadRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> {
    if (uploader_ == nullptr) {
      co_return bot::failed_operation<bot::SendMessageResult>(
          bot::BotOperationErrorCode::UnsupportedAction,
          "Telegram multipart photo upload is unavailable");
    }
    const auto response = // NOLINT(clang-analyzer-core.CallAndMessage)
        co_await uploader_->upload(request);
    co_return telegram_send_result(
        parse_telegram_operation_response(response, true), request.target,
        "Telegram multipart photo send",
        TelegramSendResultShape::SingleMessage);
  }

  auto execute(
      const obcx::telegram::bot::SendTelegramMediaGroupUploadsRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> {
    if (uploader_ == nullptr) {
      co_return bot::failed_operation<bot::SendMessageResult>(
          bot::BotOperationErrorCode::UnsupportedAction,
          "Telegram multipart media-group upload is unavailable");
    }
    const auto response = // NOLINT(clang-analyzer-core.CallAndMessage)
        co_await uploader_->upload(request);
    co_return telegram_send_result(
        parse_telegram_operation_response(response, true), request.target,
        "Telegram multipart media-group send",
        TelegramSendResultShape::MediaGroup, request.media.size());
  }

  auto execute(const obcx::telegram::bot::FetchTelegramFileRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<obcx::telegram::bot::FetchedTelegramFile>> {
    const auto download_url =
        co_await transport().download_file(request.file.file_id);
    if (download_url.empty()) {
      co_return bot::failed_operation<obcx::telegram::bot::FetchedTelegramFile>(
          bot::BotOperationErrorCode::ProviderRejected,
          "Telegram did not resolve the requested file");
    }
    const auto content = co_await transport().download_file_content(
        download_url, request.maximum_bytes);
    if (content.size() > request.maximum_bytes) {
      co_return bot::failed_operation<obcx::telegram::bot::FetchedTelegramFile>(
          bot::BotOperationErrorCode::MediaTooLarge,
          "Telegram file exceeds the requested byte bound");
    }
    if (content.empty()) {
      co_return bot::failed_operation<obcx::telegram::bot::FetchedTelegramFile>(
          bot::BotOperationErrorCode::MalformedResponse,
          "Telegram returned an empty file");
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(content.size());
    for (const auto value : content) {
      bytes.push_back(
          static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
    }
    co_return bot::
        BotOperationResult<obcx::telegram::bot::FetchedTelegramFile>::success(
            {.installation = installation_,
             .file = request.file,
             .bytes = std::move(bytes)});
  }

private:
  [[nodiscard]] auto next_echo() noexcept -> std::uint64_t {
    return echo_.fetch_add(1, std::memory_order_relaxed);
  }
  [[nodiscard]] auto protocol() const -> adapter::telegram::ProtocolAdapter & {
    if (protocol_ == nullptr) {
      throw BotComponentRuntimeError("Telegram operations are not prepared");
    }
    return *protocol_;
  }
  [[nodiscard]] auto transport() const -> TelegramTransport & {
    if (transport_ == nullptr) {
      throw BotComponentRuntimeError("Telegram operations are not prepared");
    }
    return *transport_;
  }

  bot::BotInstallationRef installation_;
  std::shared_ptr<adapter::telegram::ProtocolAdapter> protocol_;
  std::shared_ptr<TelegramTransport> transport_;
  std::shared_ptr<TelegramMediaUploader> uploader_;
  std::atomic_uint64_t echo_{};
};

TelegramMediaUploadComponent::TelegramMediaUploadComponent()
    : uploader_(std::make_shared<Uploader>()) {}

auto TelegramMediaUploadComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"telegram.media-upload"},
      .provides = {CapabilityId{
          std::string{obcx::telegram::bot::capability_ids::media_upload}}},
      .required = {CapabilityId{
          std::string{obcx::telegram::bot::capability_ids::transport}}},
  };
}

void TelegramMediaUploadComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<TelegramMediaUploader>(
      ComponentId{"telegram.media-upload"},
      CapabilityId{
          std::string{obcx::telegram::bot::capability_ids::media_upload}},
      uploader_);
}

void TelegramMediaUploadComponent::prepare(const CapabilityRegistry &registry) {
  uploader_->configure(registry.get<TelegramTransport>(CapabilityId{
      std::string{obcx::telegram::bot::capability_ids::transport}}));
}

void TelegramMediaUploadComponent::start() {}
void TelegramMediaUploadComponent::stop() {}

TelegramOperationsComponent::TelegramOperationsComponent(
    std::string installation_id, bool include_upload)
    : endpoint_(std::make_shared<Endpoint>(std::move(installation_id))),
      operations_(
          std::make_shared<OperationRegistry>(endpoint_->installation())),
      include_upload_(include_upload) {}

auto TelegramOperationsComponent::descriptor() const -> ComponentDescriptor {
  std::vector<CapabilityId> dependencies;
  for (const auto &id :
       obcx::telegram::bot::operation_dependencies(include_upload_)) {
    dependencies.emplace_back(id);
  }
  return {.id = ComponentId{"telegram.operations"},
          .provides = {CapabilityId{"bot.operations"}},
          .required = std::move(dependencies)};
}

void TelegramOperationsComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<OperationRegistry>(ComponentId{"telegram.operations"},
                                      CapabilityId{"bot.operations"},
                                      operations_);
}

void TelegramOperationsComponent::prepare(const CapabilityRegistry &registry) {
  std::shared_ptr<TelegramMediaUploader> uploader;
  if (include_upload_) {
    uploader = registry.get<TelegramMediaUploader>(
        CapabilityId{"telegram.media-upload"});
  }
  endpoint_->configure(
      registry.get<adapter::telegram::ProtocolAdapter>(CapabilityId{
          std::string{obcx::telegram::bot::capability_ids::protocol}}),
      registry.get<TelegramTransport>(CapabilityId{
          std::string{obcx::telegram::bot::capability_ids::transport}}),
      std::move(uploader));
  obcx::telegram::bot::for_each_operation(
      include_upload_, [&](const auto &definition) {
        using Request =
            typename std::remove_cvref_t<decltype(definition)>::request_type;
        operations_->install(definition.bind(bind_operation_handler<Request>(
            endpoint_, obcx::telegram::bot::redact_diagnostic)));
      });
  std::vector<std::string> available;
  for (const auto &id :
       obcx::telegram::bot::operation_dependencies(include_upload_)) {
    if (registry.contains(CapabilityId{id})) {
      available.push_back(id);
    }
  }
  operations_->seal(endpoint_->installation(), available);
}

void TelegramOperationsComponent::start() {}
void TelegramOperationsComponent::stop() { operations_->close(); }

} // namespace obcx::core
