#ifndef OBCX_INCLUDE_TELEGRAM_BOT_OPERATIONS_HPP_
#define OBCX_INCLUDE_TELEGRAM_BOT_OPERATIONS_HPP_

#include "core/bot/operation_traits.hpp"
#include "telegram/bot/types.hpp"

namespace obcx::telegram::bot {

struct SendTelegramTopicMessageRequest {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document)
      -> SendTelegramTopicMessageRequest;

  inline static const auto &action = actions::send_topic;

  TelegramTopicTarget target;
  common::Message message;

  void validate() const {
    target.validate();
    detail::validate_message(message);
  }
};

inline void to_json(Json &document,
                    const SendTelegramTopicMessageRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"message", request.message}};
}

inline void from_json(const Json &document,
                      SendTelegramTopicMessageRequest &request) {
  detail::require_object(document, "SendTelegramTopicMessageRequest");
  if (document.contains("action") &&
      document.at("action").get<obcx::bot::ActionId>() != request.action) {
    throw std::invalid_argument(
        "SendTelegramTopicMessageRequest action mismatch");
  }
  if (!document.contains("target")) {
    throw std::invalid_argument(
        "SendTelegramTopicMessageRequest requires target");
  }
  request.target = document.at("target").get<TelegramTopicTarget>();
  request.message = detail::require_message(document, "message",
                                            "SendTelegramTopicMessageRequest");
  request.validate();
}

inline auto SendTelegramTopicMessageRequest::from_json(const Json &document)
    -> SendTelegramTopicMessageRequest {
  detail::require_object(document, "SendTelegramTopicMessageRequest");
  if (!document.contains("target")) {
    throw std::invalid_argument(
        "SendTelegramTopicMessageRequest requires target");
  }
  SendTelegramTopicMessageRequest result{
      .target = document.at("target").get<TelegramTopicTarget>()};
  obcx::telegram::bot::from_json(document, result);
  return result;
}

struct EditTelegramMessageTextRequest {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> EditTelegramMessageTextRequest;

  inline static const auto &action = actions::edit_text;

  BotMessageRef message;
  std::string text;
  std::string parse_mode;

  void validate() const {
    message.validate();
    if (message.group.installation.surface != surface) {
      throw std::invalid_argument(
          "EditTelegramMessageTextRequest requires telegram.bot_api");
    }
    detail::validate_identifier(text, "edit text", 65536);
    if (!parse_mode.empty()) {
      detail::validate_identifier(parse_mode, "parse_mode", 64);
    }
  }
};

inline void to_json(Json &document,
                    const EditTelegramMessageTextRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"message", request.message},
              {"text", request.text},
              {"parse_mode", request.parse_mode}};
}

inline void from_json(const Json &document,
                      EditTelegramMessageTextRequest &request) {
  detail::require_object(document, "EditTelegramMessageTextRequest");
  if (document.contains("action") &&
      document.at("action").get<obcx::bot::ActionId>() != request.action) {
    throw std::invalid_argument(
        "EditTelegramMessageTextRequest action mismatch");
  }
  if (!document.contains("message")) {
    throw std::invalid_argument(
        "EditTelegramMessageTextRequest requires message");
  }
  request.message = document.at("message").get<BotMessageRef>();
  request.text = detail::require_string(document, "text",
                                        "EditTelegramMessageTextRequest");
  request.parse_mode.clear();
  if (document.contains("parse_mode")) {
    request.parse_mode = detail::require_string(
        document, "parse_mode", "EditTelegramMessageTextRequest");
  }
  request.validate();
}

inline auto EditTelegramMessageTextRequest::from_json(const Json &document)
    -> EditTelegramMessageTextRequest {
  detail::require_object(document, "EditTelegramMessageTextRequest");
  if (!document.contains("message")) {
    throw std::invalid_argument(
        "EditTelegramMessageTextRequest requires message");
  }
  EditTelegramMessageTextRequest result{
      .message = document.at("message").get<BotMessageRef>()};
  obcx::telegram::bot::from_json(document, result);
  return result;
}

