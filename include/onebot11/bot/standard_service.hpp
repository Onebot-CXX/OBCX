#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "onebot11/bot/standard_sensitive.hpp"

namespace obcx::onebot11::bot::standard {

// Pure native parameters, not invocation/installation authority. Callers must
// explicitly choose omission (nullopt) or a domain, including the empty domain.
struct GetCookiesParameters {
  explicit GetCookiesParameters(std::optional<std::string> requested_domain)
      : domain(std::move(requested_domain)) {}
  std::optional<std::string> domain;
};
struct GetCredentialsParameters {
  explicit GetCredentialsParameters(std::optional<std::string> requested_domain)
      : domain(std::move(requested_domain)) {}
  std::optional<std::string> domain;
};
struct GetCsrfTokenParameters {};
struct GetStatusParameters {};
struct GetVersionInfoParameters {};
struct CanSendImageParameters {};
struct CanSendRecordParameters {};
struct CleanCacheParameters {};

class RestartDelayMilliseconds {
public:
  using Number = std::variant<std::int64_t, std::uint64_t, double>;
  explicit RestartDelayMilliseconds(Number value);
  auto value() const -> const Number &;

private:
  Number value_;
};
struct SetRestartParameters {
  explicit SetRestartParameters(
      std::optional<RestartDelayMilliseconds> requested_delay)
      : delay(std::move(requested_delay)) {}
  std::optional<RestartDelayMilliseconds> delay;
};

// Decoded data only: these do not assert installation, response disposition,
// credential scope/expiry, or provider completion. The structured executor
// must bind result ownership and only decode data for an appropriate envelope.
struct CookiesData {
  SensitiveText cookies;
};
struct CsrfTokenData {
  SensitiveInt32 token;
};
struct CredentialsData {
  SensitiveText cookies;
  SensitiveInt32 csrf_token;
};
struct StatusData {
  std::optional<bool> online; // nullopt means actual native null, not missing.
  bool good;
  ServiceExtraFields extra;
};
struct VersionInfoData {
  std::string app_name;
  std::string app_version;
  std::string protocol_version;
  ServiceExtraFields extra;
};
struct CanSendImageData {
  bool yes;
};
struct CanSendRecordData {
  bool yes;
};
// No operation-specific fields; in particular this is not restart evidence.
// Base set_restart is pending, as are async/rate-limited acknowledgements.
struct ServiceNoData {};

} // namespace obcx::onebot11::bot::standard
