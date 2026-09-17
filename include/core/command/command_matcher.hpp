#ifndef OBCX_INCLUDE_CORE_COMMAND_MATCHER_HPP_
#define OBCX_INCLUDE_CORE_COMMAND_MATCHER_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace re2 {
class RE2;
}

namespace obcx::core {

inline constexpr std::size_t command_re2_max_pattern_bytes = 4U * 1024U;
inline constexpr std::size_t command_candidate_max_bytes = 256U;
inline constexpr std::int64_t command_re2_max_memory_bytes = 1U * 1024U * 1024U;

struct CommandPatternCompileResult {
  std::shared_ptr<const re2::RE2> compiled;
  std::string code;
  std::string message;

  explicit operator bool() const noexcept { return compiled != nullptr; }
};

[[nodiscard]] auto compile_command_re2(std::string_view pattern)
    -> CommandPatternCompileResult;

[[nodiscard]] auto command_re2_full_match(const re2::RE2 &compiled,
                                          std::string_view candidate) -> bool;

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_COMMAND_MATCHER_HPP_
