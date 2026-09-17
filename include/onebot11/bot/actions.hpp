#ifndef OBCX_INCLUDE_ONEBOT11_BOT_ACTIONS_HPP_
#define OBCX_INCLUDE_ONEBOT11_BOT_ACTIONS_HPP_

#include "core/bot/ids.hpp"

namespace obcx::onebot11::bot {

inline const obcx::bot::SurfaceId surface{"onebot11.qq"};

namespace actions {
inline const obcx::bot::ActionId get_group_member{"onebot11.group_member.get"};
inline const obcx::bot::ActionId get_forward_message{
    "onebot11.forward_message.get"};
inline const obcx::bot::ActionId resolve_group_file{
    "onebot11.group_file.resolve"};
inline const obcx::bot::ActionId resolve_private_file{
    "onebot11.private_file.resolve"};
inline const obcx::bot::ActionId poke_group{"onebot11.group.poke"};
} // namespace actions

} // namespace obcx::onebot11::bot

#endif // OBCX_INCLUDE_ONEBOT11_BOT_ACTIONS_HPP_
