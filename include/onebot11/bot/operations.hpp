#ifndef OBCX_INCLUDE_ONEBOT11_BOT_OPERATIONS_HPP_
#define OBCX_INCLUDE_ONEBOT11_BOT_OPERATIONS_HPP_

#include "core/bot/operation_traits.hpp"
#include "onebot11/bot/types.hpp"

namespace obcx::onebot11::bot {

struct GetOneBotGroupMemberRequest {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> GetOneBotGroupMemberRequest;

  inline static const auto &action = actions::get_group_member;

  GroupTarget target;
  std::string user_id;
  bool no_cache{};

  void validate() const {
    target.validate();
    detail::require_onebot(target.installation, "GetOneBotGroupMemberRequest");
    detail::validate_identifier(user_id, "OneBot user_id");
  }
};

inline void to_json(Json &document,
                    const GetOneBotGroupMemberRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"user_id", request.user_id},
              {"no_cache", request.no_cache}};
}

inline void from_json(const Json &document,
                      GetOneBotGroupMemberRequest &request) {
  detail::require_object(document, "GetOneBotGroupMemberRequest");
  if (document.contains("action") &&
      document.at("action").get<obcx::bot::ActionId>() != request.action) {
    throw std::invalid_argument("GetOneBotGroupMemberRequest action mismatch");
  }
  if (!document.contains("target")) {
    throw std::invalid_argument("GetOneBotGroupMemberRequest requires target");
  }
  request.target = document.at("target").get<GroupTarget>();
  request.user_id = detail::require_string(document, "user_id",
                                           "GetOneBotGroupMemberRequest");
  request.no_cache = false;
  if (document.contains("no_cache")) {
    if (!document.at("no_cache").is_boolean()) {
      throw std::invalid_argument("no_cache must be boolean");
    }
    request.no_cache = document.at("no_cache").get<bool>();
  }
  request.validate();
}

inline auto GetOneBotGroupMemberRequest::from_json(const Json &document)
    -> GetOneBotGroupMemberRequest {
  detail::require_object(document, "GetOneBotGroupMemberRequest");
  if (!document.contains("target")) {
    throw std::invalid_argument("GetOneBotGroupMemberRequest requires target");
  }
  GetOneBotGroupMemberRequest result{
      .target = document.at("target").get<GroupTarget>()};
  obcx::onebot11::bot::from_json(document, result);
  return result;
}

struct GetOneBotForwardMessageRequest {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> GetOneBotForwardMessageRequest;

  inline static const auto &action = actions::get_forward_message;

  BotInstallationRef installation;
  std::string forward_id;

  void validate() const {
    detail::require_onebot(installation, "GetOneBotForwardMessageRequest");
    detail::validate_identifier(forward_id, "OneBot forward_id");
  }
};

inline void to_json(Json &document,
                    const GetOneBotForwardMessageRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"installation", request.installation},
              {"forward_id", request.forward_id}};
}

inline void from_json(const Json &document,
                      GetOneBotForwardMessageRequest &request) {
  detail::require_object(document, "GetOneBotForwardMessageRequest");
  if (document.contains("action") &&
      document.at("action").get<obcx::bot::ActionId>() != request.action) {
    throw std::invalid_argument(
        "GetOneBotForwardMessageRequest action mismatch");
  }
  if (!document.contains("installation")) {
    throw std::invalid_argument(
        "GetOneBotForwardMessageRequest requires installation");
  }
  request.installation = document.at("installation").get<BotInstallationRef>();
  request.forward_id = detail::require_string(document, "forward_id",
                                              "GetOneBotForwardMessageRequest");
  request.validate();
}

inline auto GetOneBotForwardMessageRequest::from_json(const Json &document)
    -> GetOneBotForwardMessageRequest {
  detail::require_object(document, "GetOneBotForwardMessageRequest");
  if (!document.contains("installation")) {
    throw std::invalid_argument(
        "GetOneBotForwardMessageRequest requires installation");
  }
  GetOneBotForwardMessageRequest result{
      .installation = document.at("installation").get<BotInstallationRef>()};
  obcx::onebot11::bot::from_json(document, result);
  return result;
}

