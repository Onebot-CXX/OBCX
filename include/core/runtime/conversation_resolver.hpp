#pragma once

#include "common/config_snapshot.hpp"
#include "core/bot/conversation_resolver.hpp"

#include <memory>

namespace obcx::core {
[[nodiscard]] auto make_conversation_resolver(
    std::shared_ptr<const common::RuntimeConfigSnapshot> snapshot)
    -> std::shared_ptr<bot::ConversationResolver>;
} // namespace obcx::core
