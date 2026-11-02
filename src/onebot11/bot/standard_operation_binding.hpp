#pragma once

#include "core/bot/owned_structured_operation_binding.hpp"
#include "core/bot/structured_message_operation_binding.hpp"

// Process-private factory; it neither supplies a native handler nor registers
// an action. Never install this header as part of the Actor SDK.
#include "core/bot/structured_operation_binding.hpp"
#include "onebot11/bot/standard_operations.hpp"

namespace obcx::onebot11::bot::standard::detail {
struct StandardRequestAccess {
  static auto deadline(const SendPrivateMsgRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const SendGroupMsgRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const SendMsgRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const GetForwardMsgRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }

  static auto deadline(const GetMsgRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }

  static auto deadline(const GetRecordRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const GetImageRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const SetGroupAnonymousBanRequest &r)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return r.deadline_;
  }
  static auto deadline(const SetFriendAddRequestRequest &r)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return r.deadline_;
  }
  static auto deadline(const SetGroupAddRequestRequest &r)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return r.deadline_;
  }
  static auto deadline(const HandleQuickOperationRequest &r)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return r.deadline_;
  }

  static auto deadline(const SendLikeRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const DeleteMsgRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const SetGroupKickRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const SetGroupBanRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const SetGroupWholeBanRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const SetGroupAdminRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const SetGroupAnonymousRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const SetGroupCardRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const SetGroupNameRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const SetGroupLeaveRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const SetGroupSpecialTitleRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const GetStrangerInfoRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }

  static auto deadline(const GetLoginInfoRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }

  static auto deadline(const GetFriendListRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }

  static auto deadline(const GetGroupInfoRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }

  static auto deadline(const GetGroupListRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }

  static auto deadline(const GetGroupMemberInfoRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }

  static auto deadline(const GetGroupMemberListRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }

  static auto deadline(const GetGroupHonorInfoRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const GetCookiesRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const GetCsrfTokenRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const GetCredentialsRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const GetStatusRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const GetVersionInfoRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const CanSendImageRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const CanSendRecordRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const SetRestartRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
  static auto deadline(const CleanCacheRequest &request)
      -> const std::optional<std::chrono::steady_clock::time_point> & {
    return request.deadline_;
  }
};

using SendPrivateMsgDefinition =
    obcx::core::StructuredMessageOperationDefinition<SendPrivateMsgRequest>;
