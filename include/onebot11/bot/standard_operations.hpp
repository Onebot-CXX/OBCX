#pragma once

#include "core/bot/gateway_codec.hpp"
#include "core/bot/operation_traits.hpp"
#include "onebot11/bot/standard_event_actions.hpp"
#include "onebot11/bot/standard_execution.hpp"
#include "onebot11/bot/standard_login.hpp"
#include "onebot11/bot/standard_lookup.hpp"
#include "onebot11/bot/standard_media_queries.hpp"
#include "onebot11/bot/standard_mutations.hpp"
#include "onebot11/bot/standard_queries.hpp"
#include "onebot11/bot/standard_send.hpp"
#include "onebot11/bot/standard_service.hpp"

namespace obcx::onebot11::bot::standard {
namespace detail {
struct StandardRequestAccess;
}

struct SetGroupAnonymousBanRequest {
  explicit SetGroupAnonymousBanRequest(
      SetGroupAnonymousBanParameters parameters, CallControl control,
      ValueLimits value_limits);
  void validate() const;
  SetGroupAnonymousBanParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct detail::StandardRequestAccess;
};
using SetGroupAnonymousBanOutcome = ExecutionOutcome<NoResponseData>;
struct SetFriendAddRequestRequest {
  explicit SetFriendAddRequestRequest(SetFriendAddRequestParameters parameters,
                                      CallControl control,
                                      ValueLimits value_limits);
  void validate() const;
  SetFriendAddRequestParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct detail::StandardRequestAccess;
};
using SetFriendAddRequestOutcome = ExecutionOutcome<NoResponseData>;
struct SetGroupAddRequestRequest {
  explicit SetGroupAddRequestRequest(SetGroupAddRequestParameters parameters,
                                     CallControl control,
                                     ValueLimits value_limits);
  void validate() const;
  SetGroupAddRequestParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct detail::StandardRequestAccess;
};
using SetGroupAddRequestOutcome = ExecutionOutcome<NoResponseData>;
struct HandleQuickOperationRequest {
  explicit HandleQuickOperationRequest(
      HandleQuickOperationParameters parameters, CallControl control,
      ValueLimits value_limits);
  void validate() const;
  HandleQuickOperationParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct detail::StandardRequestAccess;
};
using HandleQuickOperationOutcome = ExecutionOutcome<NoResponseData>;

struct SendLikeRequest {
  explicit SendLikeRequest(SendLikeParameters parameters, CallControl control,
                           ValueLimits value_limits);
  void validate() const;

  SendLikeParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<SendLikeRequest>;
  friend struct detail::StandardRequestAccess;
};

using SendLikeOutcome = ExecutionOutcome<NoResponseData>;
struct DeleteMsgRequest {
  explicit DeleteMsgRequest(DeleteMsgParameters parameters, CallControl control,
                            ValueLimits value_limits);
  void validate() const;

  DeleteMsgParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<DeleteMsgRequest>;
  friend struct detail::StandardRequestAccess;
};

using DeleteMsgOutcome = ExecutionOutcome<NoResponseData>;
struct SetGroupKickRequest {
  explicit SetGroupKickRequest(SetGroupKickParameters parameters,
                               CallControl control, ValueLimits value_limits);
  void validate() const;

  SetGroupKickParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<SetGroupKickRequest>;
  friend struct detail::StandardRequestAccess;
};

using SetGroupKickOutcome = ExecutionOutcome<NoResponseData>;
struct SetGroupBanRequest {
  explicit SetGroupBanRequest(SetGroupBanParameters parameters,
                              CallControl control, ValueLimits value_limits);
  void validate() const;

  SetGroupBanParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<SetGroupBanRequest>;
  friend struct detail::StandardRequestAccess;
};

using SetGroupBanOutcome = ExecutionOutcome<NoResponseData>;
struct SetGroupWholeBanRequest {
  explicit SetGroupWholeBanRequest(SetGroupWholeBanParameters parameters,
                                   CallControl control,
                                   ValueLimits value_limits);
  void validate() const;

  SetGroupWholeBanParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<SetGroupWholeBanRequest>;
  friend struct detail::StandardRequestAccess;
};

using SetGroupWholeBanOutcome = ExecutionOutcome<NoResponseData>;
struct SetGroupAdminRequest {
  explicit SetGroupAdminRequest(SetGroupAdminParameters parameters,
                                CallControl control, ValueLimits value_limits);
  void validate() const;

  SetGroupAdminParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<SetGroupAdminRequest>;
  friend struct detail::StandardRequestAccess;
};

using SetGroupAdminOutcome = ExecutionOutcome<NoResponseData>;
struct SetGroupAnonymousRequest {
  explicit SetGroupAnonymousRequest(SetGroupAnonymousParameters parameters,
                                    CallControl control,
                                    ValueLimits value_limits);
  void validate() const;

  SetGroupAnonymousParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<SetGroupAnonymousRequest>;
  friend struct detail::StandardRequestAccess;
};

using SetGroupAnonymousOutcome = ExecutionOutcome<NoResponseData>;
struct SetGroupCardRequest {
  explicit SetGroupCardRequest(SetGroupCardParameters parameters,
                               CallControl control, ValueLimits value_limits);
  void validate() const;

  SetGroupCardParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<SetGroupCardRequest>;
  friend struct detail::StandardRequestAccess;
};

using SetGroupCardOutcome = ExecutionOutcome<NoResponseData>;
struct SetGroupNameRequest {
  explicit SetGroupNameRequest(SetGroupNameParameters parameters,
                               CallControl control, ValueLimits value_limits);
  void validate() const;

  SetGroupNameParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<SetGroupNameRequest>;
  friend struct detail::StandardRequestAccess;
};

using SetGroupNameOutcome = ExecutionOutcome<NoResponseData>;
struct SetGroupLeaveRequest {
  explicit SetGroupLeaveRequest(SetGroupLeaveParameters parameters,
                                CallControl control, ValueLimits value_limits);
  void validate() const;

  SetGroupLeaveParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<SetGroupLeaveRequest>;
  friend struct detail::StandardRequestAccess;
};

using SetGroupLeaveOutcome = ExecutionOutcome<NoResponseData>;
struct SetGroupSpecialTitleRequest {
  explicit SetGroupSpecialTitleRequest(
      SetGroupSpecialTitleParameters parameters, CallControl control,
      ValueLimits value_limits);
  void validate() const;

  SetGroupSpecialTitleParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<SetGroupSpecialTitleRequest>;
  friend struct detail::StandardRequestAccess;
};

using SetGroupSpecialTitleOutcome = ExecutionOutcome<NoResponseData>;
struct GetStrangerInfoRequest {
  explicit GetStrangerInfoRequest(GetStrangerInfoParameters parameters,
                                  CallControl control,
                                  ValueLimits value_limits);
  void validate() const;

  GetStrangerInfoParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetStrangerInfoRequest>;
  friend struct detail::StandardRequestAccess;
};

using GetStrangerInfoOutcome = ExecutionOutcome<StrangerInfo>;
struct GetLoginInfoRequest {
  explicit GetLoginInfoRequest(GetLoginInfoParameters parameters,
                               CallControl control, ValueLimits value_limits);
  void validate() const;

  GetLoginInfoParameters parameters;
  // Parameter bytes bound native params, not SDK metadata or WS framing.
  // Response bytes bound the entire native HTTP body or WS reply, not heap.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetLoginInfoRequest>;
  friend struct detail::StandardRequestAccess;
};

using GetLoginInfoOutcome = ExecutionOutcome<LoginInfo>;
struct GetFriendListRequest {
  explicit GetFriendListRequest(GetFriendListParameters parameters,
                                CallControl control, ValueLimits value_limits);
  void validate() const;

  GetFriendListParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetFriendListRequest>;
  friend struct detail::StandardRequestAccess;
};

using GetFriendListOutcome = ExecutionOutcome<FriendList>;
struct GetGroupInfoRequest {
  explicit GetGroupInfoRequest(GetGroupInfoParameters parameters,
                               CallControl control, ValueLimits value_limits);
  void validate() const;

  GetGroupInfoParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetGroupInfoRequest>;
  friend struct detail::StandardRequestAccess;
};

using GetGroupInfoOutcome = ExecutionOutcome<GroupInfo>;
struct GetGroupListRequest {
  explicit GetGroupListRequest(GetGroupListParameters parameters,
                               CallControl control, ValueLimits value_limits);
  void validate() const;

  GetGroupListParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetGroupListRequest>;
  friend struct detail::StandardRequestAccess;
};

using GetGroupListOutcome = ExecutionOutcome<GroupList>;
struct GetGroupMemberInfoRequest {
  explicit GetGroupMemberInfoRequest(GetGroupMemberInfoParameters parameters,
                                     CallControl control,
                                     ValueLimits value_limits);
  void validate() const;

  GetGroupMemberInfoParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetGroupMemberInfoRequest>;
  friend struct detail::StandardRequestAccess;
};

using GetGroupMemberInfoOutcome = ExecutionOutcome<GroupMemberInfo>;
struct GetGroupMemberListRequest {
  explicit GetGroupMemberListRequest(GetGroupMemberListParameters parameters,
                                     CallControl control,
                                     ValueLimits value_limits);
  void validate() const;

  GetGroupMemberListParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetGroupMemberListRequest>;
  friend struct detail::StandardRequestAccess;
};

using GetGroupMemberListOutcome = ExecutionOutcome<GroupMemberList>;
struct GetGroupHonorInfoRequest {
  explicit GetGroupHonorInfoRequest(GetGroupHonorInfoParameters parameters,
                                    CallControl control,
                                    ValueLimits value_limits);
  void validate() const;

  GetGroupHonorInfoParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the later native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetGroupHonorInfoRequest>;
  friend struct detail::StandardRequestAccess;
};

using GetGroupHonorInfoOutcome = ExecutionOutcome<GroupHonorInfo>;
struct GetCookiesRequest {
  explicit GetCookiesRequest(obcx::bot::BotInstallationRef installation,
                             GetCookiesParameters parameters,
                             CallControl control, ValueLimits value_limits);
  void validate() const;

  obcx::bot::BotInstallationRef installation;
  GetCookiesParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, never a renewed or wall-clock deadline.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetCookiesRequest>;
  friend struct detail::StandardRequestAccess;
};
using GetCookiesOutcome = ExecutionOutcome<CookiesData>;

struct GetCsrfTokenRequest {
  explicit GetCsrfTokenRequest(obcx::bot::BotInstallationRef installation,
                               GetCsrfTokenParameters parameters,
                               CallControl control, ValueLimits value_limits);
  void validate() const;

  obcx::bot::BotInstallationRef installation;
  GetCsrfTokenParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, never a renewed or wall-clock deadline.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetCsrfTokenRequest>;
  friend struct detail::StandardRequestAccess;
};
using GetCsrfTokenOutcome = ExecutionOutcome<CsrfTokenData>;

struct GetCredentialsRequest {
  explicit GetCredentialsRequest(obcx::bot::BotInstallationRef installation,
                                 GetCredentialsParameters parameters,
                                 CallControl control, ValueLimits value_limits);
  void validate() const;

  obcx::bot::BotInstallationRef installation;
  GetCredentialsParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, never a renewed or wall-clock deadline.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetCredentialsRequest>;
  friend struct detail::StandardRequestAccess;
};
using GetCredentialsOutcome = ExecutionOutcome<CredentialsData>;

struct GetStatusRequest {
  explicit GetStatusRequest(obcx::bot::BotInstallationRef installation,
                            GetStatusParameters parameters, CallControl control,
                            ValueLimits value_limits);
  void validate() const;

  obcx::bot::BotInstallationRef installation;
  GetStatusParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, never a renewed or wall-clock deadline.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetStatusRequest>;
  friend struct detail::StandardRequestAccess;
};
using GetStatusOutcome = ExecutionOutcome<StatusData>;

struct GetVersionInfoRequest {
  explicit GetVersionInfoRequest(obcx::bot::BotInstallationRef installation,
                                 GetVersionInfoParameters parameters,
                                 CallControl control, ValueLimits value_limits);
  void validate() const;

  obcx::bot::BotInstallationRef installation;
  GetVersionInfoParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, never a renewed or wall-clock deadline.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetVersionInfoRequest>;
  friend struct detail::StandardRequestAccess;
};
using GetVersionInfoOutcome = ExecutionOutcome<VersionInfoData>;

struct CanSendImageRequest {
  explicit CanSendImageRequest(obcx::bot::BotInstallationRef installation,
                               CanSendImageParameters parameters,
                               CallControl control, ValueLimits value_limits);
  void validate() const;