struct SendTelegramPhotoRequest {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> SendTelegramPhotoRequest;

  inline static const auto &action = actions::send_photo;

  GroupTarget target;
  std::string photo;
  std::string caption;
  std::vector<TelegramTextEntity> caption_entities;

  void validate() const {
    target.validate();
    detail::require_telegram(target.installation, "SendTelegramPhotoRequest");
    detail::validate_identifier(photo, "Telegram photo source", 16384);
    if (caption.size() > 65536U) {
      throw std::invalid_argument("Telegram photo caption exceeds SDK bound");
    }
    for (const auto &entity : caption_entities) {
      entity.validate();
    }
  }
};

inline void to_json(Json &document, const SendTelegramPhotoRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"photo", request.photo},
              {"caption", request.caption},
              {"caption_entities", request.caption_entities}};
}

inline void from_json(const Json &document, SendTelegramPhotoRequest &request) {
  detail::require_object(document, "SendTelegramPhotoRequest");
  if (document.contains("action") &&
      document.at("action").get<obcx::bot::ActionId>() != request.action) {
    throw std::invalid_argument("SendTelegramPhotoRequest action mismatch");
  }
  if (!document.contains("target")) {
    throw std::invalid_argument("SendTelegramPhotoRequest requires target");
  }
  request.target = document.at("target").get<GroupTarget>();
  request.photo =
      detail::require_string(document, "photo", "SendTelegramPhotoRequest");
  request.caption.clear();
  if (document.contains("caption")) {
    request.caption =
        detail::require_string(document, "caption", "SendTelegramPhotoRequest");
  }
  request.caption_entities.clear();
  if (document.contains("caption_entities")) {
    request.caption_entities =
        document.at("caption_entities").get<std::vector<TelegramTextEntity>>();
  }
  request.validate();
}

inline auto SendTelegramPhotoRequest::from_json(const Json &document)
    -> SendTelegramPhotoRequest {
  detail::require_object(document, "SendTelegramPhotoRequest");
  if (!document.contains("target")) {
    throw std::invalid_argument("SendTelegramPhotoRequest requires target");
  }
  SendTelegramPhotoRequest result{.target =
                                      document.at("target").get<GroupTarget>()};
  obcx::telegram::bot::from_json(document, result);
  return result;
}

struct SendTelegramPhotoUploadRequest {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> SendTelegramPhotoUploadRequest;

  inline static const auto &action = actions::send_photo_upload;

  GroupTarget target;
  TelegramMediaUpload photo;
  std::string caption;
  std::optional<std::int64_t> topic_id;
  std::size_t maximum_bytes{maximum_actor_media_bytes};

  void validate() const {
    target.validate();
    detail::require_telegram(target.installation,
                             "SendTelegramPhotoUploadRequest");
    detail::validate_media_bound(maximum_bytes,
                                 "SendTelegramPhotoUploadRequest");
    photo.validate(maximum_bytes);
    if (photo.type != "photo") {
      throw std::invalid_argument(
          "Telegram photo upload requires photo media type");
    }
    if (caption.size() > 65536U) {
      throw std::invalid_argument(
          "Telegram photo upload caption exceeds SDK bound");
    }
    detail::validate_optional_topic(topic_id, "SendTelegramPhotoUploadRequest");
  }
};

inline void to_json(Json &document,
                    const SendTelegramPhotoUploadRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"photo", request.photo},
              {"caption", request.caption},
              {"maximum_bytes", request.maximum_bytes}};
  if (request.topic_id.has_value()) {
    document["topic_id"] = *request.topic_id;
  }
}

