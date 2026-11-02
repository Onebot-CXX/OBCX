#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace obcx::onebot11::bot::standard {

namespace detail {
class SensitiveValueFactory;
}

// Intentional-use acknowledgement, not an ACL or an authorization capability.
enum class SensitiveUse : std::uint8_t { ExplicitAccess };

// No implicit conversion, formatter or generic serialization is provided.
// Callback arguments are borrowed only for the duration of the callback.
// Callers remain responsible for any copies or disclosures they make.
class SensitiveText {
public:
  SensitiveText(SensitiveText &&) noexcept;
  auto operator=(SensitiveText &&) noexcept -> SensitiveText &;
  ~SensitiveText();
  SensitiveText(const SensitiveText &) = delete;
  auto operator=(const SensitiveText &) -> SensitiveText & = delete;

  void with_value(SensitiveUse use,
                  const std::function<void(std::string_view)> &visitor) const;

private:
  friend class detail::SensitiveValueFactory;
  struct Storage;
  explicit SensitiveText(std::unique_ptr<Storage> storage);
  std::unique_ptr<Storage> storage_;
};

class SensitiveInt32 {
public:
  SensitiveInt32(SensitiveInt32 &&) noexcept;
  auto operator=(SensitiveInt32 &&) noexcept -> SensitiveInt32 &;
  ~SensitiveInt32();
  SensitiveInt32(const SensitiveInt32 &) = delete;
  auto operator=(const SensitiveInt32 &) -> SensitiveInt32 & = delete;

  void with_value(SensitiveUse use,
                  const std::function<void(std::int32_t)> &visitor) const;

private:
  friend class detail::SensitiveValueFactory;
  struct Storage;
  explicit SensitiveInt32(std::unique_ptr<Storage> storage);
  std::unique_ptr<Storage> storage_;
};

enum class ServiceValueKind : std::uint8_t {
  Null,
  Boolean,
  SignedInteger,
  UnsignedInteger,
  Floating,
  Text,
  Array,
  Object
};

// A non-owning, callback-scoped view. Neither this view nor any borrowed text
// may outlive the callback. Only ServiceExtraFields can create the root view.
class ServiceValueView {
public:
  ServiceValueView(const ServiceValueView &) = delete;
  auto operator=(const ServiceValueView &) -> ServiceValueView & = delete;
  auto kind() const -> ServiceValueKind;
  auto boolean() const -> bool;
  auto signed_integer() const -> std::int64_t;
  auto unsigned_integer() const -> std::uint64_t;
  auto floating() const -> double;
  void with_text(const std::function<void(std::string_view)> &visitor) const;
  void for_each_element(
      const std::function<void(const ServiceValueView &)> &visitor) const;
  void for_each_member(
      const std::function<void(std::string_view, const ServiceValueView &)>
          &visitor) const;

private:
  friend class ServiceExtraFields;
  explicit ServiceValueView(const void *value);
  const void *value_;
};

// Unknown names and values are both restricted. There is no JSON/dump access.
class ServiceExtraFields {
public:
  ServiceExtraFields(ServiceExtraFields &&) noexcept;
  auto operator=(ServiceExtraFields &&) noexcept -> ServiceExtraFields &;
  ~ServiceExtraFields();
  ServiceExtraFields(const ServiceExtraFields &) = delete;
  auto operator=(const ServiceExtraFields &) -> ServiceExtraFields & = delete;

  void with_value(
      SensitiveUse use,
      const std::function<void(const ServiceValueView &)> &visitor) const;

private:
  friend class detail::SensitiveValueFactory;
  struct Storage;
  explicit ServiceExtraFields(std::unique_ptr<Storage> storage);
  std::unique_ptr<Storage> storage_;
};

} // namespace obcx::onebot11::bot::standard
