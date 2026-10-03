#include "core/command/command_matcher.hpp"

#include <cstdint>
#include <re2/re2.h>

namespace obcx::core {

auto compile_command_re2(const std::string_view pattern)
    -> CommandPatternCompileResult {
  if (pattern.empty()) {
    return {
        .code = "command_pattern_empty",
        .message = "RE2 command pattern must not be empty",
    };
  }
  if (pattern.size() > command_re2_max_pattern_bytes) {
    return {
        .code = "command_pattern_too_large",
        .message = "RE2 command pattern exceeds the fixed byte limit",
    };
  }

  re2::RE2::Options options;
  options.set_encoding(re2::RE2::Options::EncodingUTF8);
  options.set_log_errors(false);
  options.set_max_mem(command_re2_max_memory_bytes);
  std::shared_ptr<const re2::RE2> compiled;
  try {
    compiled = std::make_shared<const re2::RE2>(
        re2::StringPiece{pattern.data(), pattern.size()}, options);
  } catch (...) {
    return {
        .code = "command_pattern_resource_limit",
        .message = "RE2 command pattern compilation exceeded a resource limit",
    };
  }
  if (!compiled->ok()) {
    if (compiled->error_code() == re2::RE2::ErrorPatternTooLarge) {
      return {
          .code = "command_pattern_resource_limit",
          .message =
              "RE2 command pattern compilation exceeded a resource limit",
      };
    }
    return {
        .code = "command_pattern_invalid",
        .message = "RE2 command pattern is invalid",
    };
  }
  return {.compiled = std::move(compiled)};
}

auto command_re2_full_match(const re2::RE2 &compiled,
                            const std::string_view candidate) -> bool {
  if (candidate.size() > command_candidate_max_bytes) {
    return false;
  }
  return re2::RE2::FullMatch(
      re2::StringPiece{candidate.data(), candidate.size()}, compiled);
}

} // namespace obcx::core