inline void from_json(const Json &document,
                      SendTelegramPhotoUploadRequest &request) {
  detail::require_object(document, "SendTelegramPhotoUploadRequest");
  if (document.contains("action") &&
      document.at("action").get<obcx::bot::ActionId>() != request.action) {
    throw std::invalid_argument(
        "SendTelegramPhotoUploadRequest action mismatch");
  }
  if (!document.contains("target") || !document.contains("photo") ||
      !document.contains("maximum_bytes") ||
      !document.at("maximum_bytes").is_number_unsigned()) {
    throw std::invalid_argument("SendTelegramPhotoUploadRequest requires "
                                "target, photo, and maximum_bytes");
  }
  request.target = document.at("target").get<GroupTarget>();
  request.photo = document.at("photo").get<TelegramMediaUpload>();
  request.caption.clear();
  if (document.contains("caption")) {
    request.caption = detail::require_string(document, "caption",
                                             "SendTelegramPhotoUploadRequest");
  }
  request.topic_id = detail::optional_topic_from_json(document);
  request.maximum_bytes = document.at("maximum_bytes").get<std::size_t>();
  request.validate();
}

inline auto SendTelegramPhotoUploadRequest::from_json(const Json &document)
    -> SendTelegramPhotoUploadRequest {
  detail::require_object(document, "SendTelegramPhotoUploadRequest");
  if (!document.contains("target")) {
    throw std::invalid_argument(
        "SendTelegramPhotoUploadRequest requires target");
  }
  SendTelegramPhotoUploadRequest result{
      .target = document.at("target").get<GroupTarget>()};
  obcx::telegram::bot::from_json(document, result);
  return result;
}

struct SendTelegramMediaGroupUrlsRequest {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document)
      -> SendTelegramMediaGroupUrlsRequest;

  inline static const auto &action = actions::send_group_urls;

  GroupTarget target;
  std::vector<TelegramMediaSource> media;
  std::string caption;
  std::optional<std::int64_t> topic_id;
  std::optional<BotMessageRef> reply_to;
  std::vector<TelegramTextEntity> caption_entities;

  void validate() const {
    target.validate();
    detail::require_telegram(target.installation,
                             "SendTelegramMediaGroupUrlsRequest");
    if (media.size() < 2U || media.size() > 10U) {
      throw std::invalid_argument(
          "Telegram URL media group requires between 2 and 10 items");
    }
    for (const auto &item : media) {
      item.validate();
    }
    if (caption.size() > 65536U) {
      throw std::invalid_argument(
          "Telegram media-group caption exceeds SDK bound");
    }
    detail::validate_optional_topic(topic_id,
                                    "SendTelegramMediaGroupUrlsRequest");
    if (reply_to.has_value()) {
      reply_to->validate();
      if (reply_to->group != target) {
        throw std::invalid_argument(
            "Telegram media-group reply must belong to its target group");
      }
    }
    for (const auto &entity : caption_entities) {
      entity.validate();
    }
  }
};

inline void to_json(Json &document,
                    const SendTelegramMediaGroupUrlsRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"media", request.media},
              {"caption", request.caption},
              {"caption_entities", request.caption_entities}};
  if (request.topic_id.has_value()) {
    document["topic_id"] = *request.topic_id;
  }
  if (request.reply_to.has_value()) {
    document["reply_to"] = *request.reply_to;
  }
}

inline void from_json(const Json &document,
                      SendTelegramMediaGroupUrlsRequest &request) {
  detail::require_object(document, "SendTelegramMediaGroupUrlsRequest");
  if (document.contains("action") &&
      document.at("action").get<obcx::bot::ActionId>() != request.action) {
    throw std::invalid_argument(
        "SendTelegramMediaGroupUrlsRequest action mismatch");
  }
  if (!document.contains("target") || !document.contains("media") ||
      !document.at("media").is_array()) {
    throw std::invalid_argument(
        "SendTelegramMediaGroupUrlsRequest requires target and media");
  }
  request.target = document.at("target").get<GroupTarget>();
  request.media = document.at("media").get<std::vector<TelegramMediaSource>>();
  request.caption.clear();
  if (document.contains("caption")) {
    request.caption = detail::require_string(
        document, "caption", "SendTelegramMediaGroupUrlsRequest");
  }
  request.topic_id = detail::optional_topic_from_json(document);
  request.reply_to = detail::optional_reply_from_json(document);
  request.caption_entities.clear();
  if (document.contains("caption_entities")) {
    request.caption_entities =
        document.at("caption_entities").get<std::vector<TelegramTextEntity>>();
  }
  request.validate();
}