struct ResolveOneBotGroupFileRequest {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> ResolveOneBotGroupFileRequest;

  inline static const auto &action = actions::resolve_group_file;

  GroupTarget target;
  std::string file_id;

  void validate() const {
    target.validate();
    detail::require_onebot(target.installation,
                           "ResolveOneBotGroupFileRequest");
    detail::validate_identifier(file_id, "OneBot file_id", 2048);
  }
};

inline void to_json(Json &document,
                    const ResolveOneBotGroupFileRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"file_id", request.file_id}};
}

inline void from_json(const Json &document,
                      ResolveOneBotGroupFileRequest &request) {
  detail::require_object(document, "ResolveOneBotGroupFileRequest");
  if (document.contains("action") &&
      document.at("action").get<obcx::bot::ActionId>() != request.action) {
    throw std::invalid_argument(
        "ResolveOneBotGroupFileRequest action mismatch");
  }
  if (!document.contains("target")) {
    throw std::invalid_argument(
        "ResolveOneBotGroupFileRequest requires target");
  }
  request.target = document.at("target").get<GroupTarget>();
  request.file_id = detail::require_string(document, "file_id",
                                           "ResolveOneBotGroupFileRequest");
  request.validate();
}

inline auto ResolveOneBotGroupFileRequest::from_json(const Json &document)
    -> ResolveOneBotGroupFileRequest {
  detail::require_object(document, "ResolveOneBotGroupFileRequest");
  if (!document.contains("target")) {
    throw std::invalid_argument(
        "ResolveOneBotGroupFileRequest requires target");
  }
  ResolveOneBotGroupFileRequest result{
      .target = document.at("target").get<GroupTarget>()};
  obcx::onebot11::bot::from_json(document, result);
  return result;
}

struct ResolveOneBotPrivateFileRequest {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document)
      -> ResolveOneBotPrivateFileRequest;

  inline static const auto &action = actions::resolve_private_file;

  BotInstallationRef installation;
  std::string user_id;
  std::string file_id;

  void validate() const {
    detail::require_onebot(installation, "ResolveOneBotPrivateFileRequest");
    detail::validate_identifier(user_id, "OneBot user_id");
    detail::validate_identifier(file_id, "OneBot file_id", 2048);
  }
};

inline void to_json(Json &document,
                    const ResolveOneBotPrivateFileRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"installation", request.installation},
              {"user_id", request.user_id},
              {"file_id", request.file_id}};
}

inline void from_json(const Json &document,
                      ResolveOneBotPrivateFileRequest &request) {
  detail::require_object(document, "ResolveOneBotPrivateFileRequest");
  if (document.contains("action") &&
      document.at("action").get<obcx::bot::ActionId>() != request.action) {
    throw std::invalid_argument(
        "ResolveOneBotPrivateFileRequest action mismatch");
  }
  if (!document.contains("installation")) {
    throw std::invalid_argument(
        "ResolveOneBotPrivateFileRequest requires installation");
  }
  request.installation = document.at("installation").get<BotInstallationRef>();
  request.user_id = detail::require_string(document, "user_id",
                                           "ResolveOneBotPrivateFileRequest");
  request.file_id = detail::require_string(document, "file_id",
                                           "ResolveOneBotPrivateFileRequest");
  request.validate();
}

inline auto ResolveOneBotPrivateFileRequest::from_json(const Json &document)
    -> ResolveOneBotPrivateFileRequest {
  detail::require_object(document, "ResolveOneBotPrivateFileRequest");
  if (!document.contains("installation")) {
    throw std::invalid_argument(
        "ResolveOneBotPrivateFileRequest requires installation");
  }
  ResolveOneBotPrivateFileRequest result{
      .installation = document.at("installation").get<BotInstallationRef>()};
  obcx::onebot11::bot::from_json(document, result);
  return result;
}

