#include "core/bot/gateway_codec.hpp"
#include "standard_lookup_codec.hpp"
#include "standard_service_codec.hpp"
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace obcx::onebot11::bot::standard {
DetachedContent::DetachedContent(DetachedContentEncoding encoding,
                                 std::vector<DetachedSegment> segments)
    : encoding_(encoding), segments_(std::move(segments)) {}
namespace detail {
class LookupContentAccess {
public:
  static auto make(DetachedContentEncoding encoding,
                   std::vector<DetachedSegment> segments) -> DetachedContent {
    return DetachedContent{encoding, std::move(segments)};
  }
};
namespace {
using Json = QueryJson;
[[noreturn]] void lookup_malformed() {
  throw std::invalid_argument("invalid detached standard lookup observation");
}
class LookupBudget {
public:
  explicit LookupBudget(const ValueLimits &limits) : limits_(limits) {
    limits_.validate();
  }
  void node(std::size_t depth) {
    if (depth > limits_.maximum_depth || nodes_ >= limits_.maximum_nodes)
      lookup_malformed();
    ++nodes_;
  }
  void text(std::string_view value) {
    if (value.size() > limits_.maximum_text_bytes ||
        value.size() > limits_.maximum_bytes - bytes_)
      lookup_malformed();
    obcx::bot::detail::validate_utf8(value);
    bytes_ += value.size();
  }
  void inspect(const Json &value, std::size_t depth) {
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
      lookup_malformed();
  }

private:
  const ValueLimits &limits_;
  std::size_t bytes_{};
  std::size_t nodes_{};
};
auto lookup_text(const Json &value) -> const std::string & {
  if (!value.is_string())
    lookup_malformed();
  return value.get_ref<const std::string &>();
}
auto lookup_integer(const Json &value) -> std::int64_t {
  if (value.is_number_unsigned()) {
    const auto n = value.get<std::uint64_t>();
    if (n > INT64_MAX)
      lookup_malformed();
    return static_cast<std::int64_t>(n);
  }
  if (!value.is_number_integer())
    lookup_malformed();
  return value.get<std::int64_t>();
}
auto lookup_int32(const Json &value) -> std::int32_t {
  const auto n = lookup_integer(value);
  if (!std::in_range<std::int32_t>(n))
    lookup_malformed();
  return static_cast<std::int32_t>(n);
}
auto lookup_boolean(const Json &value) -> bool {
  if (value.is_boolean())
    return value.get<bool>();
  if (value.is_number_integer()) {
    const auto n = lookup_integer(value);
    if (n == 0 || n == 1)
      return n == 1;
  }
  if (value.is_string()) {
    const auto &s = lookup_text(value);
    if (s == "1" || s == "yes" || s == "true")
      return true;
    if (s == "0" || s == "no" || s == "false")
      return false;
  }
  lookup_malformed();
}
auto lookup_sex(const Json &value) -> Sex {
  const auto &s = lookup_text(value);
  if (s == "male")
    return Sex::Male;
  if (s == "female")
    return Sex::Female;
  if (s == "unknown")
    return Sex::Unknown;
  lookup_malformed();
}
auto lookup_role(const Json &value) -> GroupRole {
  const auto &s = lookup_text(value);
  if (s == "owner")
    return GroupRole::Owner;
  if (s == "admin")
    return GroupRole::Administrator;
  if (s == "member")
    return GroupRole::Member;
  lookup_malformed();
}
template <typename T, typename Decode>
auto lookup_field(const Json &object, const char *key, bool required,
                  LookupBudget &budget, std::size_t depth, Decode decode)
    -> LookupField<T> {
  const auto it = object.find(key);
  if (it == object.end()) {
    if (required)
      lookup_malformed();
    return LookupAbsent{};
  }
  budget.node(depth);
  if (it->is_null()) {
    if (required)
      lookup_malformed();
    return LookupNull{};
  }
  return decode(*it);
}
auto lookup_content(const Json &, LookupBudget &, std::size_t)
    -> DetachedContent;
template <typename T>
auto lookup_value(const Json &value, LookupBudget &budget, std::size_t depth)
    -> T {
  if constexpr (std::is_same_v<T, std::string>) {
    const auto &s = lookup_text(value);
    budget.text(s);
    return s;
  } else if constexpr (std::is_same_v<T, SensitiveText>) {
    const auto &s = lookup_text(value);
    budget.text(s);
    return SensitiveValueFactory::text(s);
  } else if constexpr (std::is_same_v<T, bool>) {
    return lookup_boolean(value);
  } else if constexpr (std::is_same_v<T, DetachedNodeUserId>) {
    if (value.is_string())
      return lookup_value<std::string>(value, budget, depth);
    return lookup_integer(value);
  } else if constexpr (std::is_same_v<T,
                                      std::shared_ptr<const DetachedContent>>) {
    return std::make_shared<const DetachedContent>(
        lookup_content(value, budget, depth));
  }
}
template <typename T>
auto lookup_segment_field(const Json &data, const char *key, bool required,
                          LookupBudget &budget, std::size_t depth)
    -> LookupField<T> {
  return lookup_field<T>(
      data, key, required, budget, depth,
      [&](const Json &v) { return lookup_value<T>(v, budget, depth); });
}
auto lookup_segment(const Json &segment, LookupBudget &budget,
                    std::size_t depth) -> DetachedSegment {
  budget.node(depth);
  if (!segment.is_object())
    lookup_malformed();
  const auto &type = lookup_text(segment.at("type"));
  const auto &data = segment.at("data");
  if (!data.is_null() && !data.is_object())
    lookup_malformed();
  budget.node(depth + 1);
  if (type == "text") {
    if (data.is_null())
      return DetachedText{std::nullopt};
    return DetachedText{DetachedTextFields{lookup_segment_field<std::string>(
        data, "text", true, budget, depth + 2)}};
  }
  if (type == "face") {
    if (data.is_null())
      return DetachedFace{std::nullopt};
    return DetachedFace{DetachedFaceFields{lookup_segment_field<std::string>(
        data, "id", true, budget, depth + 2)}};
  }
  if (type == "image") {
    if (data.is_null())
      return DetachedImage{std::nullopt};
    if (data.contains("type") && !data.at("type").is_null() &&
        lookup_text(data.at("type")) != "flash")
      lookup_malformed();
    return DetachedImage{DetachedImageFields{
        lookup_segment_field<SensitiveText>(data, "file", true, budget,
                                            depth + 2),
        lookup_segment_field<std::string>(data, "type", false, budget,
                                          depth + 2),
        lookup_segment_field<SensitiveText>(data, "url", false, budget,
                                            depth + 2),
        lookup_segment_field<bool>(data, "cache", false, budget, depth + 2),
        lookup_segment_field<bool>(data, "proxy", false, budget, depth + 2),
        lookup_segment_field<std::string>(data, "timeout", false, budget,
                                          depth + 2)}};
  }
  if (type == "record") {
    if (data.is_null())
      return DetachedRecord{std::nullopt};
    return DetachedRecord{DetachedRecordFields{
        lookup_segment_field<SensitiveText>(data, "file", true, budget,
                                            depth + 2),
        lookup_segment_field<bool>(data, "magic", false, budget, depth + 2),
        lookup_segment_field<SensitiveText>(data, "url", false, budget,
                                            depth + 2),
        lookup_segment_field<bool>(data, "cache", false, budget, depth + 2),
        lookup_segment_field<bool>(data, "proxy", false, budget, depth + 2),
        lookup_segment_field<std::string>(data, "timeout", false, budget,
                                          depth + 2)}};
  }
  if (type == "video") {
    if (data.is_null())
      return DetachedVideo{std::nullopt};
    return DetachedVideo{DetachedVideoFields{
        lookup_segment_field<SensitiveText>(data, "file", true, budget,
                                            depth + 2),
        lookup_segment_field<SensitiveText>(data, "url", false, budget,
                                            depth + 2),
        lookup_segment_field<bool>(data, "cache", false, budget, depth + 2),
        lookup_segment_field<bool>(data, "proxy", false, budget, depth + 2),
        lookup_segment_field<std::string>(data, "timeout", false, budget,
                                          depth + 2)}};
  }
  if (type == "at") {
    if (data.is_null())
      return DetachedAt{std::nullopt};
    return DetachedAt{DetachedAtFields{lookup_segment_field<std::string>(
        data, "qq", true, budget, depth + 2)}};
  }
  if (type == "rps") {
    if (data.is_null())
      return DetachedRps{std::nullopt};
    return DetachedRps{DetachedRpsFields{}};
  }
  if (type == "dice") {
    if (data.is_null())
      return DetachedDice{std::nullopt};
    return DetachedDice{DetachedDiceFields{}};
  }
  if (type == "shake") {
    if (data.is_null())
      return DetachedShake{std::nullopt};
    return DetachedShake{DetachedShakeFields{}};
  }
  if (type == "poke") {
    if (data.is_null())
      return DetachedPoke{std::nullopt};
    return DetachedPoke{DetachedPokeFields{
        lookup_segment_field<std::string>(data, "type", true, budget,
                                          depth + 2),
        lookup_segment_field<std::string>(data, "id", true, budget, depth + 2),
        lookup_segment_field<std::string>(data, "name", false, budget,
                                          depth + 2)}};
  }
  if (type == "anonymous") {
    if (data.is_null())
      return DetachedAnonymous{std::nullopt};
    return DetachedAnonymous{DetachedAnonymousFields{
        lookup_segment_field<bool>(data, "ignore", false, budget, depth + 2)}};
  }
  if (type == "share") {
    if (data.is_null())
      return DetachedShare{std::nullopt};
    return DetachedShare{DetachedShareFields{
        lookup_segment_field<SensitiveText>(data, "url", true, budget,
                                            depth + 2),
        lookup_segment_field<std::string>(data, "title", true, budget,
                                          depth + 2),
        lookup_segment_field<std::string>(data, "content", false, budget,
                                          depth + 2),
        lookup_segment_field<SensitiveText>(data, "image", false, budget,
                                            depth + 2)}};
  }
  if (type == "contact") {
    if (data.is_null())
      return DetachedContact{std::nullopt};
    const auto &kind = lookup_text(data.at("type"));
    if (kind != "qq" && kind != "group")
      lookup_malformed();
    return DetachedContact{DetachedContactFields{
        lookup_segment_field<std::string>(data, "type", true, budget,
                                          depth + 2),
        lookup_segment_field<std::string>(data, "id", true, budget,
                                          depth + 2)}};
  }
  if (type == "location") {
    if (data.is_null())
      return DetachedLocation{std::nullopt};
    return DetachedLocation{DetachedLocationFields{
        lookup_segment_field<std::string>(data, "lat", true, budget, depth + 2),
        lookup_segment_field<std::string>(data, "lon", true, budget, depth + 2),
        lookup_segment_field<std::string>(data, "title", false, budget,
                                          depth + 2),
        lookup_segment_field<std::string>(data, "content", false, budget,
                                          depth + 2)}};
  }
  if (type == "music") {
    if (data.is_null())
      return DetachedMusic{std::nullopt};
    const auto &kind = lookup_text(data.at("type"));
    if (kind != "qq" && kind != "163" && kind != "xm" && kind != "custom")
      lookup_malformed();
    if (kind == "custom") {
      (void)lookup_text(data.at("url"));
      (void)lookup_text(data.at("audio"));
      (void)lookup_text(data.at("title"));
    } else
      (void)lookup_text(data.at("id"));
    return DetachedMusic{DetachedMusicFields{
        lookup_segment_field<std::string>(data, "type", true, budget,
                                          depth + 2),
        lookup_segment_field<std::string>(data, "id", false, budget, depth + 2),
        lookup_segment_field<SensitiveText>(data, "url", false, budget,
                                            depth + 2),
        lookup_segment_field<SensitiveText>(data, "audio", false, budget,
                                            depth + 2),
        lookup_segment_field<std::string>(data, "title", false, budget,
                                          depth + 2),
        lookup_segment_field<std::string>(data, "content", false, budget,
                                          depth + 2),
        lookup_segment_field<SensitiveText>(data, "image", false, budget,
                                            depth + 2)}};
  }
  if (type == "reply") {
    if (data.is_null())
      return DetachedReply{std::nullopt};
    return DetachedReply{DetachedReplyFields{lookup_segment_field<std::string>(
        data, "id", true, budget, depth + 2)}};
  }
  if (type == "forward") {
    if (data.is_null())
      return DetachedForward{std::nullopt};
    return DetachedForward{
        DetachedForwardFields{lookup_segment_field<SensitiveText>(
            data, "id", true, budget, depth + 2)}};
  }
  if (type == "node") {
    if (data.is_null())
      return DetachedNode{std::nullopt};
    if (!data.contains("id") || data.at("id").is_null()) {
      if (!data.contains("user_id") || data.at("user_id").is_null() ||
          !data.contains("nickname") || data.at("nickname").is_null() ||
          !data.contains("content") || data.at("content").is_null())
        lookup_malformed();
    } else {
      (void)lookup_text(data.at("id"));
    }
    return DetachedNode{DetachedNodeFields{
        lookup_segment_field<std::string>(data, "id", false, budget, depth + 2),
        lookup_segment_field<DetachedNodeUserId>(data, "user_id", false, budget,
                                                 depth + 2),
        lookup_segment_field<std::string>(data, "nickname", false, budget,
                                          depth + 2),
        lookup_segment_field<std::shared_ptr<const DetachedContent>>(
            data, "content", false, budget, depth + 2)}};
  }
  if (type == "xml") {
    if (data.is_null())
      return DetachedXml{std::nullopt};
    return DetachedXml{DetachedXmlFields{lookup_segment_field<SensitiveText>(
        data, "data", true, budget, depth + 2)}};
  }
  if (type == "json") {
    if (data.is_null())
      return DetachedJson{std::nullopt};
    return DetachedJson{DetachedJsonFields{lookup_segment_field<SensitiveText>(
        data, "data", true, budget, depth + 2)}};
  }
  lookup_malformed();
}
// Decode entities once, after separating CQ syntax. Encoded CQ in plain text
// cannot become a segment; commas are entities only inside parameter values.
auto lookup_unescape(std::string_view input, bool parameter,
                     LookupBudget &budget) -> std::string {
  budget.text(input); // Upper bound checked before allocation.
  std::string out;
  out.reserve(input.size());
  for (std::size_t i = 0; i < input.size();) {
    const auto tail = input.substr(i);
    if (tail.starts_with("&amp;")) {
      out += '&';
      i += 5;
    } else if (tail.starts_with("&#91;")) {
      out += '[';
      i += 5;
    } else if (tail.starts_with("&#93;")) {
      out += ']';
      i += 5;
    } else if (parameter && tail.starts_with("&#44;")) {
      out += ',';
      i += 5;
    } else {
      out += input[i];
      ++i;
    }
  }
  return out;
}
auto lookup_cq_name(std::string_view name) -> bool {
  if (name.empty())
    return false;
  for (const auto ch : name)
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '_'))
      return false;
  return true;
}
auto lookup_content(const Json &value, LookupBudget &budget, std::size_t depth)
    -> DetachedContent {
  budget.node(depth);
  std::vector<DetachedSegment> segments;
  if (value.is_array()) {
    // No reserve from an untrusted size. Each element is charged before growth.
    for (const auto &segment : value)
      segments.push_back(lookup_segment(segment, budget, depth + 1));
    return LookupContentAccess::make(DetachedContentEncoding::SegmentArray,
                                     std::move(segments));
  }
  if (value.is_object()) {
    segments.push_back(lookup_segment(value, budget, depth + 1));
    return LookupContentAccess::make(
        DetachedContentEncoding::SingleSegmentObject, std::move(segments));
  }
  const std::string_view text = lookup_text(value);
  for (std::size_t cursor = 0; cursor < text.size();) {
    const auto start = text.find("[CQ:", cursor);
    if (start != cursor) {
      const auto end = start == std::string_view::npos ? text.size() : start;
      budget.node(depth + 1);
      budget.node(depth + 2);
      budget.node(depth + 3);
      auto plain =
          lookup_unescape(text.substr(cursor, end - cursor), false, budget);
      segments.emplace_back(DetachedText{DetachedTextFields{std::move(plain)}});
      cursor = end;
      if (cursor == text.size())
        break;
    }
    const auto end = text.find(']', cursor + 4);
    if (end == std::string_view::npos)
      lookup_malformed();
    auto body = text.substr(cursor + 4, end - cursor - 4);
    auto comma = body.find(',');
    const auto type = body.substr(0, comma);
    if (!lookup_cq_name(type))
      lookup_malformed();
    budget.node(depth + 1);
    budget.node(depth + 2);
    budget.text("type");
    budget.text("data");
    budget.text(type);
    Json data = Json::object();
    while (comma != std::string_view::npos) {
      body.remove_prefix(comma + 1);
      comma = body.find(',');
      const auto parameter = body.substr(0, comma);
      const auto equals = parameter.find('=');
      if (equals == std::string_view::npos)
        lookup_malformed();
      const auto key = parameter.substr(0, equals);
      if (!lookup_cq_name(key))
        lookup_malformed();
      budget.node(depth + 2);
      budget.text(key);
      if (data.contains(std::string{key}))
        lookup_malformed();
      data.emplace(std::string{key},
                   lookup_unescape(parameter.substr(equals + 1), true, budget));
    }
    segments.push_back(lookup_segment(
        Json{{"type", std::string{type}}, {"data", std::move(data)}}, budget,
        depth + 1));
    cursor = end + 1;
  }
  return LookupContentAccess::make(DetachedContentEncoding::CqString,
                                   std::move(segments));
}
auto lookup_sender(const Json &value, LookupBudget &budget) -> DetachedSender {
  if (!value.is_object())
    lookup_malformed();
  budget.node(1);
  const auto text = [&](const Json &v) {
    return lookup_value<std::string>(v, budget, 2);
  };
  return {
      lookup_field<std::int64_t>(value, "user_id", false, budget, 2,
                                 lookup_integer),
      lookup_field<std::string>(value, "nickname", false, budget, 2, text),
      lookup_field<std::string>(value, "card", false, budget, 2, text),
      lookup_field<Sex>(value, "sex", false, budget, 2, lookup_sex),
      lookup_field<std::int32_t>(value, "age", false, budget, 2, lookup_int32),
      lookup_field<std::string>(value, "area", false, budget, 2, text),
      lookup_field<std::string>(value, "level", false, budget, 2, text),
      lookup_field<GroupRole>(value, "role", false, budget, 2, lookup_role),
      lookup_field<std::string>(value, "title", false, budget, 2, text)};
}
void lookup_scope(const obcx::bot::BotInstallationRef &scope,
                  LookupBudget &budget) {
  budget.node(1);
  budget.node(2);
  budget.node(2);
  budget.text(scope.installation_id);
  budget.text(scope.surface.value());
  scope.validate();
}
void lookup_visit_content(const DetachedContent &, LookupBudget &, std::size_t);
template <typename T>
void lookup_visit_value(const T &value, LookupBudget &budget,
                        std::size_t depth) {
  if constexpr (std::is_same_v<T, std::string>)
    budget.text(value);
  else if constexpr (std::is_same_v<T, SensitiveText>)
    value.with_value(SensitiveUse::ExplicitAccess,
                     [&](std::string_view text) { budget.text(text); });
  else if constexpr (std::is_same_v<T,
                                    std::shared_ptr<const DetachedContent>>) {
    if (!value)
      lookup_malformed();
    lookup_visit_content(*value, budget, depth);
  } else if constexpr (std::is_same_v<T, DetachedNodeUserId>) {
    std::visit([&](const auto &v) { lookup_visit_value(v, budget, depth); },
               value);
  } else if constexpr (std::is_same_v<T, Sex>) {
    if (value != Sex::Male && value != Sex::Female && value != Sex::Unknown)
      lookup_malformed();
  } else if constexpr (std::is_same_v<T, GroupRole>) {
    if (value != GroupRole::Owner && value != GroupRole::Administrator &&
        value != GroupRole::Member)
      lookup_malformed();
  }
}
template <typename T>
void lookup_visit_field(const LookupField<T> &field, LookupBudget &budget,
                        std::size_t depth) {
  if (field.valueless_by_exception())
    lookup_malformed();
  if (std::holds_alternative<LookupAbsent>(field))
    return;
  budget.node(depth);
  if (const auto *value = std::get_if<T>(&field))
    lookup_visit_value(*value, budget, depth);
}
void lookup_visit_fields(const DetachedTextFields &fields, LookupBudget &budget,
                         std::size_t depth) {
  lookup_visit_field(fields.text, budget, depth);
}
void lookup_visit_fields(const DetachedFaceFields &fields, LookupBudget &budget,
                         std::size_t depth) {
  lookup_visit_field(fields.id, budget, depth);
}
void lookup_visit_fields(const DetachedImageFields &fields,
                         LookupBudget &budget, std::size_t depth) {
  lookup_visit_field(fields.file, budget, depth);
  lookup_visit_field(fields.type, budget, depth);
  lookup_visit_field(fields.url, budget, depth);
  lookup_visit_field(fields.cache, budget, depth);
  lookup_visit_field(fields.proxy, budget, depth);
  lookup_visit_field(fields.timeout, budget, depth);
}
void lookup_visit_fields(const DetachedRecordFields &fields,
                         LookupBudget &budget, std::size_t depth) {
  lookup_visit_field(fields.file, budget, depth);
  lookup_visit_field(fields.magic, budget, depth);
  lookup_visit_field(fields.url, budget, depth);
  lookup_visit_field(fields.cache, budget, depth);
  lookup_visit_field(fields.proxy, budget, depth);
  lookup_visit_field(fields.timeout, budget, depth);
}
void lookup_visit_fields(const DetachedVideoFields &fields,
                         LookupBudget &budget, std::size_t depth) {
  lookup_visit_field(fields.file, budget, depth);
  lookup_visit_field(fields.url, budget, depth);
  lookup_visit_field(fields.cache, budget, depth);
  lookup_visit_field(fields.proxy, budget, depth);
  lookup_visit_field(fields.timeout, budget, depth);
}
void lookup_visit_fields(const DetachedAtFields &fields, LookupBudget &budget,
                         std::size_t depth) {
  lookup_visit_field(fields.qq, budget, depth);
}
void lookup_visit_fields(const DetachedRpsFields &fields, LookupBudget &budget,
                         std::size_t depth) {
  (void)fields;
  (void)budget;
  (void)depth;
}
void lookup_visit_fields(const DetachedDiceFields &fields, LookupBudget &budget,
                         std::size_t depth) {
  (void)fields;
  (void)budget;
  (void)depth;
}
void lookup_visit_fields(const DetachedShakeFields &fields,
                         LookupBudget &budget, std::size_t depth) {
  (void)fields;
  (void)budget;
  (void)depth;
}
void lookup_visit_fields(const DetachedPokeFields &fields, LookupBudget &budget,
                         std::size_t depth) {
  lookup_visit_field(fields.type, budget, depth);
  lookup_visit_field(fields.id, budget, depth);
  lookup_visit_field(fields.name, budget, depth);
}
void lookup_visit_fields(const DetachedAnonymousFields &fields,
                         LookupBudget &budget, std::size_t depth) {
  lookup_visit_field(fields.ignore, budget, depth);
}
void lookup_visit_fields(const DetachedShareFields &fields,
                         LookupBudget &budget, std::size_t depth) {
  lookup_visit_field(fields.url, budget, depth);
  lookup_visit_field(fields.title, budget, depth);
  lookup_visit_field(fields.content, budget, depth);
  lookup_visit_field(fields.image, budget, depth);
}
void lookup_visit_fields(const DetachedContactFields &fields,
                         LookupBudget &budget, std::size_t depth) {
  lookup_visit_field(fields.type, budget, depth);
  lookup_visit_field(fields.id, budget, depth);
}
void lookup_visit_fields(const DetachedLocationFields &fields,
                         LookupBudget &budget, std::size_t depth) {
  lookup_visit_field(fields.lat, budget, depth);
  lookup_visit_field(fields.lon, budget, depth);
  lookup_visit_field(fields.title, budget, depth);
  lookup_visit_field(fields.content, budget, depth);
}
void lookup_visit_fields(const DetachedMusicFields &fields,
                         LookupBudget &budget, std::size_t depth) {
  lookup_visit_field(fields.type, budget, depth);
  lookup_visit_field(fields.id, budget, depth);
  lookup_visit_field(fields.url, budget, depth);
  lookup_visit_field(fields.audio, budget, depth);
  lookup_visit_field(fields.title, budget, depth);
  lookup_visit_field(fields.content, budget, depth);
  lookup_visit_field(fields.image, budget, depth);
}
void lookup_visit_fields(const DetachedReplyFields &fields,
                         LookupBudget &budget, std::size_t depth) {
  lookup_visit_field(fields.id, budget, depth);
}
void lookup_visit_fields(const DetachedForwardFields &fields,
                         LookupBudget &budget, std::size_t depth) {
  lookup_visit_field(fields.id, budget, depth);
}
void lookup_visit_fields(const DetachedNodeFields &fields, LookupBudget &budget,
                         std::size_t depth) {
  lookup_visit_field(fields.id, budget, depth);
  lookup_visit_field(fields.user_id, budget, depth);
  lookup_visit_field(fields.nickname, budget, depth);
  lookup_visit_field(fields.content, budget, depth);
}
void lookup_visit_fields(const DetachedXmlFields &fields, LookupBudget &budget,
                         std::size_t depth) {
  lookup_visit_field(fields.data, budget, depth);
}
void lookup_visit_fields(const DetachedJsonFields &fields, LookupBudget &budget,
                         std::size_t depth) {
  lookup_visit_field(fields.data, budget, depth);
}
void lookup_visit_content(const DetachedContent &content, LookupBudget &budget,
                          std::size_t depth) {
  budget.node(depth);
  switch (content.encoding()) {
  case DetachedContentEncoding::CqString:
  case DetachedContentEncoding::SegmentArray:
    break;
  case DetachedContentEncoding::SingleSegmentObject:
    if (content.segments().size() != 1)
      lookup_malformed();
    break;
  default:
    lookup_malformed();
  }
  for (const auto &segment : content.segments()) {
    budget.node(depth + 1);
    std::visit(
        [&](const auto &s) {
          budget.node(depth + 2);
          if (s.data)
            lookup_visit_fields(*s.data, budget, depth + 3);
        },
        segment);
  }
}
void lookup_visit_sender(const DetachedSender &sender, LookupBudget &budget) {
  budget.node(1);
  lookup_visit_field(sender.user_id, budget, 2);
  lookup_visit_field(sender.nickname, budget, 2);
  lookup_visit_field(sender.card, budget, 2);
  lookup_visit_field(sender.sex, budget, 2);
  lookup_visit_field(sender.age, budget, 2);
  lookup_visit_field(sender.area, budget, 2);
  lookup_visit_field(sender.level, budget, 2);
  lookup_visit_field(sender.role, budget, 2);
  lookup_visit_field(sender.title, budget, 2);
}
void lookup_forward_nodes(const DetachedContent &content) {
  if (content.encoding() != DetachedContentEncoding::SegmentArray)
    lookup_malformed();
  for (const auto &segment : content.segments()) {
    const auto *node = std::get_if<DetachedNode>(&segment);
    if (!node || !node->data ||
        !std::holds_alternative<DetachedNodeUserId>(node->data->user_id) ||
        !std::holds_alternative<std::string>(node->data->nickname) ||
        !std::holds_alternative<std::shared_ptr<const DetachedContent>>(
            node->data->content))
      lookup_malformed();
  }
}
} // namespace