inline auto SendTelegramMediaGroupUrlsRequest::from_json(const Json &document)
    -> SendTelegramMediaGroupUrlsRequest {
  detail::require_object(document, "SendTelegramMediaGroupUrlsRequest");
  if (!document.contains("target")) {
    throw std::invalid_argument(
        "SendTelegramMediaGroupUrlsRequest requires target");
  }
  SendTelegramMediaGroupUrlsRequest result{
      .target = document.at("target").get<GroupTarget>()};
  obcx::telegram::bot::from_json(document, result);
  return result;
}

struct SendTelegramMediaGroupUploadsRequest {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document)
      -> SendTelegramMediaGroupUploadsRequest;

  inline static const auto &action = actions::send_group_uploads;

  GroupTarget target;
  std::vector<TelegramMediaUpload> media;
  std::string caption;
  std::optional<std::int64_t> topic_id;
  std::optional<BotMessageRef> reply_to;
  std::vector<TelegramTextEntity> caption_entities;
  std::size_t maximum_bytes{maximum_actor_media_bytes};

  void validate() const {
    target.validate();
    detail::require_telegram(target.installation,
                             "SendTelegramMediaGroupUploadsRequest");
    detail::validate_media_bound(maximum_bytes,
                                 "SendTelegramMediaGroupUploadsRequest");
    if (media.size() < 2U || media.size() > 10U) {
      throw std::invalid_argument(
          "Telegram upload media group requires between 2 and 10 items");
    }
    std::size_t total = 0;
    for (const auto &item : media) {
      item.validate(maximum_bytes);
      if (item.bytes.size() > maximum_bytes - total) {
        throw std::invalid_argument(
            "Telegram uploads exceed request byte bound");
      }
      total += item.bytes.size();
    }
    if (caption.size() > 65536U) {
      throw std::invalid_argument(
          "Telegram media-group caption exceeds SDK bound");
    }
    detail::validate_optional_topic(topic_id,
                                    "SendTelegramMediaGroupUploadsRequest");
    if (reply_to.has_value()) {
      reply_to->validate();
      if (reply_to->group != target) {
        throw std::invalid_argument(
            "Telegram media-group reply must belong to its target group");
      }
    }
    for (const auto &entity : caption_entities) {
      entity.validate();
    }
  }
};

inline void to_json(Json &document,
                    const SendTelegramMediaGroupUploadsRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"media", request.media},
              {"caption", request.caption},
              {"caption_entities", request.caption_entities},
              {"maximum_bytes", request.maximum_bytes}};
  if (request.topic_id.has_value()) {
    document["topic_id"] = *request.topic_id;
  }
  if (request.reply_to.has_value()) {
    document["reply_to"] = *request.reply_to;
  }
}

