#pragma once

#include "core/bot/ids.hpp"

namespace obcx::onebot11::bot {

inline const obcx::bot::SurfaceId surface{"onebot11.qq"};

namespace actions {
inline const obcx::bot::ActionId get_user_info{"onebot11.user_info.get"};
inline const obcx::bot::ActionId get_login_info{"onebot11.login_info.get"};
inline const obcx::bot::ActionId get_image{"onebot11.image.get"};
inline const obcx::bot::ActionId get_group_member{"onebot11.group_member.get"};
inline const obcx::bot::ActionId get_forward_message{
    "onebot11.forward_message.get"};
inline const obcx::bot::ActionId resolve_group_file{
    "onebot11.group_file.resolve"};
inline const obcx::bot::ActionId resolve_private_file{
    "onebot11.private_file.resolve"};
inline const obcx::bot::ActionId poke_group{"onebot11.group.poke"};
inline const obcx::bot::ActionId set_message_emoji_like{
    "onebot11.message.emoji_like.set"};
} // namespace actions

} // namespace obcx::onebot11::bot