  obcx::bot::BotInstallationRef installation;
  CanSendImageParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, never a renewed or wall-clock deadline.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<CanSendImageRequest>;
  friend struct detail::StandardRequestAccess;
};
using CanSendImageOutcome = ExecutionOutcome<CanSendImageData>;

struct CanSendRecordRequest {
  explicit CanSendRecordRequest(obcx::bot::BotInstallationRef installation,
                                CanSendRecordParameters parameters,
                                CallControl control, ValueLimits value_limits);
  void validate() const;

  obcx::bot::BotInstallationRef installation;
  CanSendRecordParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, never a renewed or wall-clock deadline.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<CanSendRecordRequest>;
  friend struct detail::StandardRequestAccess;
};
using CanSendRecordOutcome = ExecutionOutcome<CanSendRecordData>;

struct SetRestartRequest {
  explicit SetRestartRequest(obcx::bot::BotInstallationRef installation,
                             SetRestartParameters parameters,
                             CallControl control, ValueLimits value_limits);
  void validate() const;

  obcx::bot::BotInstallationRef installation;
  SetRestartParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, never a renewed or wall-clock deadline.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<SetRestartRequest>;
  friend struct detail::StandardRequestAccess;
};
using SetRestartOutcome = ExecutionOutcome<ServiceNoData>;

struct CleanCacheRequest {
  explicit CleanCacheRequest(obcx::bot::BotInstallationRef installation,
                             CleanCacheParameters parameters,
                             CallControl control, ValueLimits value_limits);
  void validate() const;

  obcx::bot::BotInstallationRef installation;
  CleanCacheParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, never a renewed or wall-clock deadline.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<CleanCacheRequest>;
  friend struct detail::StandardRequestAccess;
};
using CleanCacheOutcome = ExecutionOutcome<ServiceNoData>;

struct SendPrivateMsgRequest {
  explicit SendPrivateMsgRequest(SendPrivateMsgParameters parameters,
                                 CallControl control, ValueLimits value_limits);
  void validate() const;
  SendPrivateMsgParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<SendPrivateMsgRequest>;
  friend struct detail::StandardRequestAccess;
};
using SendPrivateMsgOutcome = ExecutionOutcome<SendMessageData>;

struct SendGroupMsgRequest {
  explicit SendGroupMsgRequest(SendGroupMsgParameters parameters,
                               CallControl control, ValueLimits value_limits);
  void validate() const;
  SendGroupMsgParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<SendGroupMsgRequest>;
  friend struct detail::StandardRequestAccess;
};
using SendGroupMsgOutcome = ExecutionOutcome<SendMessageData>;

struct SendMsgRequest {
  explicit SendMsgRequest(SendMsgParameters parameters, CallControl control,
                          ValueLimits value_limits);
  void validate() const;
  SendMsgParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<SendMsgRequest>;
  friend struct detail::StandardRequestAccess;
};
using SendMsgOutcome = ExecutionOutcome<SendMessageData>;

// Query intent only: completion never grants acquisition or local-read
// authority.
struct GetImageRequest {
  explicit GetImageRequest(GetImageParameters parameters, CallControl control,
                           ValueLimits value_limits);
  void validate() const;

  GetImageParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetImageRequest>;
  friend struct detail::StandardRequestAccess;
};
using GetImageOutcome = ExecutionOutcome<ImageFileData>;

// Query intent only: completion never grants acquisition or local-read
// authority.
struct GetRecordRequest {
  explicit GetRecordRequest(GetRecordParameters parameters, CallControl control,
                            ValueLimits value_limits);
  void validate() const;

  GetRecordParameters parameters;
  CallControl control;
  ValueLimits value_limits;

private:
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetRecordRequest>;
  friend struct detail::StandardRequestAccess;
};
using GetRecordOutcome = ExecutionOutcome<ConvertedRecordData>;

struct GetMsgRequest {
  explicit GetMsgRequest(GetMessageParameters parameters, CallControl control,
                         ValueLimits value_limits);
  void validate() const;

  GetMessageParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetMsgRequest>;
  friend struct detail::StandardRequestAccess;
};

using GetMsgOutcome = ExecutionOutcome<DetachedMessage>;
struct GetForwardMsgRequest {
  explicit GetForwardMsgRequest(GetForwardMessageParameters parameters,
                                CallControl control, ValueLimits value_limits);
  void validate() const;