inline void from_json(const Json &document,
                      SendTelegramMediaGroupUploadsRequest &request) {
  detail::require_object(document, "SendTelegramMediaGroupUploadsRequest");
  if (document.contains("action") &&
      document.at("action").get<obcx::bot::ActionId>() != request.action) {
    throw std::invalid_argument(
        "SendTelegramMediaGroupUploadsRequest action mismatch");
  }
  if (!document.contains("target") || !document.contains("media") ||
      !document.at("media").is_array() || !document.contains("maximum_bytes") ||
      !document.at("maximum_bytes").is_number_unsigned()) {
    throw std::invalid_argument("SendTelegramMediaGroupUploadsRequest requires "
                                "target, media, and maximum_bytes");
  }
  request.target = document.at("target").get<GroupTarget>();
  request.media = document.at("media").get<std::vector<TelegramMediaUpload>>();
  request.caption.clear();
  if (document.contains("caption")) {
    request.caption = detail::require_string(
        document, "caption", "SendTelegramMediaGroupUploadsRequest");
  }
  request.topic_id = detail::optional_topic_from_json(document);
  request.reply_to = detail::optional_reply_from_json(document);
  request.caption_entities.clear();
  if (document.contains("caption_entities")) {
    request.caption_entities =
        document.at("caption_entities").get<std::vector<TelegramTextEntity>>();
  }
  request.maximum_bytes = document.at("maximum_bytes").get<std::size_t>();
  request.validate();
}

inline auto SendTelegramMediaGroupUploadsRequest::from_json(
    const Json &document) -> SendTelegramMediaGroupUploadsRequest {
  detail::require_object(document, "SendTelegramMediaGroupUploadsRequest");
  if (!document.contains("target")) {
    throw std::invalid_argument(
        "SendTelegramMediaGroupUploadsRequest requires target");
  }
  SendTelegramMediaGroupUploadsRequest result{
      .target = document.at("target").get<GroupTarget>()};
  obcx::telegram::bot::from_json(document, result);
  return result;
}

struct FetchTelegramFileRequest {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> FetchTelegramFileRequest;

  inline static const auto &action = actions::fetch_file;

  BotInstallationRef installation;
  TelegramFileRef file;
  std::size_t maximum_bytes{};

  void validate() const {
    detail::require_telegram(installation, "FetchTelegramFileRequest");
    file.validate();
    detail::validate_media_bound(maximum_bytes, "FetchTelegramFileRequest");
    if (file.file_size.has_value() &&
        static_cast<std::uint64_t>(*file.file_size) > maximum_bytes) {
      throw std::invalid_argument(
          "Telegram declared file size exceeds request byte bound");
    }
  }
};

inline void to_json(Json &document, const FetchTelegramFileRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"installation", request.installation},
              {"file", request.file},
              {"maximum_bytes", request.maximum_bytes}};
}

inline void from_json(const Json &document, FetchTelegramFileRequest &request) {
  detail::require_object(document, "FetchTelegramFileRequest");
  if (document.contains("action") &&
      document.at("action").get<obcx::bot::ActionId>() != request.action) {
    throw std::invalid_argument("FetchTelegramFileRequest action mismatch");
  }
  if (!document.contains("installation") || !document.contains("file") ||
      !document.contains("maximum_bytes") ||
      !document.at("maximum_bytes").is_number_unsigned()) {
    throw std::invalid_argument("FetchTelegramFileRequest requires "
                                "installation, file, and maximum_bytes");
  }
  request.installation = document.at("installation").get<BotInstallationRef>();
  request.file = document.at("file").get<TelegramFileRef>();
  request.maximum_bytes = document.at("maximum_bytes").get<std::size_t>();
  request.validate();
}

inline auto FetchTelegramFileRequest::from_json(const Json &document)
    -> FetchTelegramFileRequest {
  detail::require_object(document, "FetchTelegramFileRequest");
  if (!document.contains("installation")) {
    throw std::invalid_argument(
        "FetchTelegramFileRequest requires installation");
  }
  FetchTelegramFileRequest result{
      .installation = document.at("installation").get<BotInstallationRef>()};
  obcx::telegram::bot::from_json(document, result);
  return result;
}

} // namespace obcx::telegram::bot

