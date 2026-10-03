#ifndef OBCX_INCLUDE_CORE_BOT_CONFIGURATION_ERROR_HPP_
#define OBCX_INCLUDE_CORE_BOT_CONFIGURATION_ERROR_HPP_

#include <stdexcept>
#include <string>
#include <utility>

namespace obcx::core {

// Diagnostics contain schema paths and rules, never rejected credential values.
class BotConfigurationError final : public std::runtime_error {
public:
  BotConfigurationError(std::string code, std::string path,
                        const std::string &message)
      : std::runtime_error(message), code_(std::move(code)),
        path_(std::move(path)) {}
  [[nodiscard]] auto code() const noexcept -> const std::string & {
    return code_;
  }
  [[nodiscard]] auto path() const noexcept -> const std::string & {
    return path_;
  }

private:
  std::string code_;
  std::string path_;
};

} // namespace obcx::core

#endif
