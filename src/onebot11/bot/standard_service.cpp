#include "onebot11/bot/standard_service.hpp"

#include "standard_service_codec.hpp"

#include "core/bot/validation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace obcx::onebot11::bot::standard {

RestartDelayMilliseconds::RestartDelayMilliseconds(Number value)
    : value_(std::move(value)) {
  if (const auto *floating = std::get_if<double>(&value_);
      floating && !std::isfinite(*floating))
    throw std::invalid_argument("restart delay must be finite");
}
auto RestartDelayMilliseconds::value() const -> const Number & {
  return value_;
}

namespace detail {
namespace {
[[noreturn]] void invalid_data() {
  throw std::invalid_argument("invalid standard service data");
}
[[noreturn]] void over_limit() {
  throw std::invalid_argument("standard service value exceeds limits");
}

// Owned-value accounting: cumulative raw string/key bytes and value nodes,
// with an individual byte cap for each string/key. Wire escaping, punctuation
// and numeric spelling belong to enclosing CallLimits, not ValueLimits.
// One ledger covers borrowed input and projection, including ignored fields.
class ServiceBudget {
public:
  explicit ServiceBudget(const ValueLimits &limits) : limits_(limits) {
    limits.validate();
  }
  void bytes(std::size_t count) {
    charge(count, bytes_,
           std::min(limits_.maximum_bytes,
                    obcx::bot::detail::domain_maximum_bytes));
  }
  void node(std::size_t depth) {
    if (depth > std::min(limits_.maximum_depth, std::size_t{32}))
      over_limit();
    charge(1, nodes_,
           std::min(limits_.maximum_nodes,
                    obcx::bot::detail::domain_maximum_items));
  }
  void text(std::string_view value) {
    if (value.size() > std::min(limits_.maximum_text_bytes,
                                obcx::bot::detail::domain_maximum_bytes))
      over_limit();
    bytes(value.size());
    for (std::size_t i = 0; i < value.size();) {
      const auto first = static_cast<unsigned char>(value[i]);
      if (first < 0x80) {
        ++i;
        continue;
      }
      // Validate Unicode before Json serialization could throw a diagnostic
      // containing native string bytes. Preserve valid spelling verbatim.
      const std::size_t width = first >= 0xC2 && first <= 0xDF   ? 2
                                : first >= 0xE0 && first <= 0xEF ? 3
                                : first >= 0xF0 && first <= 0xF4 ? 4
                                                                 : 0;
      if (width == 0 || width > value.size() - i)
        invalid_data();
      for (std::size_t j = 1; j < width; ++j) {
        const auto next = static_cast<unsigned char>(value[i + j]);
        if (next < 0x80 || next > 0xBF)
          invalid_data();
      }
      const auto second = static_cast<unsigned char>(value[i + 1]);
      if ((first == 0xE0 && second < 0xA0) ||
          (first == 0xED && second > 0x9F) ||
          (first == 0xF0 && second < 0x90) || (first == 0xF4 && second > 0x8F))
        invalid_data();
      i += width;
    }
  }
  void inspect(const Json &value, std::size_t depth) {
    node(depth);
    if (value.is_object() || value.is_array()) {
      for (auto it = value.begin(); it != value.end(); ++it) {
        if (value.is_object())
          text(it.key());
        inspect(it.value(), depth + 1);
      }
    } else if (value.is_string()) {
      text(value.get_ref<const std::string &>());
    } else if (value.is_boolean() || value.is_null()) {
      // Scalar nodes have no owned string/key bytes.
    } else if (value.is_number()) {
      if (value.is_number_float() && !std::isfinite(value.get<double>()))
        invalid_data();
    } else {
      invalid_data(); // Binary/discarded are not standard JSON values.
    }
  }
  void object(std::size_t depth) { node(depth); }
  void field(std::string_view name, const Json &value, std::size_t depth) {
    text(name);
    inspect(value, depth);
  }

private:
  static void charge(std::size_t count, std::size_t &used,
                     std::size_t maximum) {
    if (count > maximum - used)
      over_limit();
    used += count;
  }
  const ValueLimits &limits_;
  std::size_t bytes_{};
  std::size_t nodes_{};
};

auto member(const Json &data, std::string_view name) -> const Json & {
  if (!data.is_object())
    invalid_data();
  const auto found = data.find(name);
  if (found == data.end())
    invalid_data();
  return *found;
}
auto string_member(const Json &data, std::string_view name)
    -> const std::string & {
  const auto &value = member(data, name);
  if (!value.is_string())
    invalid_data();
  return value.get_ref<const std::string &>();
}
auto bool_member(const Json &data, std::string_view name) -> bool {
  const auto &value = member(data, name);
  if (!value.is_boolean())
    invalid_data();
  return value.get<bool>();
}
auto int32_member(const Json &data, std::string_view name) -> std::int32_t {
  const auto &value = member(data, name);
  if (value.is_number_unsigned()) {
    const auto number = value.get<std::uint64_t>();
    if (number >
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
      invalid_data();
    return static_cast<std::int32_t>(number);
  }
  if (!value.is_number_integer())
    invalid_data();
  const auto number = value.get<std::int64_t>();
  if (number < std::numeric_limits<std::int32_t>::min() ||
      number > std::numeric_limits<std::int32_t>::max())
    invalid_data();
  return static_cast<std::int32_t>(number);
}
void project_fields(ServiceBudget &budget, const Json &data,
                    std::initializer_list<std::string_view> names) {
  budget.object(0);
  // DTO member names are not retained strings; their values still contribute
  // logical nodes/depth and any actual copied string bytes.
  for (const auto name : names)
    budget.inspect(member(data, name), 1);
}
void project_with_extras(ServiceBudget &budget, const Json &data,
                         std::initializer_list<std::string_view> known) {
  project_fields(budget, data, known);
  budget.object(1);
  for (auto it = data.begin(); it != data.end(); ++it) {
    if (std::find(known.begin(), known.end(), it.key()) == known.end()) {
      budget.field(it.key(), it.value(), 2);
    }
  }
}
auto encode_domain(const std::optional<std::string> &domain,
                   const ValueLimits &limits) -> Json {
  ServiceBudget budget{limits};
  // Parameter source and native projection share one budget. Do not construct
  // the native Json string until both reservations have succeeded.
  if (domain) {
    budget.node(0);
    budget.text(*domain);
  }
  budget.object(0);
  if (domain) {
    budget.text("domain");
    budget.node(1);
    budget.text(*domain);
  }
  auto parameters = Json::object();
  if (domain)
    parameters["domain"] = *domain;
  return parameters;
}
auto encode_empty(const ValueLimits &limits) -> Json {
  ServiceBudget budget{limits};
  budget.object(0);
  return Json::object();
}
} // namespace

auto encode_parameters(const GetCookiesParameters &parameters,
                       const ValueLimits &limits) -> Json {
  return encode_domain(parameters.domain, limits);
}
auto encode_parameters(const GetCredentialsParameters &parameters,
                       const ValueLimits &limits) -> Json {
  return encode_domain(parameters.domain, limits);
}
auto encode_parameters(const GetCsrfTokenParameters &,
                       const ValueLimits &limits) -> Json {
  return encode_empty(limits);
}
auto encode_parameters(const GetStatusParameters &, const ValueLimits &limits)
    -> Json {
  return encode_empty(limits);
}
auto encode_parameters(const GetVersionInfoParameters &,
                       const ValueLimits &limits) -> Json {
  return encode_empty(limits);
}
auto encode_parameters(const CanSendImageParameters &,
                       const ValueLimits &limits) -> Json {
  return encode_empty(limits);
}
auto encode_parameters(const CanSendRecordParameters &,
                       const ValueLimits &limits) -> Json {
  return encode_empty(limits);
}
auto encode_parameters(const CleanCacheParameters &, const ValueLimits &limits)
    -> Json {
  return encode_empty(limits);
}
auto encode_parameters(const SetRestartParameters &parameters,
                       const ValueLimits &limits) -> Json {
  ServiceBudget budget{limits};
  budget.object(0);
  if (!parameters.delay)
    return Json::object();
  // A scalar cannot allocate a native payload tree or expose string values.
  const auto delay = std::visit([](auto number) { return Json(number); },
                                parameters.delay->value());
  budget.inspect(delay, 0);
  budget.field("delay", delay, 1);
  auto result = Json::object();
  result["delay"] = delay;
  return result;
}

auto decode_get_cookies_data(const Json &data, const ValueLimits &limits)
    -> CookiesData {
  ServiceBudget budget{limits};
  budget.inspect(data, 0);
  const auto &cookies = string_member(data, "cookies");
  project_fields(budget, data, {"cookies"});
  return {SensitiveValueFactory::text(cookies)};
}
auto decode_get_csrf_token_data(const Json &data, const ValueLimits &limits)
    -> CsrfTokenData {
  ServiceBudget budget{limits};
  budget.inspect(data, 0);
  const auto token = int32_member(data, "token");
  project_fields(budget, data, {"token"});
  return {SensitiveValueFactory::int32(token)};
}
auto decode_get_credentials_data(const Json &data, const ValueLimits &limits)
    -> CredentialsData {
  ServiceBudget budget{limits};
  budget.inspect(data, 0);
  const auto &cookies = string_member(data, "cookies");
  const auto token = int32_member(data, "csrf_token");
  project_fields(budget, data, {"cookies", "csrf_token"});
  return {SensitiveValueFactory::text(cookies),
          SensitiveValueFactory::int32(token)};
}
auto decode_get_status_data(const Json &data, const ValueLimits &limits)
    -> StatusData {
  ServiceBudget budget{limits};
  budget.inspect(data, 0);
  const auto &online = member(data, "online");
  if (!online.is_null() && !online.is_boolean())
    invalid_data();
  const auto good = bool_member(data, "good");
  project_with_extras(budget, data, {"online", "good"});
  return {online.is_null() ? std::nullopt : std::optional{online.get<bool>()},
          good, SensitiveValueFactory::extras(data, {"online", "good"})};
}
auto decode_get_version_info_data(const Json &data, const ValueLimits &limits)
    -> VersionInfoData {
  ServiceBudget budget{limits};
  budget.inspect(data, 0);
  const auto &app_name = string_member(data, "app_name");
  const auto &app_version = string_member(data, "app_version");
  const auto &protocol_version = string_member(data, "protocol_version");
  project_with_extras(budget, data,
                      {"app_name", "app_version", "protocol_version"});
  return {app_name, app_version, protocol_version,
          SensitiveValueFactory::extras(
              data, {"app_name", "app_version", "protocol_version"})};
}
auto decode_can_send_image_data(const Json &data, const ValueLimits &limits)
    -> CanSendImageData {
  ServiceBudget budget{limits};
  budget.inspect(data, 0);
  const auto yes = bool_member(data, "yes");
  project_fields(budget, data, {"yes"});
  return {yes};
}
auto decode_can_send_record_data(const Json &data, const ValueLimits &limits)
    -> CanSendRecordData {
  ServiceBudget budget{limits};
  budget.inspect(data, 0);
  const auto yes = bool_member(data, "yes");
  project_fields(budget, data, {"yes"});
  return {yes};
}
auto decode_set_restart_data(const Json &data, const ValueLimits &limits)
    -> ServiceNoData {
  ServiceBudget{limits}.inspect(data, 0);
  if (!data.is_null())
    invalid_data();
  return {};
}
auto decode_clean_cache_data(const Json &data, const ValueLimits &limits)
    -> ServiceNoData {
  ServiceBudget{limits}.inspect(data, 0);
  if (!data.is_null())
    invalid_data();
  return {};
}

} // namespace detail
} // namespace obcx::onebot11::bot::standard
