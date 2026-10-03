#ifndef OBCX_INCLUDE_ONEBOT11_BOT_CAPABILITY_IDS_HPP_
#define OBCX_INCLUDE_ONEBOT11_BOT_CAPABILITY_IDS_HPP_

#include "core/bot/capability_ids.hpp"
#include <string_view>

namespace obcx::onebot11::bot::capability_ids {

inline constexpr std::string_view protocol = "onebot11.protocol";
inline constexpr std::string_view transport = "onebot11.transport";

} // namespace obcx::onebot11::bot::capability_ids

#endif
