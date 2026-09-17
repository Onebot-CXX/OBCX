#ifndef OBCX_INCLUDE_CORE_BOT_CONFIGURATION_FINGERPRINT_HPP_
#define OBCX_INCLUDE_CORE_BOT_CONFIGURATION_FINGERPRINT_HPP_

#include <array>
#include <iomanip>
#include <openssl/sha.h>
#include <sstream>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>

namespace obcx::core {

[[nodiscard]] inline auto configuration_digest(const std::string_view content)
    -> std::string {
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
  SHA256(reinterpret_cast<const unsigned char *>(content.data()),
         content.size(), digest.data());
  std::ostringstream encoded;
  encoded << std::hex << std::setfill('0');
  for (const auto byte : digest) {
    encoded << std::setw(2) << static_cast<unsigned int>(byte);
  }
  return encoded.str();
}

// Only call after the owning module has validated its closed connection schema.
// Formatting normalizes TOML spelling/order; plaintext is never
// returned/logged.
[[nodiscard]] inline auto configuration_digest(const toml::table &configuration)
    -> std::string {
  std::ostringstream normalized;
  normalized << toml::json_formatter{configuration};
  return configuration_digest(normalized.str());
}

} // namespace obcx::core

#endif
