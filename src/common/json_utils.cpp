#include "common/json_utils.hpp"
#include "common/logger.hpp"
#include <algorithm>

namespace obcx::common {

auto JsonUtils::validate_required_fields(
    const json &j, const std::vector<std::string> &required_fields) -> bool {
  return std::ranges::all_of(required_fields, [&j](const std::string &field) {
    return j.contains(field) && !j[field].is_null();
  });
}

void JsonUtils::merge(json &target, const json &source, bool overwrite) {
  for (const auto &[key, value] : source.items()) {
    if (!target.contains(key) || overwrite) {
      target[key] = value;
    } else if (target[key].is_object() && value.is_object()) {
      // 递归合并对象
      merge(target[key], value, overwrite);
    }
  }
}

auto JsonUtils::pretty_print(const json &j, int indent) -> std::string {
  try {
    return j.dump(indent);
  } catch (const json::exception &e) {
    OBCX_ERROR("Failed to pretty print JSON: {}", e.what());
    return "{}";
  }
}

auto JsonUtils::parse(const std::string &str) -> std::optional<json> {
  try {
    return json::parse(str);
  } catch (const json::exception &e) {
    OBCX_ERROR("Failed to parse JSON: {}", e.what());
    return std::nullopt;
  }
}

auto JsonUtils::has_path(const json &j, const std::string &path) -> bool {
  try {
    auto ptr = json::json_pointer(path);
    return j.contains(ptr);
  } catch (const json::exception &) {
    return false;
  }
}

auto JsonUtils::get_id_as_string(const json &j, const std::string &key)
    -> std::string {
  if (j.contains(key)) {
    const auto &value = j.at(key);
    if (value.is_string()) {
      return value.get<std::string>();
    }
    if (value.is_number()) {
      return std::to_string(value.get<long long>());
    }
  }
  return "";
}

auto JsonUtils::get_optional_id_as_string(const json &j, const std::string &key)
    -> std::optional<std::string> {
  if (j.contains(key)) {
    const auto &value = j.at(key);
    if (value.is_string()) {
      return value.get<std::string>();
    }
    if (value.is_number()) {
      return std::to_string(value.get<long long>());
    }
  }
  return std::nullopt;
}

} // namespace obcx::common
