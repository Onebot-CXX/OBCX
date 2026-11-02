#pragma once

#include "core/bot/references.hpp"
#include "onebot11/bot/standard_common.hpp"

#include <string>

namespace obcx::onebot11::bot::standard {

struct GetLoginInfoParameters {
  obcx::bot::BotInstallationRef installation;
};

// Explicit provider observation, not a cache read or a new account authority.
struct LoginInfo {
  obcx::bot::BotInstallationRef installation;
  std::int64_t user_id;
  std::string nickname;
};

} // namespace obcx::onebot11::bot::standard
