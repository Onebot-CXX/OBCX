#ifndef OBCX_INCLUDE_TELEGRAM_BOT_TYPES_HPP_
#define OBCX_INCLUDE_TELEGRAM_BOT_TYPES_HPP_

#include "core/bot/gateway_codec.hpp"
#include "core/bot/messaging.hpp"
#include "telegram/bot/actions.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace obcx::telegram::bot {

using obcx::bot::BotInstallationRef;
using obcx::bot::BotMessageRef;
using obcx::bot::GroupTarget;
using obcx::bot::Json;

namespace detail {
using obcx::bot::detail::require_message;
using obcx::bot::detail::require_object;
using obcx::bot::detail::require_string;
using obcx::bot::detail::validate_identifier;
using obcx::bot::detail::validate_message;
} // namespace detail

inline constexpr std::size_t maximum_actor_media_bytes =
    std::size_t{128} * 1024U * 1024U;

namespace detail {

inline void require_telegram(const BotInstallationRef &installation,
                             const std::string_view type) {
  installation.validate();
  if (installation.surface != surface) {
    throw std::invalid_argument(std::string{type} +
                                " requires telegram.bot_api");
  }
}

inline void validate_optional_topic(const std::optional<std::int64_t> topic_id,
                                    const std::string_view type) {
  if (topic_id.has_value() && *topic_id <= 0) {
    throw std::invalid_argument(std::string{type} +
                                " topic_id must be positive");
  }
}

inline void validate_media_bound(const std::size_t maximum_bytes,
                                 const std::string_view type) {
  if (maximum_bytes == 0 || maximum_bytes > maximum_actor_media_bytes) {
    throw std::invalid_argument(std::string{type} +
                                " maximum_bytes is outside the SDK bound");
  }
}

inline auto optional_topic_from_json(const Json &document)
    -> std::optional<std::int64_t> {
  if (!document.contains("topic_id") || document.at("topic_id").is_null()) {
    return std::nullopt;
  }
  if (!document.at("topic_id").is_number_integer()) {
    throw std::invalid_argument("topic_id must be an integer");
  }
  return document.at("topic_id").get<std::int64_t>();
}

inline auto optional_reply_from_json(const Json &document)
    -> std::optional<BotMessageRef> {
  if (!document.contains("reply_to") || document.at("reply_to").is_null()) {
    return std::nullopt;
  }
  if (!document.at("reply_to").is_object()) {
    throw std::invalid_argument("reply_to must be a BotMessageRef");
  }
  return document.at("reply_to").get<BotMessageRef>();
}

} // namespace detail

struct TelegramTopicTarget {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> TelegramTopicTarget;

  GroupTarget group;
  std::int64_t topic_id{};

  void validate() const {
    group.validate();
    if (group.installation.surface != surface) {
      throw std::invalid_argument(
          "TelegramTopicTarget requires telegram.bot_api");
    }
    if (topic_id <= 0) {
      throw std::invalid_argument("Telegram topic_id must be positive");
    }
  }

  auto operator==(const TelegramTopicTarget &) const -> bool = default;
};

inline void to_json(Json &document, const TelegramTopicTarget &target) {
  target.validate();
  document = {{"group", target.group}, {"topic_id", target.topic_id}};
}

inline void from_json(const Json &document, TelegramTopicTarget &target) {
  detail::require_object(document, "TelegramTopicTarget");
  if (!document.contains("group") || !document.at("group").is_object()) {
    throw std::invalid_argument("TelegramTopicTarget requires group");
  }
  if (!document.contains("topic_id") ||
      !document.at("topic_id").is_number_integer()) {
    throw std::invalid_argument(
        "TelegramTopicTarget requires integer topic_id");
  }
  target.group = document.at("group").get<GroupTarget>();
  target.topic_id = document.at("topic_id").get<std::int64_t>();
  target.validate();
}

inline auto TelegramTopicTarget::from_json(const Json &document)
    -> TelegramTopicTarget {
  detail::require_object(document, "TelegramTopicTarget");
  if (!document.contains("group")) {
    throw std::invalid_argument("TelegramTopicTarget requires group");
  }
  TelegramTopicTarget result{.group = document.at("group").get<GroupTarget>()};
  obcx::telegram::bot::from_json(document, result);
  return result;
}

