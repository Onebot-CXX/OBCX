#pragma once
#include "core/command/command_platform_adapter.hpp"
#include <string>
namespace obcx::onebot11::bot {
[[nodiscard]] auto make_command_adapter()
    -> std::shared_ptr<core::ICommandPlatformAdapter>;
}
