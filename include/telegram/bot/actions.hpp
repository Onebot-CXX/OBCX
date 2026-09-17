#ifndef OBCX_INCLUDE_TELEGRAM_BOT_ACTIONS_HPP_
#define OBCX_INCLUDE_TELEGRAM_BOT_ACTIONS_HPP_

#include "core/bot/ids.hpp"

namespace obcx::telegram::bot {

inline const obcx::bot::SurfaceId surface{"telegram.bot_api"};

namespace actions {
inline const obcx::bot::ActionId send_topic{"telegram.message.send_topic"};
inline const obcx::bot::ActionId edit_text{"telegram.message.edit_text"};
inline const obcx::bot::ActionId send_photo{"telegram.media.send_photo"};
inline const obcx::bot::ActionId send_photo_upload{
    "telegram.media.send_photo_upload"};
inline const obcx::bot::ActionId send_group_urls{
    "telegram.media.send_group_urls"};
inline const obcx::bot::ActionId send_group_uploads{
    "telegram.media.send_group_uploads"};
inline const obcx::bot::ActionId fetch_file{"telegram.media.fetch_file"};
} // namespace actions

} // namespace obcx::telegram::bot

#endif // OBCX_INCLUDE_TELEGRAM_BOT_ACTIONS_HPP_
