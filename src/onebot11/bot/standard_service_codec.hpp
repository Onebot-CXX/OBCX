#pragma once

#include <initializer_list>
#include <string_view>

#include "common/json_utils.hpp"
#include "onebot11/bot/standard_common.hpp"
#include "onebot11/bot/standard_service.hpp"

namespace obcx::onebot11::bot::standard::detail {

using Json = common::json;

// Internal construction only, after cumulative borrowed-tree preflight.
class SensitiveValueFactory {
public:
  static auto text(std::string_view value) -> SensitiveText;
  static auto int32(std::int32_t value) -> SensitiveInt32;
  static auto extras(const Json &object,
                     std::initializer_list<std::string_view> known)
      -> ServiceExtraFields;
};

// Typed parameter overloads never accept action strings or execution modes.
// Parent registration binds each to onebot11.standard.<native_name>; suffix
// selection and response disposition belong to the shared execution seam.
auto encode_parameters(const GetCookiesParameters &, const ValueLimits &)
    -> Json;
auto encode_parameters(const GetCredentialsParameters &, const ValueLimits &)
    -> Json;
auto encode_parameters(const GetCsrfTokenParameters &, const ValueLimits &)
    -> Json;
auto encode_parameters(const GetStatusParameters &, const ValueLimits &)
    -> Json;
auto encode_parameters(const GetVersionInfoParameters &, const ValueLimits &)
    -> Json;
auto encode_parameters(const CanSendImageParameters &, const ValueLimits &)
    -> Json;
auto encode_parameters(const CanSendRecordParameters &, const ValueLimits &)
    -> Json;
auto encode_parameters(const SetRestartParameters &, const ValueLimits &)
    -> Json;
auto encode_parameters(const CleanCacheParameters &, const ValueLimits &)
    -> Json;

// Inspect ALL borrowed data, including unknown fields, before copying. Limits
// cover native/projected nodes and raw string/key bytes cumulatively; the text
// cap applies to each key/string. Depth starts at root zero. Wire framing and
// escaping need separate enclosing CallLimits accounting. No transport or
// raw parsing occurs here. Duplicate-key rejection and pre-DOM frame/parser
// limits must be enforced upstream; an existing DOM cannot recover duplicates.
auto decode_get_cookies_data(const Json &, const ValueLimits &) -> CookiesData;
auto decode_get_csrf_token_data(const Json &, const ValueLimits &)
    -> CsrfTokenData;
auto decode_get_credentials_data(const Json &, const ValueLimits &)
    -> CredentialsData;
auto decode_get_status_data(const Json &, const ValueLimits &) -> StatusData;
auto decode_get_version_info_data(const Json &, const ValueLimits &)
    -> VersionInfoData;
auto decode_can_send_image_data(const Json &, const ValueLimits &)
    -> CanSendImageData;
auto decode_can_send_record_data(const Json &, const ValueLimits &)
    -> CanSendRecordData;
// No-data operations require native null after bounded input preflight. The
// enclosing binding must require data to exist, never synthesize missing null.
// These functions do not classify ok/async or prove side-effect completion;
// pending restart still requires separate pending-envelope validation.
auto decode_set_restart_data(const Json &, const ValueLimits &)
    -> ServiceNoData;
auto decode_clean_cache_data(const Json &, const ValueLimits &)
    -> ServiceNoData;

} // namespace obcx::onebot11::bot::standard::detail