auto decode_get_msg(const QueryJson &data,
                    const GetMessageParameters &parameters,
                    const QueryNativeCodec &codec, const ValueLimits &limits)
    -> DetachedMessage {
  codec.validate(parameters.message.installation());
  LookupBudget budget{limits};
  budget.inspect(
      data,
      0); // Entire borrowed tree, including unknown fields, before copies.
  if (!data.is_object())
    lookup_malformed();
  const auto time = lookup_int32(data.at("time"));
  const auto id = lookup_int32(data.at("message_id"));
  const auto real_id = lookup_int32(data.at("real_id"));
  if (id != QuerySelectors::export_message(parameters.message, limits))
    lookup_malformed();
  const auto &type = lookup_text(data.at("message_type"));
  if (type != "private" && type != "group")
    lookup_malformed();
  budget.node(0);
  for (int i = 0; i < 4; ++i)
    budget.node(1);
  lookup_scope(parameters.message.installation(), budget);
  DetachedMessage result{parameters.message.installation(),
                         time,
                         type == "private" ? MessageKind::Private
                                           : MessageKind::Group,
                         id,
                         real_id,
                         lookup_sender(data.at("sender"), budget),
                         lookup_content(data.at("message"), budget, 1)};
  validate_lookup_result(result, parameters, limits);
  return result;
}
auto decode_get_forward_msg(const QueryJson &data,
                            const GetForwardMessageParameters &parameters,
                            const QueryNativeCodec &codec,
                            const ValueLimits &limits)
    -> DetachedForwardMessage {
  codec.validate(parameters.forward.installation());
  LookupBudget budget{limits};
  budget.inspect(data, 0);
  if (!data.is_object() || !data.at("message").is_array())
    lookup_malformed();
  budget.node(0);
  lookup_scope(parameters.forward.installation(), budget);
  DetachedForwardMessage result{parameters.forward.installation(),
                                lookup_content(data.at("message"), budget, 1)};
  validate_lookup_result(result, parameters, limits);
  return result;
}
void validate_lookup_result(const DetachedMessage &value,
                            const GetMessageParameters &parameters,
                            const ValueLimits &limits) {
  LookupBudget budget{limits};
  budget.node(0);
  lookup_scope(value.installation, budget);
  for (int i = 0; i < 4; ++i)
    budget.node(1);
  if (value.installation != parameters.message.installation() ||
      value.message_id !=
          QuerySelectors::export_message(parameters.message, limits) ||
      (value.message_type != MessageKind::Private &&
       value.message_type != MessageKind::Group))
    lookup_malformed();
  lookup_visit_sender(value.sender, budget);
  lookup_visit_content(value.message, budget, 1);
}
void validate_lookup_result(const DetachedForwardMessage &value,
                            const GetForwardMessageParameters &parameters,
                            const ValueLimits &limits) {
  LookupBudget budget{limits};
  budget.node(0);
  lookup_scope(value.installation, budget);
  if (value.installation != parameters.forward.installation())
    lookup_malformed();
  lookup_visit_content(value.message, budget, 1);
  lookup_forward_nodes(value.message);
}
} // namespace detail
} // namespace obcx::onebot11::bot::standard
