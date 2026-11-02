#include "standard_login_codec.hpp"

#include <cmath>
#include <limits>

namespace obcx::onebot11::bot::standard::detail {
namespace {
namespace domain = obcx::bot;

class LoginBudget {
public:
  explicit LoginBudget(const ValueLimits &limits) : limits_(limits) {
    limits_.validate();
  }
  void node(std::size_t depth) {
    if (depth > limits_.maximum_depth || depth > 32 ||
        nodes_ >= limits_.maximum_nodes ||
        nodes_ >= domain::detail::domain_maximum_items)
      throw std::invalid_argument("standard login value exceeds shape limit");
    ++nodes_;
  }
  void text(std::string_view value) {
    if (value.size() > limits_.maximum_text_bytes ||
        value.size() > limits_.maximum_bytes - bytes_ ||
        value.size() > domain::detail::domain_maximum_bytes - bytes_)
      throw std::invalid_argument("standard login value exceeds text limit");
    bytes_ += value.size();
    domain::detail::validate_utf8(value);
  }
  void scope(const domain::BotInstallationRef &scope, std::size_t depth) {
    node(depth);
    node(depth + 1);
    text(scope.installation_id);
    node(depth + 1);
    text(scope.surface.value());
  }
  void inspect(const domain::Json &value, std::size_t depth) {
    node(depth);
    if (value.is_string())
      text(value.get_ref<const std::string &>());
    else if (value.is_structured()) {
      for (auto it = value.begin(); it != value.end(); ++it) {
        if (value.is_object())
          text(it.key());
        inspect(it.value(), depth + 1);
      }
    } else if (value.is_binary() || value.is_discarded() ||
               (value.is_number_float() && !std::isfinite(value.get<double>())))
      throw std::invalid_argument("invalid standard login value");
  }

private:
  const ValueLimits &limits_;
  std::size_t bytes_{};
  std::size_t nodes_{};
};
void login_scope(const GetLoginInfoParameters &parameters,
                 const domain::binding::NativeReferenceCodec &codec) {
  codec.validate(parameters.installation);
  if (parameters.installation.surface.value() != "onebot11.qq")
    throw std::invalid_argument("invalid standard login installation");
}
} // namespace

auto encode_get_login_info(const GetLoginInfoParameters &parameters,
                           const domain::binding::NativeReferenceCodec &codec,
                           const ValueLimits &limits) -> domain::Json {
  LoginBudget budget(limits);
  budget.node(0);
  budget.scope(parameters.installation, 1);
  budget.node(0); // Native empty params object.
  login_scope(parameters, codec);
  return domain::Json::object();
}

auto decode_get_login_info(const domain::Json &data,
                           const GetLoginInfoParameters &parameters,
                           const domain::binding::NativeReferenceCodec &codec,
                           const ValueLimits &limits) -> LoginInfo {
  LoginBudget budget(limits);
  budget.node(0);
  budget.scope(parameters.installation, 1);
  budget.inspect(data, 0);
  domain::detail::require_object(data, "standard login data");
  const auto &id = data.at("user_id");
  if ((!id.is_number_integer() && !id.is_number_unsigned()) ||
      (id.is_number_unsigned() &&
       id.get<std::uint64_t>() > static_cast<std::uint64_t>(INT64_MAX)))
    throw std::invalid_argument("invalid standard login identity");
  const auto user_id = id.get<std::int64_t>();
  if (user_id <= 0 || !data.at("nickname").is_string())
    throw std::invalid_argument("invalid standard login identity");
  const auto &nickname = data.at("nickname").get_ref<const std::string &>();
  // Bound the full borrowed source and retained projection before copying.
  budget.node(0);
  budget.scope(parameters.installation, 1);
  budget.node(1);
  budget.node(1);
  budget.text(nickname);
  login_scope(parameters, codec);
  return {parameters.installation, user_id, nickname};
}
} // namespace obcx::onebot11::bot::standard::detail
