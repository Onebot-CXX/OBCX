#pragma once

// QQ process transport seam, not an actor SDK or a second identity authority.
#include "common/message_type.hpp"
#include "core/bot/self_identity_cache.hpp"
#include <functional>

namespace obcx::network::onebot11 {
using OriginCapture = std::function<std::optional<core::SelfIdentityOrigin>()>;
using EventCallback = std::function<core::SelfObservationResult(
    const common::Event &, const std::optional<core::SelfIdentityOrigin> &)>;
using OriginValidation = std::function<core::SelfObservationResult(
    const core::SelfIdentityOrigin &)>;
using ProfilePublication = std::function<core::SelfIdentityCache::Result(
    std::string_view, const core::SelfIdentityOrigin &)>;

inline auto retired_profile() -> core::SelfIdentityCache::Result {
  return core::SelfIdentityCache::Result::failure(
      {.code = bot::BotOperationErrorCode::CacheUnavailable,
       .message = "QQ profile originating connection or account retired",
       .retryable = false,
       .submission_safety = bot::SubmissionSafety::DefinitelyNotSubmitted});
}
} // namespace obcx::network::onebot11
