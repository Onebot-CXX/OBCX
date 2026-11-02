#pragma once

// Process-private pure codecs. The executor supplies the selected installation;
// suffix selection, envelopes, byte/deadline controls and completion belong to
// the execution layer, not these parameter serializers.
#include "common/json_utils.hpp"
#include "onebot11/bot/standard_common.hpp"
#include "onebot11/bot/standard_mutations.hpp"

#include <string_view>

namespace obcx::onebot11::bot::standard::detail {

// No open action string or generic parameter overload is accepted.
inline constexpr auto native_action(const SendLikeParameters &) noexcept
    -> std::string_view {
  return "send_like";
}
auto encode_parameters(const SendLikeParameters &parameters,
                       const obcx::bot::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json;

inline constexpr auto native_action(const DeleteMsgParameters &) noexcept
    -> std::string_view {
  return "delete_msg";
}
auto encode_parameters(const DeleteMsgParameters &parameters,
                       const obcx::bot::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json;

inline constexpr auto native_action(const SetGroupKickParameters &) noexcept
    -> std::string_view {
  return "set_group_kick";
}
auto encode_parameters(const SetGroupKickParameters &parameters,
                       const obcx::bot::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json;

inline constexpr auto native_action(const SetGroupBanParameters &) noexcept
    -> std::string_view {
  return "set_group_ban";
}
auto encode_parameters(const SetGroupBanParameters &parameters,
                       const obcx::bot::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json;

inline constexpr auto native_action(const SetGroupWholeBanParameters &) noexcept
    -> std::string_view {
  return "set_group_whole_ban";
}
auto encode_parameters(const SetGroupWholeBanParameters &parameters,
                       const obcx::bot::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json;

inline constexpr auto native_action(const SetGroupAdminParameters &) noexcept
    -> std::string_view {
  return "set_group_admin";
}
auto encode_parameters(const SetGroupAdminParameters &parameters,
                       const obcx::bot::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json;

inline constexpr auto native_action(
    const SetGroupAnonymousParameters &) noexcept -> std::string_view {
  return "set_group_anonymous";
}
auto encode_parameters(const SetGroupAnonymousParameters &parameters,
                       const obcx::bot::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json;

inline constexpr auto native_action(const SetGroupCardParameters &) noexcept
    -> std::string_view {
  return "set_group_card";
}
auto encode_parameters(const SetGroupCardParameters &parameters,
                       const obcx::bot::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json;

inline constexpr auto native_action(const SetGroupNameParameters &) noexcept
    -> std::string_view {
  return "set_group_name";
}
auto encode_parameters(const SetGroupNameParameters &parameters,
                       const obcx::bot::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json;

inline constexpr auto native_action(const SetGroupLeaveParameters &) noexcept
    -> std::string_view {
  return "set_group_leave";
}
auto encode_parameters(const SetGroupLeaveParameters &parameters,
                       const obcx::bot::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json;

inline constexpr auto native_action(
    const SetGroupSpecialTitleParameters &) noexcept -> std::string_view {
  return "set_group_special_title";
}
auto encode_parameters(const SetGroupSpecialTitleParameters &parameters,
                       const obcx::bot::BotInstallationRef &installation,
                       const ValueLimits &limits) -> common::json;

} // namespace obcx::onebot11::bot::standard::detail