namespace obcx::bot {

template <>
struct OperationTraits<telegram::bot::SendTelegramTopicMessageRequest>
    : OperationContract<telegram::bot::SendTelegramTopicMessageRequest,
                        obcx::bot::SendMessageResult, true> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == telegram::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.target.group.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.messages.size() != 1U ||
        result.primary().group != request.target.group) {
      throw std::invalid_argument(
          "Telegram result does not match its requested scope or bound");
    }
  }
};

template <>
struct OperationTraits<telegram::bot::EditTelegramMessageTextRequest>
    : OperationContract<telegram::bot::EditTelegramMessageTextRequest,
                        telegram::bot::EditMessageTextResult, true> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == telegram::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.message.group.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.message != request.message) {
      throw std::invalid_argument(
          "Telegram result does not match its requested scope or bound");
    }
  }
};

template <>
struct OperationTraits<telegram::bot::SendTelegramPhotoRequest>
    : OperationContract<telegram::bot::SendTelegramPhotoRequest,
                        obcx::bot::SendMessageResult, true> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == telegram::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.target.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.messages.size() != 1U ||
        result.primary().group != request.target) {
      throw std::invalid_argument(
          "Telegram result does not match its requested scope or bound");
    }
  }
};

template <>
struct OperationTraits<telegram::bot::SendTelegramPhotoUploadRequest>
    : OperationContract<telegram::bot::SendTelegramPhotoUploadRequest,
                        obcx::bot::SendMessageResult, true> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == telegram::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.target.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.messages.size() != 1U ||
        result.primary().group != request.target) {
      throw std::invalid_argument(
          "Telegram result does not match its requested scope or bound");
    }
  }
};

template <>
struct OperationTraits<telegram::bot::SendTelegramMediaGroupUrlsRequest>
    : OperationContract<telegram::bot::SendTelegramMediaGroupUrlsRequest,
                        obcx::bot::SendMessageResult, true> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == telegram::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.target.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.messages.size() != request.media.size() ||
        result.primary().group != request.target) {
      throw std::invalid_argument(
          "Telegram result does not match its requested scope or bound");
    }
  }
};

template <>
struct OperationTraits<telegram::bot::SendTelegramMediaGroupUploadsRequest>
    : OperationContract<telegram::bot::SendTelegramMediaGroupUploadsRequest,
                        obcx::bot::SendMessageResult, true> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == telegram::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.target.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.messages.size() != request.media.size() ||
        result.primary().group != request.target) {
      throw std::invalid_argument(
          "Telegram result does not match its requested scope or bound");
    }
  }
};

template <>
struct OperationTraits<telegram::bot::FetchTelegramFileRequest>
    : OperationContract<telegram::bot::FetchTelegramFileRequest,
                        telegram::bot::FetchedTelegramFile, false> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == telegram::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.installation != request.installation ||
        result.file.file_id != request.file.file_id ||
        result.bytes.size() > request.maximum_bytes) {
      throw std::invalid_argument(
          "Telegram result does not match its requested scope or bound");
    }
  }
};

} // namespace obcx::bot