  GetForwardMessageParameters parameters;
  // Parameter bytes bound the serialized native params object, not this SDK
  // envelope/WS framing. Response bytes bound the entire native HTTP body or
  // WS reply, not headers/TLS/heap; the native backend enforces these.
  CallControl control;
  ValueLimits value_limits;

private:
  // Same-process dispatch metadata, not caller configuration or a wall clock.
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  friend struct obcx::bot::GatewayCodec<GetForwardMsgRequest>;
  friend struct detail::StandardRequestAccess;
};

using GetForwardMsgOutcome = ExecutionOutcome<DetachedForwardMessage>;
} // namespace obcx::onebot11::bot::standard

namespace obcx::bot {
template <> struct OperationTraits<onebot11::bot::standard::SendLikeRequest> {
  using request_type = onebot11::bot::standard::SendLikeRequest;
  using result_type = onebot11::bot::standard::SendLikeOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::SendLikeRequest> {
  static auto encode(const onebot11::bot::standard::SendLikeRequest &request)
      -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::SendLikeRequest;
};
template <> struct OperationTraits<onebot11::bot::standard::DeleteMsgRequest> {
  using request_type = onebot11::bot::standard::DeleteMsgRequest;
  using result_type = onebot11::bot::standard::DeleteMsgOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::DeleteMsgRequest> {
  static auto encode(const onebot11::bot::standard::DeleteMsgRequest &request)
      -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::DeleteMsgRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::SetGroupKickRequest> {
  using request_type = onebot11::bot::standard::SetGroupKickRequest;
  using result_type = onebot11::bot::standard::SetGroupKickOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::SetGroupKickRequest> {
  static auto encode(
      const onebot11::bot::standard::SetGroupKickRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::SetGroupKickRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::SetGroupBanRequest> {
  using request_type = onebot11::bot::standard::SetGroupBanRequest;
  using result_type = onebot11::bot::standard::SetGroupBanOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::SetGroupBanRequest> {
  static auto encode(const onebot11::bot::standard::SetGroupBanRequest &request)
      -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::SetGroupBanRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::SetGroupWholeBanRequest> {
  using request_type = onebot11::bot::standard::SetGroupWholeBanRequest;
  using result_type = onebot11::bot::standard::SetGroupWholeBanOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <>
struct GatewayCodec<onebot11::bot::standard::SetGroupWholeBanRequest> {
  static auto encode(
      const onebot11::bot::standard::SetGroupWholeBanRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::SetGroupWholeBanRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::SetGroupAdminRequest> {
  using request_type = onebot11::bot::standard::SetGroupAdminRequest;
  using result_type = onebot11::bot::standard::SetGroupAdminOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::SetGroupAdminRequest> {
  static auto encode(
      const onebot11::bot::standard::SetGroupAdminRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::SetGroupAdminRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::SetGroupAnonymousRequest> {
  using request_type = onebot11::bot::standard::SetGroupAnonymousRequest;
  using result_type = onebot11::bot::standard::SetGroupAnonymousOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <>
struct GatewayCodec<onebot11::bot::standard::SetGroupAnonymousRequest> {
  static auto encode(
      const onebot11::bot::standard::SetGroupAnonymousRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::SetGroupAnonymousRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::SetGroupCardRequest> {
  using request_type = onebot11::bot::standard::SetGroupCardRequest;
  using result_type = onebot11::bot::standard::SetGroupCardOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::SetGroupCardRequest> {
  static auto encode(
      const onebot11::bot::standard::SetGroupCardRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::SetGroupCardRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::SetGroupNameRequest> {
  using request_type = onebot11::bot::standard::SetGroupNameRequest;
  using result_type = onebot11::bot::standard::SetGroupNameOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::SetGroupNameRequest> {
  static auto encode(
      const onebot11::bot::standard::SetGroupNameRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::SetGroupNameRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::SetGroupLeaveRequest> {
  using request_type = onebot11::bot::standard::SetGroupLeaveRequest;
  using result_type = onebot11::bot::standard::SetGroupLeaveOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::SetGroupLeaveRequest> {
  static auto encode(
      const onebot11::bot::standard::SetGroupLeaveRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::SetGroupLeaveRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::SetGroupSpecialTitleRequest> {
  using request_type = onebot11::bot::standard::SetGroupSpecialTitleRequest;
  using result_type = onebot11::bot::standard::SetGroupSpecialTitleOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <>
struct GatewayCodec<onebot11::bot::standard::SetGroupSpecialTitleRequest> {
  static auto encode(
      const onebot11::bot::standard::SetGroupSpecialTitleRequest &request)
      -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::SetGroupSpecialTitleRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::GetStrangerInfoRequest> {
  using request_type = onebot11::bot::standard::GetStrangerInfoRequest;
  using result_type = onebot11::bot::standard::GetStrangerInfoOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <>
struct GatewayCodec<onebot11::bot::standard::GetStrangerInfoRequest> {
  static auto encode(
      const onebot11::bot::standard::GetStrangerInfoRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetStrangerInfoRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::GetLoginInfoRequest> {
  using request_type = onebot11::bot::standard::GetLoginInfoRequest;
  using result_type = onebot11::bot::standard::GetLoginInfoOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::GetLoginInfoRequest> {
  static auto encode(
      const onebot11::bot::standard::GetLoginInfoRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetLoginInfoRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::GetFriendListRequest> {
  using request_type = onebot11::bot::standard::GetFriendListRequest;
  using result_type = onebot11::bot::standard::GetFriendListOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::GetFriendListRequest> {
  static auto encode(
      const onebot11::bot::standard::GetFriendListRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetFriendListRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::GetGroupInfoRequest> {
  using request_type = onebot11::bot::standard::GetGroupInfoRequest;
  using result_type = onebot11::bot::standard::GetGroupInfoOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::GetGroupInfoRequest> {
  static auto encode(
      const onebot11::bot::standard::GetGroupInfoRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetGroupInfoRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::GetGroupListRequest> {
  using request_type = onebot11::bot::standard::GetGroupListRequest;
  using result_type = onebot11::bot::standard::GetGroupListOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::GetGroupListRequest> {
  static auto encode(
      const onebot11::bot::standard::GetGroupListRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetGroupListRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::GetGroupMemberInfoRequest> {
  using request_type = onebot11::bot::standard::GetGroupMemberInfoRequest;
  using result_type = onebot11::bot::standard::GetGroupMemberInfoOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <>
struct GatewayCodec<onebot11::bot::standard::GetGroupMemberInfoRequest> {
  static auto encode(
      const onebot11::bot::standard::GetGroupMemberInfoRequest &request)
      -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetGroupMemberInfoRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::GetGroupMemberListRequest> {
  using request_type = onebot11::bot::standard::GetGroupMemberListRequest;
  using result_type = onebot11::bot::standard::GetGroupMemberListOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <>
struct GatewayCodec<onebot11::bot::standard::GetGroupMemberListRequest> {
  static auto encode(
      const onebot11::bot::standard::GetGroupMemberListRequest &request)
      -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetGroupMemberListRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::GetGroupHonorInfoRequest> {
  using request_type = onebot11::bot::standard::GetGroupHonorInfoRequest;
  using result_type = onebot11::bot::standard::GetGroupHonorInfoOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <>
struct GatewayCodec<onebot11::bot::standard::GetGroupHonorInfoRequest> {
  static auto encode(
      const onebot11::bot::standard::GetGroupHonorInfoRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetGroupHonorInfoRequest;
};
template <> struct OperationTraits<onebot11::bot::standard::GetCookiesRequest> {
  using request_type = onebot11::bot::standard::GetCookiesRequest;
  using result_type = onebot11::bot::standard::GetCookiesOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;
  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};
template <> struct GatewayCodec<onebot11::bot::standard::GetCookiesRequest> {
  static auto encode(const onebot11::bot::standard::GetCookiesRequest &request)
      -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetCookiesRequest;
};

template <>
struct OperationTraits<onebot11::bot::standard::GetCsrfTokenRequest> {
  using request_type = onebot11::bot::standard::GetCsrfTokenRequest;
  using result_type = onebot11::bot::standard::GetCsrfTokenOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;
  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};
template <> struct GatewayCodec<onebot11::bot::standard::GetCsrfTokenRequest> {
  static auto encode(
      const onebot11::bot::standard::GetCsrfTokenRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetCsrfTokenRequest;
};

template <>
struct OperationTraits<onebot11::bot::standard::GetCredentialsRequest> {
  using request_type = onebot11::bot::standard::GetCredentialsRequest;
  using result_type = onebot11::bot::standard::GetCredentialsOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;
  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};
template <>
struct GatewayCodec<onebot11::bot::standard::GetCredentialsRequest> {
  static auto encode(
      const onebot11::bot::standard::GetCredentialsRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetCredentialsRequest;
};

template <> struct OperationTraits<onebot11::bot::standard::GetStatusRequest> {
  using request_type = onebot11::bot::standard::GetStatusRequest;
  using result_type = onebot11::bot::standard::GetStatusOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;
  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};
template <> struct GatewayCodec<onebot11::bot::standard::GetStatusRequest> {
  static auto encode(const onebot11::bot::standard::GetStatusRequest &request)
      -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetStatusRequest;
};

template <>
struct OperationTraits<onebot11::bot::standard::GetVersionInfoRequest> {
  using request_type = onebot11::bot::standard::GetVersionInfoRequest;
  using result_type = onebot11::bot::standard::GetVersionInfoOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;
  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};
template <>
struct GatewayCodec<onebot11::bot::standard::GetVersionInfoRequest> {
  static auto encode(
      const onebot11::bot::standard::GetVersionInfoRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetVersionInfoRequest;
};

template <>
struct OperationTraits<onebot11::bot::standard::CanSendImageRequest> {
  using request_type = onebot11::bot::standard::CanSendImageRequest;
  using result_type = onebot11::bot::standard::CanSendImageOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;
  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};
template <> struct GatewayCodec<onebot11::bot::standard::CanSendImageRequest> {
  static auto encode(
      const onebot11::bot::standard::CanSendImageRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::CanSendImageRequest;
};

template <>
struct OperationTraits<onebot11::bot::standard::CanSendRecordRequest> {
  using request_type = onebot11::bot::standard::CanSendRecordRequest;
  using result_type = onebot11::bot::standard::CanSendRecordOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;
  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};
template <> struct GatewayCodec<onebot11::bot::standard::CanSendRecordRequest> {
  static auto encode(
      const onebot11::bot::standard::CanSendRecordRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::CanSendRecordRequest;
};

template <> struct OperationTraits<onebot11::bot::standard::SetRestartRequest> {
  using request_type = onebot11::bot::standard::SetRestartRequest;
  using result_type = onebot11::bot::standard::SetRestartOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;
  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};
template <> struct GatewayCodec<onebot11::bot::standard::SetRestartRequest> {
  static auto encode(const onebot11::bot::standard::SetRestartRequest &request)
      -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::SetRestartRequest;
};

template <> struct OperationTraits<onebot11::bot::standard::CleanCacheRequest> {
  using request_type = onebot11::bot::standard::CleanCacheRequest;
  using result_type = onebot11::bot::standard::CleanCacheOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;
  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};
template <> struct GatewayCodec<onebot11::bot::standard::CleanCacheRequest> {
  static auto encode(const onebot11::bot::standard::CleanCacheRequest &request)
      -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::CleanCacheRequest;
};

template <>
struct OperationTraits<onebot11::bot::standard::SendPrivateMsgRequest> {
  using request_type = onebot11::bot::standard::SendPrivateMsgRequest;
  using result_type = onebot11::bot::standard::SendPrivateMsgOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = true;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <>
struct GatewayCodec<onebot11::bot::standard::SendPrivateMsgRequest> {
  static auto encode(
      const onebot11::bot::standard::SendPrivateMsgRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::SendPrivateMsgRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::SendGroupMsgRequest> {
  using request_type = onebot11::bot::standard::SendGroupMsgRequest;
  using result_type = onebot11::bot::standard::SendGroupMsgOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = true;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::SendGroupMsgRequest> {
  static auto encode(
      const onebot11::bot::standard::SendGroupMsgRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::SendGroupMsgRequest;
};
template <> struct OperationTraits<onebot11::bot::standard::SendMsgRequest> {
  using request_type = onebot11::bot::standard::SendMsgRequest;
  using result_type = onebot11::bot::standard::SendMsgOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = true;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::SendMsgRequest> {
  static auto encode(const onebot11::bot::standard::SendMsgRequest &request)
      -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::SendMsgRequest;
};
template <> struct OperationTraits<onebot11::bot::standard::GetMsgRequest> {
  using request_type = onebot11::bot::standard::GetMsgRequest;
  using result_type = onebot11::bot::standard::GetMsgOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::GetMsgRequest> {
  static auto encode(const onebot11::bot::standard::GetMsgRequest &request)
      -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetMsgRequest;
};
template <>
struct OperationTraits<onebot11::bot::standard::GetForwardMsgRequest> {
  using request_type = onebot11::bot::standard::GetForwardMsgRequest;
  using result_type = onebot11::bot::standard::GetForwardMsgOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;

  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};

// Explicit internal SDK envelope only; no native action or outcome serializer.
template <> struct GatewayCodec<onebot11::bot::standard::GetForwardMsgRequest> {
  static auto encode(
      const onebot11::bot::standard::GetForwardMsgRequest &request) -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetForwardMsgRequest;
};
template <> struct OperationTraits<onebot11::bot::standard::GetImageRequest> {
  using request_type = onebot11::bot::standard::GetImageRequest;
  using result_type = onebot11::bot::standard::GetImageOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;
  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};
template <> struct GatewayCodec<onebot11::bot::standard::GetImageRequest> {
  static auto encode(const onebot11::bot::standard::GetImageRequest &request)
      -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetImageRequest;
};

template <> struct OperationTraits<onebot11::bot::standard::GetRecordRequest> {
  using request_type = onebot11::bot::standard::GetRecordRequest;
  using result_type = onebot11::bot::standard::GetRecordOutcome;
  static constexpr bool side_effecting = false;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool message_producing = false;
  static auto action() -> const ActionId &;
  static auto installation(const request_type &request)
      -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &surface) -> bool;
  static void validate_result(const request_type &request,
                              const result_type &result);
};
template <> struct GatewayCodec<onebot11::bot::standard::GetRecordRequest> {
  static auto encode(const onebot11::bot::standard::GetRecordRequest &request)
      -> Json;
  static auto decode(const Json &payload)
      -> onebot11::bot::standard::GetRecordRequest;
};

} // namespace obcx::bot

namespace obcx::bot {
template <>
struct OperationTraits<onebot11::bot::standard::SetGroupAnonymousBanRequest> {
  using request_type = onebot11::bot::standard::SetGroupAnonymousBanRequest;
  using result_type = onebot11::bot::standard::SetGroupAnonymousBanOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool requires_owned_request = true;
  static constexpr bool message_producing = false;
  static auto action() -> const ActionId &;
  static auto installation(const request_type &) -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &) -> bool;
  static void validate_result(const request_type &, const result_type &);
};
template <>
struct OperationTraits<onebot11::bot::standard::SetFriendAddRequestRequest> {
  using request_type = onebot11::bot::standard::SetFriendAddRequestRequest;
  using result_type = onebot11::bot::standard::SetFriendAddRequestOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool requires_owned_request = true;
  static constexpr bool message_producing = false;
  static auto action() -> const ActionId &;
  static auto installation(const request_type &) -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &) -> bool;
  static void validate_result(const request_type &, const result_type &);
};
template <>
struct OperationTraits<onebot11::bot::standard::SetGroupAddRequestRequest> {
  using request_type = onebot11::bot::standard::SetGroupAddRequestRequest;
  using result_type = onebot11::bot::standard::SetGroupAddRequestOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool requires_owned_request = true;
  static constexpr bool message_producing = false;
  static auto action() -> const ActionId &;
  static auto installation(const request_type &) -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &) -> bool;
  static void validate_result(const request_type &, const result_type &);
};
template <>
struct OperationTraits<onebot11::bot::standard::HandleQuickOperationRequest> {
  using request_type = onebot11::bot::standard::HandleQuickOperationRequest;
  using result_type = onebot11::bot::standard::HandleQuickOperationOutcome;
  static constexpr bool side_effecting = true;
  static constexpr bool requires_structured_result = true;
  static constexpr bool requires_outcome_client = true;
  static constexpr bool requires_owned_request = true;
  static constexpr bool message_producing = true;
  static auto action() -> const ActionId &;
  static auto installation(const request_type &) -> const BotInstallationRef &;
  static auto supports_surface(const SurfaceId &) -> bool;
  static void validate_result(const request_type &, const result_type &);
};
} // namespace obcx::bot