struct EditMessageTextResult {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> EditMessageTextResult;

  BotMessageRef message;

  void validate() const {
    message.validate();
    if (message.group.installation.surface != surface) {
      throw std::invalid_argument(
          "EditMessageTextResult requires telegram.bot_api");
    }
  }

  auto operator==(const EditMessageTextResult &) const -> bool = default;
};

inline void to_json(Json &document, const EditMessageTextResult &result) {
  result.validate();
  document = {{"message", result.message}};
}

inline void from_json(const Json &document, EditMessageTextResult &result) {
  detail::require_object(document, "EditMessageTextResult");
  if (!document.contains("message")) {
    throw std::invalid_argument("EditMessageTextResult requires message");
  }
  result.message = document.at("message").get<BotMessageRef>();
  result.validate();
}

inline auto EditMessageTextResult::from_json(const Json &document)
    -> EditMessageTextResult {
  detail::require_object(document, "EditMessageTextResult");
  if (!document.contains("message")) {
    throw std::invalid_argument("EditMessageTextResult requires message");
  }
  EditMessageTextResult result{.message =
                                   document.at("message").get<BotMessageRef>()};
  obcx::telegram::bot::from_json(document, result);
  return result;
}

struct TelegramTextEntity {
  std::string type;
  std::size_t offset{};
  std::size_t length{};

  void validate() const {
    detail::validate_identifier(type, "Telegram entity type", 128);
    if (length == 0) {
      throw std::invalid_argument("Telegram entity length must be positive");
    }
    if (offset > std::numeric_limits<std::size_t>::max() - length) {
      throw std::invalid_argument("Telegram entity range overflows");
    }
  }

  auto operator==(const TelegramTextEntity &) const -> bool = default;
};

inline void to_json(Json &document, const TelegramTextEntity &entity) {
  entity.validate();
  document = {{"type", entity.type},
              {"offset", entity.offset},
              {"length", entity.length}};
}

inline void from_json(const Json &document, TelegramTextEntity &entity) {
  detail::require_object(document, "TelegramTextEntity");
  entity.type = detail::require_string(document, "type", "TelegramTextEntity");
  if (!document.contains("offset") ||
      !document.at("offset").is_number_unsigned() ||
      !document.contains("length") ||
      !document.at("length").is_number_unsigned()) {
    throw std::invalid_argument(
        "TelegramTextEntity requires unsigned offset and length");
  }
  entity.offset = document.at("offset").get<std::size_t>();
  entity.length = document.at("length").get<std::size_t>();
  entity.validate();
}

struct TelegramMediaSource {
  std::string type;
  std::string source;

  void validate() const {
    detail::validate_identifier(type, "Telegram media type", 64);
    detail::validate_identifier(source, "Telegram media source", 16384);
  }

  auto operator==(const TelegramMediaSource &) const -> bool = default;
};

inline void to_json(Json &document, const TelegramMediaSource &media) {
  media.validate();
  document = {{"type", media.type}, {"source", media.source}};
}

inline void from_json(const Json &document, TelegramMediaSource &media) {
  detail::require_object(document, "TelegramMediaSource");
  media.type = detail::require_string(document, "type", "TelegramMediaSource");
  media.source =
      detail::require_string(document, "source", "TelegramMediaSource");
  media.validate();
}

struct TelegramMediaUpload {
  std::string type;
  std::string filename;
  std::string mime_type;
  std::vector<std::uint8_t> bytes;

  void validate() const { validate(maximum_actor_media_bytes); }

  void validate(const std::size_t maximum_bytes) const {
    detail::validate_identifier(type, "Telegram upload type", 64);
    detail::validate_identifier(filename, "Telegram upload filename", 512);
    detail::validate_identifier(mime_type, "Telegram upload MIME type", 256);
    if (bytes.empty()) {
      throw std::invalid_argument("Telegram upload bytes cannot be empty");
    }
    if (bytes.size() > maximum_bytes) {
      throw std::invalid_argument("Telegram upload exceeds request byte bound");
    }
  }

