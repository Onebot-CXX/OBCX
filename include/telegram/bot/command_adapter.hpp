#pragma once
#include "core/command/command_platform_adapter.hpp"
#include <string>
namespace obcx::telegram::bot {
[[nodiscard]] auto make_command_adapter(std::string bot_target)
    -> std::shared_ptr<core::ICommandPlatformAdapter>;
}