struct PokeOneBotGroupRequest {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> PokeOneBotGroupRequest;

  inline static const auto &action = actions::poke_group;

  GroupTarget target;
  std::string user_id;

  void validate() const {
    target.validate();
    detail::require_onebot(target.installation, "PokeOneBotGroupRequest");
    detail::validate_identifier(user_id, "OneBot user_id");
  }
};

inline void to_json(Json &document, const PokeOneBotGroupRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"user_id", request.user_id}};
}

inline void from_json(const Json &document, PokeOneBotGroupRequest &request) {
  detail::require_object(document, "PokeOneBotGroupRequest");
  if (document.contains("action") &&
      document.at("action").get<obcx::bot::ActionId>() != request.action) {
    throw std::invalid_argument("PokeOneBotGroupRequest action mismatch");
  }
  if (!document.contains("target")) {
    throw std::invalid_argument("PokeOneBotGroupRequest requires target");
  }
  request.target = document.at("target").get<GroupTarget>();
  request.user_id =
      detail::require_string(document, "user_id", "PokeOneBotGroupRequest");
  request.validate();
}

inline auto PokeOneBotGroupRequest::from_json(const Json &document)
    -> PokeOneBotGroupRequest {
  detail::require_object(document, "PokeOneBotGroupRequest");
  if (!document.contains("target")) {
    throw std::invalid_argument("PokeOneBotGroupRequest requires target");
  }
  PokeOneBotGroupRequest result{.target =
                                    document.at("target").get<GroupTarget>()};
  obcx::onebot11::bot::from_json(document, result);
  return result;
}

} // namespace obcx::onebot11::bot

namespace obcx::bot {

template <>
struct OperationTraits<onebot11::bot::GetOneBotGroupMemberRequest>
    : OperationContract<onebot11::bot::GetOneBotGroupMemberRequest,
                        onebot11::bot::OneBotGroupMember, false> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == onebot11::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.target.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.target != request.target || result.user_id != request.user_id) {
      throw std::invalid_argument(
          "OneBot result does not match its requested scope");
    }
  }
};

template <>
struct OperationTraits<onebot11::bot::GetOneBotForwardMessageRequest>
    : OperationContract<onebot11::bot::GetOneBotForwardMessageRequest,
                        onebot11::bot::OneBotForwardMessage, false> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == onebot11::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.installation != request.installation ||
        result.forward_id != request.forward_id) {
      throw std::invalid_argument(
          "OneBot result does not match its requested scope");
    }
  }
};

template <>
struct OperationTraits<onebot11::bot::ResolveOneBotGroupFileRequest>
    : OperationContract<onebot11::bot::ResolveOneBotGroupFileRequest,
                        onebot11::bot::ResolvedOneBotGroupFile, false> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == onebot11::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.target.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.target != request.target || result.file_id != request.file_id) {
      throw std::invalid_argument(
          "OneBot result does not match its requested scope");
    }
  }
};

template <>
struct OperationTraits<onebot11::bot::ResolveOneBotPrivateFileRequest>
    : OperationContract<onebot11::bot::ResolveOneBotPrivateFileRequest,
                        onebot11::bot::ResolvedOneBotPrivateFile, false> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == onebot11::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.installation != request.installation ||
        result.user_id != request.user_id ||
        result.file_id != request.file_id) {
      throw std::invalid_argument(
          "OneBot result does not match its requested scope");
    }
  }
};

template <>
struct OperationTraits<onebot11::bot::PokeOneBotGroupRequest>
    : OperationContract<onebot11::bot::PokeOneBotGroupRequest,
                        onebot11::bot::OneBotGroupPokeResult, true> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == onebot11::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.target.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.target != request.target || result.user_id != request.user_id) {
      throw std::invalid_argument(
          "OneBot result does not match its requested scope");
    }
  }
};

} // namespace obcx::bot

#endif // OBCX_INCLUDE_ONEBOT11_BOT_OPERATIONS_HPP_