  auto operator==(const TelegramMediaUpload &) const -> bool = default;
};

inline void to_json(Json &document, const TelegramMediaUpload &media) {
  media.validate();
  document = {{"type", media.type},
              {"filename", media.filename},
              {"mime_type", media.mime_type},
              {"bytes", media.bytes}};
}

inline void from_json(const Json &document, TelegramMediaUpload &media) {
  detail::require_object(document, "TelegramMediaUpload");
  media.type = detail::require_string(document, "type", "TelegramMediaUpload");
  media.filename =
      detail::require_string(document, "filename", "TelegramMediaUpload");
  media.mime_type =
      detail::require_string(document, "mime_type", "TelegramMediaUpload");
  if (!document.contains("bytes") || !document.at("bytes").is_array()) {
    throw std::invalid_argument("TelegramMediaUpload requires bytes array");
  }
  media.bytes = document.at("bytes").get<std::vector<std::uint8_t>>();
  media.validate();
}

struct TelegramFileRef {
  std::string file_id;
  std::string file_unique_id;
  std::string file_type;
  std::optional<std::int64_t> file_size;
  std::optional<std::string> mime_type;
  std::optional<std::string> file_name;

  void validate() const {
    detail::validate_identifier(file_id, "Telegram file_id", 2048);
    if (!file_unique_id.empty()) {
      detail::validate_identifier(file_unique_id, "Telegram file_unique_id",
                                  2048);
    }
    detail::validate_identifier(file_type, "Telegram file_type", 64);
    if (file_size.has_value() && *file_size < 0) {
      throw std::invalid_argument("Telegram file_size cannot be negative");
    }
    if (mime_type.has_value()) {
      detail::validate_identifier(*mime_type, "Telegram file MIME type", 256);
    }
    if (file_name.has_value()) {
      detail::validate_identifier(*file_name, "Telegram file name", 512);
    }
  }

  auto operator==(const TelegramFileRef &) const -> bool = default;
};

inline void to_json(Json &document, const TelegramFileRef &file) {
  file.validate();
  document = {{"file_id", file.file_id},
              {"file_unique_id", file.file_unique_id},
              {"file_type", file.file_type}};
  if (file.file_size.has_value()) {
    document["file_size"] = *file.file_size;
  }
  if (file.mime_type.has_value()) {
    document["mime_type"] = *file.mime_type;
  }
  if (file.file_name.has_value()) {
    document["file_name"] = *file.file_name;
  }
}

inline void from_json(const Json &document, TelegramFileRef &file) {
  detail::require_object(document, "TelegramFileRef");
  file.file_id = detail::require_string(document, "file_id", "TelegramFileRef");
  file.file_unique_id.clear();
  if (document.contains("file_unique_id")) {
    file.file_unique_id =
        detail::require_string(document, "file_unique_id", "TelegramFileRef");
  }
  file.file_type =
      detail::require_string(document, "file_type", "TelegramFileRef");
  file.file_size.reset();
  if (document.contains("file_size")) {
    if (!document.at("file_size").is_number_integer()) {
      throw std::invalid_argument("TelegramFileRef file_size must be integer");
    }
    file.file_size = document.at("file_size").get<std::int64_t>();
  }
  file.mime_type.reset();
  if (document.contains("mime_type")) {
    file.mime_type =
        detail::require_string(document, "mime_type", "TelegramFileRef");
  }
  file.file_name.reset();
  if (document.contains("file_name")) {
    file.file_name =
        detail::require_string(document, "file_name", "TelegramFileRef");
  }
  file.validate();
}

struct FetchedTelegramFile {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> FetchedTelegramFile;

  BotInstallationRef installation;
  TelegramFileRef file;
  std::vector<std::uint8_t> bytes;

  void validate() const {
    detail::require_telegram(installation, "FetchedTelegramFile");
    file.validate();
    if (bytes.empty()) {
      throw std::invalid_argument("fetched Telegram file cannot be empty");
    }
    if (bytes.size() > maximum_actor_media_bytes) {
      throw std::invalid_argument("fetched Telegram file exceeds SDK bound");
    }
  }

  auto operator==(const FetchedTelegramFile &) const -> bool = default;
};

