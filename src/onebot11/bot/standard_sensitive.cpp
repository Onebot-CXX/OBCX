#include "onebot11/bot/standard_sensitive.hpp"

#include "standard_service_codec.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace obcx::onebot11::bot::standard {
namespace {
using Json = common::json;

// Best-effort erasure of this owner's storage, not a promise to erase caller,
// parser, allocator, transport or compiler-created copies.
void erase_bytes(void *data, std::size_t size) noexcept {
  auto *bytes = static_cast<volatile unsigned char *>(data);
  while (size-- != 0)
    *bytes++ = 0;
}
void erase_tree(Json &value) noexcept {
  if (value.is_object()) {
    auto &object = value.get_ref<Json::object_t &>();
    while (!object.empty()) {
      auto member = object.extract(object.begin());
      erase_bytes(member.key().data(), member.key().size());
      erase_tree(member.mapped());
    }
  } else if (value.is_array()) {
    for (auto &element : value)
      erase_tree(element);
  } else if (value.is_string()) {
    auto &text = value.get_ref<std::string &>();
    erase_bytes(text.data(), text.size());
  } else if (value.is_number_unsigned()) {
    auto &number = value.get_ref<Json::number_unsigned_t &>();
    erase_bytes(&number, sizeof(number));
  } else if (value.is_number_integer()) {
    auto &number = value.get_ref<Json::number_integer_t &>();
    erase_bytes(&number, sizeof(number));
  } else if (value.is_number_float()) {
    auto &number = value.get_ref<Json::number_float_t &>();
    erase_bytes(&number, sizeof(number));
  } else if (value.is_boolean()) {
    auto &boolean = value.get_ref<Json::boolean_t &>();
    erase_bytes(&boolean, sizeof(boolean));
  }
}
void require_access(SensitiveUse use, bool present) {
  if (use != SensitiveUse::ExplicitAccess)
    throw std::invalid_argument("explicit sensitive access required");
  if (!present)
    throw std::logic_error("sensitive value was moved");
}
void require_kind(ServiceValueKind actual, ServiceValueKind expected) {
  if (actual != expected)
    throw std::invalid_argument(
        "restricted service value has a different kind");
}
} // namespace

struct SensitiveText::Storage {
  explicit Storage(std::string_view value) : text(value.begin(), value.end()) {}
  ~Storage() { erase_bytes(text.data(), text.size()); }
  std::vector<char> text;
};
SensitiveText::SensitiveText(std::unique_ptr<Storage> storage)
    : storage_(std::move(storage)) {}
SensitiveText::SensitiveText(SensitiveText &&) noexcept = default;
auto SensitiveText::operator=(SensitiveText &&) noexcept
    -> SensitiveText & = default;
SensitiveText::~SensitiveText() = default;
void SensitiveText::with_value(
    SensitiveUse use,
    const std::function<void(std::string_view)> &visitor) const {
  require_access(use, static_cast<bool>(storage_));
  const auto &text = storage_->text;
  visitor(text.empty() ? std::string_view{}
                       : std::string_view{text.data(), text.size()});
}

struct SensitiveInt32::Storage {
  explicit Storage(std::int32_t value) : number(value) {}
  ~Storage() { erase_bytes(&number, sizeof(number)); }
  std::int32_t number;
};
SensitiveInt32::SensitiveInt32(std::unique_ptr<Storage> storage)
    : storage_(std::move(storage)) {}
SensitiveInt32::SensitiveInt32(SensitiveInt32 &&) noexcept = default;
auto SensitiveInt32::operator=(SensitiveInt32 &&) noexcept
    -> SensitiveInt32 & = default;
SensitiveInt32::~SensitiveInt32() = default;
void SensitiveInt32::with_value(
    SensitiveUse use, const std::function<void(std::int32_t)> &visitor) const {
  require_access(use, static_cast<bool>(storage_));
  visitor(storage_->number);
}

struct ServiceExtraFields::Storage {
  Json value = Json::object();
  ~Storage() { erase_tree(value); }
};
ServiceExtraFields::ServiceExtraFields(std::unique_ptr<Storage> storage)
    : storage_(std::move(storage)) {}
