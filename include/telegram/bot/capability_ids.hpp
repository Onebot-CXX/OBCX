#ifndef OBCX_INCLUDE_TELEGRAM_BOT_CAPABILITY_IDS_HPP_
#define OBCX_INCLUDE_TELEGRAM_BOT_CAPABILITY_IDS_HPP_

#include "core/bot/capability_ids.hpp"
#include <string_view>

namespace obcx::telegram::bot::capability_ids {

inline constexpr std::string_view protocol = "telegram.protocol";
inline constexpr std::string_view transport = "telegram.transport";
inline constexpr std::string_view media_upload = "telegram.media-upload";
inline constexpr std::string_view command_catalog = "telegram.command-catalog";

} // namespace obcx::telegram::bot::capability_ids

#endif