inline void to_json(Json &document, const FetchedTelegramFile &file) {
  file.validate();
  document = {{"installation", file.installation},
              {"file", file.file},
              {"bytes", file.bytes}};
}

inline void from_json(const Json &document, FetchedTelegramFile &file) {
  detail::require_object(document, "FetchedTelegramFile");
  if (!document.contains("installation") || !document.contains("file") ||
      !document.contains("bytes") || !document.at("bytes").is_array()) {
    throw std::invalid_argument(
        "FetchedTelegramFile requires installation, file, and bytes array");
  }
  file.installation = document.at("installation").get<BotInstallationRef>();
  file.file = document.at("file").get<TelegramFileRef>();
  file.bytes = document.at("bytes").get<std::vector<std::uint8_t>>();
  file.validate();
}

inline auto FetchedTelegramFile::from_json(const Json &document)
    -> FetchedTelegramFile {
  detail::require_object(document, "FetchedTelegramFile");
  if (!document.contains("installation")) {
    throw std::invalid_argument("FetchedTelegramFile requires installation");
  }
  FetchedTelegramFile result{
      .installation = document.at("installation").get<BotInstallationRef>()};
  obcx::telegram::bot::from_json(document, result);
  return result;
}

} // namespace obcx::telegram::bot

namespace obcx::telegram::bot::detail {

inline auto gateway_binary_size(const Json &document, const std::string &field,
                                const std::size_t maximum_bytes)
    -> std::size_t {
  if (!document.is_object() || !document.contains(field) ||
      !document.at(field).is_binary()) {
    throw std::invalid_argument("gateway media requires a binary byte field");
  }
  const auto &binary = document.at(field).get_binary();
  if (binary.has_subtype() || binary.empty() || binary.size() > maximum_bytes) {
    throw std::invalid_argument(
        "gateway media binary exceeds its bound or is invalid");
  }
  return binary.size();
}

inline auto take_gateway_binary(Json &document, const std::string &field,
                                const std::size_t maximum_bytes)
    -> std::vector<std::uint8_t> {
  (void)gateway_binary_size(document, field, maximum_bytes);
  return std::move(document.at(field).get_binary());
}

} // namespace obcx::telegram::bot::detail

namespace obcx::bot {

template <> struct GatewayCodec<telegram::bot::TelegramMediaUpload> {
  using Value = telegram::bot::TelegramMediaUpload;
  static auto encode(Value &value) -> Json {
    value.validate();
    Json payload{{"type", value.type},
                 {"filename", value.filename},
                 {"mime_type", value.mime_type}};
    payload["bytes"] = Json::binary(std::move(value.bytes));
    return payload;
  }
  static auto decode(Json payload) -> Value {
    detail::require_object(payload, "TelegramMediaUpload gateway payload");
    Value result{
        .type = detail::require_string(payload, "type", "TelegramMediaUpload"),
        .filename =
            detail::require_string(payload, "filename", "TelegramMediaUpload"),
        .mime_type =
            detail::require_string(payload, "mime_type", "TelegramMediaUpload"),
        .bytes = telegram::bot::detail::take_gateway_binary(
            payload, "bytes", telegram::bot::maximum_actor_media_bytes)};
    result.validate();
    return result;
  }
};

template <> struct GatewayCodec<telegram::bot::FetchedTelegramFile> {
  using Value = telegram::bot::FetchedTelegramFile;
  static auto encode(Value &value) -> Json {
    value.validate();
    Json payload{{"installation", value.installation}, {"file", value.file}};
    payload["bytes"] = Json::binary(std::move(value.bytes));
    return payload;
  }
  static auto decode(Json payload) -> Value {
    detail::require_object(payload, "FetchedTelegramFile gateway payload");
    Value result{
        .installation = payload.at("installation").get<BotInstallationRef>(),
        .file = payload.at("file").get<telegram::bot::TelegramFileRef>(),
        .bytes = telegram::bot::detail::take_gateway_binary(
            payload, "bytes", telegram::bot::maximum_actor_media_bytes)};
    result.validate();
    return result;
  }
};

} // namespace obcx::bot

#endif // OBCX_INCLUDE_TELEGRAM_BOT_TYPES_HPP_