ServiceExtraFields::ServiceExtraFields(ServiceExtraFields &&) noexcept =
    default;
auto ServiceExtraFields::operator=(ServiceExtraFields &&) noexcept
    -> ServiceExtraFields & = default;
ServiceExtraFields::~ServiceExtraFields() = default;
void ServiceExtraFields::with_value(
    SensitiveUse use,
    const std::function<void(const ServiceValueView &)> &visitor) const {
  require_access(use, static_cast<bool>(storage_));
  visitor(ServiceValueView{&storage_->value});
}

ServiceValueView::ServiceValueView(const void *value) : value_(value) {}
auto ServiceValueView::kind() const -> ServiceValueKind {
  const auto &value = *static_cast<const Json *>(value_);
  if (value.is_null())
    return ServiceValueKind::Null;
  if (value.is_boolean())
    return ServiceValueKind::Boolean;
  if (value.is_number_unsigned())
    return ServiceValueKind::UnsignedInteger;
  if (value.is_number_integer())
    return ServiceValueKind::SignedInteger;
  if (value.is_number_float())
    return ServiceValueKind::Floating;
  if (value.is_string())
    return ServiceValueKind::Text;
  if (value.is_array())
    return ServiceValueKind::Array;
  if (value.is_object())
    return ServiceValueKind::Object;
  throw std::logic_error("restricted service value has unsupported kind");
}
auto ServiceValueView::boolean() const -> bool {
  require_kind(kind(), ServiceValueKind::Boolean);
  return static_cast<const Json *>(value_)->get<bool>();
}
auto ServiceValueView::signed_integer() const -> std::int64_t {
  require_kind(kind(), ServiceValueKind::SignedInteger);
  return static_cast<const Json *>(value_)->get<std::int64_t>();
}
auto ServiceValueView::unsigned_integer() const -> std::uint64_t {
  require_kind(kind(), ServiceValueKind::UnsignedInteger);
  return static_cast<const Json *>(value_)->get<std::uint64_t>();
}
auto ServiceValueView::floating() const -> double {
  require_kind(kind(), ServiceValueKind::Floating);
  return static_cast<const Json *>(value_)->get<double>();
}
void ServiceValueView::with_text(
    const std::function<void(std::string_view)> &visitor) const {
  require_kind(kind(), ServiceValueKind::Text);
  visitor(static_cast<const Json *>(value_)->get_ref<const std::string &>());
}
void ServiceValueView::for_each_element(
    const std::function<void(const ServiceValueView &)> &visitor) const {
  require_kind(kind(), ServiceValueKind::Array);
  for (const auto &value : *static_cast<const Json *>(value_))
    visitor(ServiceValueView{&value});
}
void ServiceValueView::for_each_member(
    const std::function<void(std::string_view, const ServiceValueView &)>
        &visitor) const {
  require_kind(kind(), ServiceValueKind::Object);
  const auto &object = *static_cast<const Json *>(value_);
  for (auto it = object.begin(); it != object.end(); ++it)
    visitor(it.key(), ServiceValueView{&it.value()});
}

auto detail::SensitiveValueFactory::text(std::string_view value)
    -> SensitiveText {
  return SensitiveText{std::make_unique<SensitiveText::Storage>(value)};
}
auto detail::SensitiveValueFactory::int32(std::int32_t value)
    -> SensitiveInt32 {
  return SensitiveInt32{std::make_unique<SensitiveInt32::Storage>(value)};
}
auto detail::SensitiveValueFactory::extras(
    const Json &object, std::initializer_list<std::string_view> known)
    -> ServiceExtraFields {
  auto storage = std::make_unique<ServiceExtraFields::Storage>();
  for (auto it = object.begin(); it != object.end(); ++it) {
    if (std::find(known.begin(), known.end(), it.key()) == known.end())
      storage->value[it.key()] = it.value();
  }
  return ServiceExtraFields{std::move(storage)};
}

} // namespace obcx::onebot11::bot::standard