namespace obcx::bot {

template <> struct GatewayCodec<telegram::bot::SendTelegramPhotoUploadRequest> {
  using Value = telegram::bot::SendTelegramPhotoUploadRequest;

  static auto encode(Value &value) -> Json {
    value.validate();
    Json payload{{"action", value.action},
                 {"target", value.target},
                 {"caption", value.caption},
                 {"maximum_bytes", value.maximum_bytes}};
    if (value.topic_id) {
      payload["topic_id"] = *value.topic_id;
    }
    payload["photo"] =
        GatewayCodec<telegram::bot::TelegramMediaUpload>::encode(value.photo);
    return payload;
  }

  static auto decode(Json payload) -> Value {
    detail::require_object(payload, "Telegram photo upload gateway payload");
    if (payload.contains("action") &&
        payload.at("action").get<ActionId>() != Value::action) {
      throw std::invalid_argument(
          "Telegram photo upload gateway action mismatch");
    }
    if (!payload.contains("maximum_bytes") ||
        !payload.at("maximum_bytes").is_number_unsigned() ||
        !payload.contains("photo")) {
      throw std::invalid_argument("Telegram photo upload gateway requires "
                                  "photo and maximum_bytes");
    }
    const auto maximum = payload.at("maximum_bytes").get<std::size_t>();
    telegram::bot::detail::validate_media_bound(
        maximum, "Telegram photo upload gateway");
    (void)telegram::bot::detail::gateway_binary_size(payload.at("photo"),
                                                     "bytes", maximum);
    Value result{
        .target = payload.at("target").get<GroupTarget>(),
        .photo = GatewayCodec<telegram::bot::TelegramMediaUpload>::decode(
            std::move(payload.at("photo"))),
        .caption = payload.value("caption", std::string{}),
        .topic_id = telegram::bot::detail::optional_topic_from_json(payload),
        .maximum_bytes = maximum};
    result.validate();
    return result;
  }
};

template <>
struct GatewayCodec<telegram::bot::SendTelegramMediaGroupUploadsRequest> {
  using Value = telegram::bot::SendTelegramMediaGroupUploadsRequest;

  static auto encode(Value &value) -> Json {
    value.validate();
    Json payload{{"action", value.action},
                 {"target", value.target},
                 {"caption", value.caption},
                 {"caption_entities", value.caption_entities},
                 {"maximum_bytes", value.maximum_bytes},
                 {"media", Json::array()}};
    if (value.topic_id) {
      payload["topic_id"] = *value.topic_id;
    }
    if (value.reply_to) {
      payload["reply_to"] = *value.reply_to;
    }
    for (auto &item : value.media) {
      payload["media"].push_back(
          GatewayCodec<telegram::bot::TelegramMediaUpload>::encode(item));
    }
    return payload;
  }

  static auto decode(Json payload) -> Value {
    detail::require_object(payload, "Telegram upload gateway payload");
    if (payload.contains("action") &&
        payload.at("action").get<ActionId>() != Value::action) {
      throw std::invalid_argument("Telegram upload gateway action mismatch");
    }
    if (!payload.contains("maximum_bytes") ||
        !payload.at("maximum_bytes").is_number_unsigned() ||
        !payload.contains("media") || !payload.at("media").is_array()) {
      throw std::invalid_argument(
          "Telegram upload gateway requires media and maximum_bytes");
    }
    const auto maximum = payload.at("maximum_bytes").get<std::size_t>();
    telegram::bot::detail::validate_media_bound(maximum,
                                                "Telegram upload gateway");
    auto &media = payload.at("media");
    if (media.size() < 2U || media.size() > 10U) {
      throw std::invalid_argument(
          "Telegram upload gateway requires 2..10 items");
    }
    std::size_t total = 0;
    for (const auto &item : media) {
      const auto size =
          telegram::bot::detail::gateway_binary_size(item, "bytes", maximum);
      if (size > maximum - total) {
        throw std::invalid_argument(
            "Telegram gateway uploads exceed request byte bound");
      }
      total += size;
    }
    // The complete byte set is checked before moving/copying any buffer.
    Value result{
        .target = payload.at("target").get<GroupTarget>(),
        .caption = payload.value("caption", std::string{}),
        .topic_id = telegram::bot::detail::optional_topic_from_json(payload),
        .reply_to = telegram::bot::detail::optional_reply_from_json(payload),
        .caption_entities =
            payload.value("caption_entities",
                          std::vector<telegram::bot::TelegramTextEntity>{}),
        .maximum_bytes = maximum};
    result.media.reserve(media.size());
    for (auto &item : media) {
      result.media.push_back(
          GatewayCodec<telegram::bot::TelegramMediaUpload>::decode(
              std::move(item)));
    }
    result.validate();
    return result;
  }
};

} // namespace obcx::bot

#endif // OBCX_INCLUDE_TELEGRAM_BOT_OPERATIONS_HPP_
