#pragma once

#include "onebot11/bot/standard_operations.hpp"

#include <memory>

namespace obcx::onebot11::bot::standard {
class Client {
public:
  explicit Client(obcx::bot::BotOperationGateway &gateway)
      : gateway_(gateway) {}

  auto send_like(SendLikeRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<SendLikeOutcome>>>;

  auto delete_msg(DeleteMsgRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<DeleteMsgOutcome>>>;

  auto set_group_kick(SetGroupKickRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<SetGroupKickOutcome>>>;

  auto set_group_ban(SetGroupBanRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<SetGroupBanOutcome>>>;

  auto set_group_whole_ban(SetGroupWholeBanRequest request)
      -> boost::asio::awaitable<obcx::bot::BotOperationResult<
          std::unique_ptr<SetGroupWholeBanOutcome>>>;

  auto set_group_admin(SetGroupAdminRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<SetGroupAdminOutcome>>>;

  auto set_group_anonymous(SetGroupAnonymousRequest request)
      -> boost::asio::awaitable<obcx::bot::BotOperationResult<
          std::unique_ptr<SetGroupAnonymousOutcome>>>;

  auto set_group_card(SetGroupCardRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<SetGroupCardOutcome>>>;

  auto set_group_name(SetGroupNameRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<SetGroupNameOutcome>>>;

  auto set_group_leave(SetGroupLeaveRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<SetGroupLeaveOutcome>>>;

  auto set_group_special_title(SetGroupSpecialTitleRequest request)
      -> boost::asio::awaitable<obcx::bot::BotOperationResult<
          std::unique_ptr<SetGroupSpecialTitleOutcome>>>;

  auto
  get_stranger_info(GetStrangerInfoRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<GetStrangerInfoOutcome>>>;

  auto get_login_info(GetLoginInfoRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<GetLoginInfoOutcome>>>;

  auto get_friend_list(GetFriendListRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<GetFriendListOutcome>>>;

  auto get_group_info(GetGroupInfoRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<GetGroupInfoOutcome>>>;

  auto get_group_list(GetGroupListRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<GetGroupListOutcome>>>;

  auto get_group_member_info(GetGroupMemberInfoRequest request)
      -> boost::asio::awaitable<obcx::bot::BotOperationResult<
          std::unique_ptr<GetGroupMemberInfoOutcome>>>;

  auto get_group_member_list(GetGroupMemberListRequest request)
      -> boost::asio::awaitable<obcx::bot::BotOperationResult<
          std::unique_ptr<GetGroupMemberListOutcome>>>;

  auto get_group_honor_info(GetGroupHonorInfoRequest request)
      -> boost::asio::awaitable<obcx::bot::BotOperationResult<
          std::unique_ptr<GetGroupHonorInfoOutcome>>>;

  auto get_cookies(GetCookiesRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<GetCookiesOutcome>>>;

  auto get_csrf_token(GetCsrfTokenRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<GetCsrfTokenOutcome>>>;

  auto get_credentials(GetCredentialsRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<GetCredentialsOutcome>>>;

  auto get_status(GetStatusRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<GetStatusOutcome>>>;

  auto
  get_version_info(GetVersionInfoRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<GetVersionInfoOutcome>>>;

  auto can_send_image(CanSendImageRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<CanSendImageOutcome>>>;

  auto can_send_record(CanSendRecordRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<CanSendRecordOutcome>>>;

  auto set_restart(SetRestartRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<SetRestartOutcome>>>;

  auto clean_cache(CleanCacheRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<CleanCacheOutcome>>>;

  auto
  send_private_msg(SendPrivateMsgRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<SendPrivateMsgOutcome>>>;

  auto send_group_msg(SendGroupMsgRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<SendGroupMsgOutcome>>>;

  auto send_msg(SendMsgRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<SendMsgOutcome>>>;

  auto get_msg(GetMsgRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<GetMsgOutcome>>>;

  auto get_forward_msg(GetForwardMsgRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<GetForwardMsgOutcome>>>;

  auto get_image(GetImageRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<GetImageOutcome>>>;

  auto get_record(GetRecordRequest request) -> boost::asio::awaitable<
      obcx::bot::BotOperationResult<std::unique_ptr<GetRecordOutcome>>>;

  auto set_group_anonymous_ban(SetGroupAnonymousBanRequest request)
      -> boost::asio::awaitable<obcx::bot::BotOperationResult<
          std::unique_ptr<SetGroupAnonymousBanOutcome>>>;

  auto set_friend_add_request(SetFriendAddRequestRequest request)
      -> boost::asio::awaitable<obcx::bot::BotOperationResult<
          std::unique_ptr<SetFriendAddRequestOutcome>>>;

  auto set_group_add_request(SetGroupAddRequestRequest request)
      -> boost::asio::awaitable<obcx::bot::BotOperationResult<
          std::unique_ptr<SetGroupAddRequestOutcome>>>;

  auto handle_quick_operation(HandleQuickOperationRequest request)
      -> boost::asio::awaitable<obcx::bot::BotOperationResult<
          std::unique_ptr<HandleQuickOperationOutcome>>>;

private:
  // The facade is not retained. The gateway follows the caller/generation's
  // existing lifetime obligation through completion of the owning coroutine.
  static auto send_like_owned(obcx::bot::BotOperationGateway &gateway,
                              SendLikeRequest request)
      -> boost::asio::awaitable<
          obcx::bot::BotOperationResult<std::unique_ptr<SendLikeOutcome>>>;
  obcx::bot::BotOperationGateway &gateway_;
};
} // namespace obcx::onebot11::bot::standard