auto send_private_msg_definition() -> const SendPrivateMsgDefinition &;
auto bind_send_private_msg(SendPrivateMsgDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using SendGroupMsgDefinition =
    obcx::core::StructuredMessageOperationDefinition<SendGroupMsgRequest>;
auto send_group_msg_definition() -> const SendGroupMsgDefinition &;
auto bind_send_group_msg(SendGroupMsgDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using SendMsgDefinition =
    obcx::core::StructuredMessageOperationDefinition<SendMsgRequest>;
auto send_msg_definition() -> const SendMsgDefinition &;
auto bind_send_msg(SendMsgDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using SendLikeDefinition =
    obcx::core::StructuredOperationDefinition<SendLikeRequest>;
auto send_like_definition() -> const SendLikeDefinition &;
auto bind_send_like(SendLikeDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using DeleteMsgDefinition =
    obcx::core::StructuredOperationDefinition<DeleteMsgRequest>;
auto delete_msg_definition() -> const DeleteMsgDefinition &;
auto bind_delete_msg(DeleteMsgDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using SetGroupKickDefinition =
    obcx::core::StructuredOperationDefinition<SetGroupKickRequest>;
auto set_group_kick_definition() -> const SetGroupKickDefinition &;
auto bind_set_group_kick(SetGroupKickDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using SetGroupBanDefinition =
    obcx::core::StructuredOperationDefinition<SetGroupBanRequest>;
auto set_group_ban_definition() -> const SetGroupBanDefinition &;
auto bind_set_group_ban(SetGroupBanDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using SetGroupWholeBanDefinition =
    obcx::core::StructuredOperationDefinition<SetGroupWholeBanRequest>;
auto set_group_whole_ban_definition() -> const SetGroupWholeBanDefinition &;
auto bind_set_group_whole_ban(SetGroupWholeBanDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using SetGroupAdminDefinition =
    obcx::core::StructuredOperationDefinition<SetGroupAdminRequest>;
auto set_group_admin_definition() -> const SetGroupAdminDefinition &;
auto bind_set_group_admin(SetGroupAdminDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using SetGroupAnonymousDefinition =
    obcx::core::StructuredOperationDefinition<SetGroupAnonymousRequest>;
auto set_group_anonymous_definition() -> const SetGroupAnonymousDefinition &;
auto bind_set_group_anonymous(SetGroupAnonymousDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using SetGroupCardDefinition =
    obcx::core::StructuredOperationDefinition<SetGroupCardRequest>;
auto set_group_card_definition() -> const SetGroupCardDefinition &;
auto bind_set_group_card(SetGroupCardDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using SetGroupNameDefinition =
    obcx::core::StructuredOperationDefinition<SetGroupNameRequest>;
auto set_group_name_definition() -> const SetGroupNameDefinition &;
auto bind_set_group_name(SetGroupNameDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using SetGroupLeaveDefinition =
    obcx::core::StructuredOperationDefinition<SetGroupLeaveRequest>;
auto set_group_leave_definition() -> const SetGroupLeaveDefinition &;
auto bind_set_group_leave(SetGroupLeaveDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using SetGroupSpecialTitleDefinition =
    obcx::core::StructuredOperationDefinition<SetGroupSpecialTitleRequest>;
auto set_group_special_title_definition()
    -> const SetGroupSpecialTitleDefinition &;
auto bind_set_group_special_title(
    SetGroupSpecialTitleDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetStrangerInfoDefinition =
    obcx::core::StructuredOperationDefinition<GetStrangerInfoRequest>;
auto get_stranger_info_definition() -> const GetStrangerInfoDefinition &;
auto bind_get_stranger_info(GetStrangerInfoDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetLoginInfoDefinition =
    obcx::core::StructuredOperationDefinition<GetLoginInfoRequest>;
auto get_login_info_definition() -> const GetLoginInfoDefinition &;
auto bind_get_login_info(GetLoginInfoDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetFriendListDefinition =
    obcx::core::StructuredOperationDefinition<GetFriendListRequest>;
auto get_friend_list_definition() -> const GetFriendListDefinition &;
auto bind_get_friend_list(GetFriendListDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetGroupInfoDefinition =
    obcx::core::StructuredOperationDefinition<GetGroupInfoRequest>;
auto get_group_info_definition() -> const GetGroupInfoDefinition &;
auto bind_get_group_info(GetGroupInfoDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetGroupListDefinition =
    obcx::core::StructuredOperationDefinition<GetGroupListRequest>;
auto get_group_list_definition() -> const GetGroupListDefinition &;
auto bind_get_group_list(GetGroupListDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetGroupMemberInfoDefinition =
    obcx::core::StructuredOperationDefinition<GetGroupMemberInfoRequest>;
auto get_group_member_info_definition() -> const GetGroupMemberInfoDefinition &;
auto bind_get_group_member_info(GetGroupMemberInfoDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetGroupMemberListDefinition =
    obcx::core::StructuredOperationDefinition<GetGroupMemberListRequest>;
auto get_group_member_list_definition() -> const GetGroupMemberListDefinition &;
auto bind_get_group_member_list(GetGroupMemberListDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetGroupHonorInfoDefinition =
    obcx::core::StructuredOperationDefinition<GetGroupHonorInfoRequest>;
auto get_group_honor_info_definition() -> const GetGroupHonorInfoDefinition &;
auto bind_get_group_honor_info(GetGroupHonorInfoDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetCookiesDefinition =
    obcx::core::StructuredOperationDefinition<GetCookiesRequest>;
auto get_cookies_definition() -> const GetCookiesDefinition &;
auto bind_get_cookies(GetCookiesDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetCsrfTokenDefinition =
    obcx::core::StructuredOperationDefinition<GetCsrfTokenRequest>;
auto get_csrf_token_definition() -> const GetCsrfTokenDefinition &;
auto bind_get_csrf_token(GetCsrfTokenDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetCredentialsDefinition =
    obcx::core::StructuredOperationDefinition<GetCredentialsRequest>;
auto get_credentials_definition() -> const GetCredentialsDefinition &;
auto bind_get_credentials(GetCredentialsDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetStatusDefinition =
    obcx::core::StructuredOperationDefinition<GetStatusRequest>;
auto get_status_definition() -> const GetStatusDefinition &;
auto bind_get_status(GetStatusDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetVersionInfoDefinition =
    obcx::core::StructuredOperationDefinition<GetVersionInfoRequest>;
auto get_version_info_definition() -> const GetVersionInfoDefinition &;
auto bind_get_version_info(GetVersionInfoDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using CanSendImageDefinition =
    obcx::core::StructuredOperationDefinition<CanSendImageRequest>;
auto can_send_image_definition() -> const CanSendImageDefinition &;
auto bind_can_send_image(CanSendImageDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using CanSendRecordDefinition =
    obcx::core::StructuredOperationDefinition<CanSendRecordRequest>;
auto can_send_record_definition() -> const CanSendRecordDefinition &;
auto bind_can_send_record(CanSendRecordDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using SetRestartDefinition =
    obcx::core::StructuredOperationDefinition<SetRestartRequest>;
auto set_restart_definition() -> const SetRestartDefinition &;
auto bind_set_restart(SetRestartDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using CleanCacheDefinition =
    obcx::core::StructuredOperationDefinition<CleanCacheRequest>;
auto clean_cache_definition() -> const CleanCacheDefinition &;
auto bind_clean_cache(CleanCacheDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetMsgDefinition =
    obcx::core::StructuredOperationDefinition<GetMsgRequest>;
auto get_msg_definition() -> const GetMsgDefinition &;
auto bind_get_msg(GetMsgDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetForwardMsgDefinition =
    obcx::core::StructuredOperationDefinition<GetForwardMsgRequest>;
auto get_forward_msg_definition() -> const GetForwardMsgDefinition &;
auto bind_get_forward_msg(GetForwardMsgDefinition::Handler handler)
    -> obcx::core::OperationBinding;
using GetImageDefinition =
    obcx::core::StructuredOperationDefinition<GetImageRequest>;
auto get_image_definition() -> const GetImageDefinition &;
auto bind_get_image(GetImageDefinition::Handler handler)
    -> obcx::core::OperationBinding;

using GetRecordDefinition =
    obcx::core::StructuredOperationDefinition<GetRecordRequest>;
auto get_record_definition() -> const GetRecordDefinition &;
auto bind_get_record(GetRecordDefinition::Handler handler)
    -> obcx::core::OperationBinding;

// Only non-secret validation context crosses await alongside owned outcomes.
// Selectors stay move-only; returned provider paths are never copied to JSON.
struct MediaResultContext {
  ExecutionMode mode;
  ValueLimits values;
  std::optional<RecordFormat> requested_format;
};
auto media_result_context(const GetImageRequest &) -> MediaResultContext;
auto media_result_context(const GetRecordRequest &) -> MediaResultContext;
void validate_media_result(const MediaResultContext &, const GetImageOutcome &);
void validate_media_result(const MediaResultContext &,
                           const GetRecordOutcome &);

} // namespace obcx::onebot11::bot::standard::detail

namespace obcx::onebot11::bot::standard::detail {
using SetGroupAnonymousBanDefinition =
    obcx::core::OwnedStructuredOperationDefinition<SetGroupAnonymousBanRequest>;
auto set_group_anonymous_ban_definition()
    -> const SetGroupAnonymousBanDefinition &;
auto bind_set_group_anonymous_ban(SetGroupAnonymousBanDefinition::Handler)
    -> obcx::core::OperationBinding;
using SetFriendAddRequestDefinition =
    obcx::core::OwnedStructuredOperationDefinition<SetFriendAddRequestRequest>;
auto set_friend_add_request_definition()
    -> const SetFriendAddRequestDefinition &;
auto bind_set_friend_add_request(SetFriendAddRequestDefinition::Handler)
    -> obcx::core::OperationBinding;
using SetGroupAddRequestDefinition =
    obcx::core::OwnedStructuredOperationDefinition<SetGroupAddRequestRequest>;
auto set_group_add_request_definition() -> const SetGroupAddRequestDefinition &;
auto bind_set_group_add_request(SetGroupAddRequestDefinition::Handler)
    -> obcx::core::OperationBinding;
using HandleQuickOperationDefinition =
    obcx::core::OwnedStructuredMessageOperationDefinition<
        HandleQuickOperationRequest>;
auto handle_quick_operation_definition()
    -> const HandleQuickOperationDefinition &;
auto bind_handle_quick_operation(HandleQuickOperationDefinition::Handler)
    -> obcx::core::OperationBinding;
} // namespace obcx::onebot11::bot::standard::detail
