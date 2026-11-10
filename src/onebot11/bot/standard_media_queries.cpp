#include "standard_media_query_codec.hpp"

#include "standard_service_codec.hpp"

#include <stdexcept>
#include <utility>

namespace obcx::onebot11::bot::standard {
namespace detail {
// Encoding borrows directly: no callback allocation, export copy or new ledger.
class MediaQuerySelectorAccess final {
public:
  static auto filename(const ImageFileSelector &value) noexcept
      -> const std::string & {
    return value.filename_;
  }
  static auto filename(const RecordFileSelector &value) noexcept
      -> const std::string & {
    return value.filename_;
  }
};
} // namespace detail
namespace {
namespace bot_model = obcx::bot;
using Json = detail::QueryJson;

[[noreturn]] void malformed() {
  throw std::invalid_argument("invalid standard media query value");
}
[[noreturn]] void exceeded() {
  throw std::invalid_argument("standard media query value exceeds bound");
}
void installation(const bot_model::BotInstallationRef &scope) {
  scope.validate();
  if (scope.surface.value() != "onebot11.qq")
    malformed();
}
void require_access(SensitiveUse use) {
  if (use != SensitiveUse::ExplicitAccess)
    throw std::invalid_argument("explicit sensitive access required");
}

// One ledger for complete borrowed input and actual retained/generated values.
// DTO names, allocator storage and framing are not payload bytes. Reference
// validation scratch remains the enclosing CallLimits integration's concern.
class MediaQueryBudget {
public:
  explicit MediaQueryBudget(const ValueLimits &limits) : limits_(limits) {
    limits.validate();
  }
  void node(std::size_t depth) {
    if (depth > limits_.maximum_depth || depth > 32 ||
        nodes_ == limits_.maximum_nodes ||
        nodes_ == bot_model::detail::bot_maximum_items)
      exceeded();
    ++nodes_;
  }
  void text(std::string_view value) {
    if (value.size() > limits_.maximum_text_bytes ||
        value.size() > limits_.maximum_bytes - bytes_ ||
        value.size() > bot_model::detail::bot_maximum_bytes - bytes_)
      exceeded();
    bytes_ += value.size();
    bot_model::detail::validate_utf8(value);
  }
  void scope(const bot_model::BotInstallationRef &value, std::size_t depth) {
    node(depth);
    node(depth + 1);
    text(value.installation_id);
    node(depth + 1);
    text(value.surface.value());
  }
  void selector(const bot_model::BotInstallationRef &value,
                std::string_view filename, std::size_t depth) {
    node(depth);
    scope(value, depth + 1);
    node(depth + 1);
    text(filename);
  }
  void inspect(const Json &value, std::size_t depth = 0) {
    node(depth);
    if (value.is_string()) {
      text(value.get_ref<const std::string &>());
    } else if (value.is_structured()) {
      for (auto it = value.begin(); it != value.end(); ++it) {
        if (value.is_object())
          text(it.key());
        inspect(it.value(), depth + 1);
      }
    } else if (value.is_binary() || value.is_discarded()) {
      malformed();
    }
  }

private:
  const ValueLimits &limits_;
  std::size_t bytes_{};
  std::size_t nodes_{};
};

auto record_format(RecordFormat value) -> std::string_view {
  switch (value) {
  case RecordFormat::Mp3:
    return "mp3";
  case RecordFormat::Amr:
    return "amr";
  case RecordFormat::Wma:
    return "wma";
  case RecordFormat::M4a:
    return "m4a";
  case RecordFormat::Spx:
    return "spx";
  case RecordFormat::Ogg:
    return "ogg";
  case RecordFormat::Wav:
    return "wav";
  case RecordFormat::Flac:
    return "flac";
  }
  malformed();
}
auto file(const Json &data) -> const std::string & {
  if (!data.is_object())
    malformed();
  const auto it = data.find("file");
  if (it == data.end() || !it->is_string())
    malformed();
  return it->get_ref<const std::string &>();
}
void project_file(std::string_view path, MediaQueryBudget &budget) {
  budget.node(1); // ProviderLocalFile
  budget.node(2); // SensitiveText's logical text, not its private storage.
  budget.text(path);
}
} // namespace

ImageFileSelector::ImageFileSelector(bot_model::BotInstallationRef scope,
                                     std::string filename)
    : installation_(std::move(scope)), filename_(std::move(filename)) {}
auto ImageFileSelector::installation() const noexcept
    -> const bot_model::BotInstallationRef & {
  return installation_;
}
void ImageFileSelector::with_filename(
    SensitiveUse use,
    const std::function<void(std::string_view)> &visitor) const {
  require_access(use);
  visitor(filename_);
}
auto MediaQuerySelectors::import_image(
    const bot_model::BotInstallationRef &scope, std::string_view filename,
    const ValueLimits &limits) -> ImageFileSelector {
  MediaQueryBudget budget{limits};
  budget.selector(scope, filename, 0); // borrowed
  budget.selector(scope, filename, 0); // retained
  installation(scope);
  return ImageFileSelector{scope, std::string{filename}};
}

RecordFileSelector::RecordFileSelector(bot_model::BotInstallationRef scope,
                                       std::string filename)
    : installation_(std::move(scope)), filename_(std::move(filename)) {}
auto RecordFileSelector::installation() const noexcept
    -> const bot_model::BotInstallationRef & {
  return installation_;
}
void RecordFileSelector::with_filename(
    SensitiveUse use,
    const std::function<void(std::string_view)> &visitor) const {
  require_access(use);
  visitor(filename_);
}
auto MediaQuerySelectors::import_record(
    const bot_model::BotInstallationRef &scope, std::string_view filename,
    const ValueLimits &limits) -> RecordFileSelector {
  MediaQueryBudget budget{limits};
  budget.selector(scope, filename, 0);
  budget.selector(scope, filename, 0);
  installation(scope);
  return RecordFileSelector{scope, std::string{filename}};
}

namespace detail {
auto encode_get_image(const GetImageParameters &p,
                      const bot_model::BotInstallationRef &expected,
                      const QueryNativeCodec &codec, const ValueLimits &limits)
    -> QueryJson {
  MediaQueryBudget budget{limits};
  const auto &filename = MediaQuerySelectorAccess::filename(p.image);
  budget.node(0);
  budget.selector(p.image.installation(), filename, 1);
  budget.scope(expected, 0);
  budget.node(0); // generated parameters
  budget.text("file");
  budget.node(1);
  budget.text(filename);
  installation(expected);
  codec.validate(expected);
  if (p.image.installation() != expected)
    malformed();
  auto result = QueryJson::object();
  result.emplace("file", filename);
  return result;
}
auto decode_get_image_data(const QueryJson &data, const ValueLimits &limits)
    -> ImageFileData {
  MediaQueryBudget budget{limits};
  budget.inspect(data);
  const auto &path = file(data);
  budget.node(0);
  project_file(path, budget);
  return {{SensitiveValueFactory::text(path)}};
}
auto encode_get_record(const GetRecordParameters &p,
                       const bot_model::BotInstallationRef &expected,
                       const QueryNativeCodec &codec, const ValueLimits &limits)
    -> QueryJson {
  MediaQueryBudget budget{limits};
  const auto &filename = MediaQuerySelectorAccess::filename(p.record);
  budget.node(0);
  budget.selector(p.record.installation(), filename, 1);
  budget.node(1); // out_format enum
  budget.scope(expected, 0);
  const auto format = record_format(p.out_format);
  budget.node(0);
  budget.text("file");
  budget.node(1);
  budget.text(filename);
  budget.text("out_format");
  budget.node(1);
  budget.text(format);
  installation(expected);
  codec.validate(expected);
  if (p.record.installation() != expected)
    malformed();
  auto result = QueryJson::object();
  result.emplace("file", filename);
  result.emplace("out_format", format);
  return result;
}
auto decode_get_record_data(const QueryJson &data, RecordFormat requested,
                            const ValueLimits &limits) -> ConvertedRecordData {
  MediaQueryBudget budget{limits};
  budget.inspect(data);
  budget.node(0); // borrowed requested format
  (void)record_format(requested);
  const auto &path = file(data);
  budget.node(0);
  budget.node(1); // retained request context
  project_file(path, budget);
  return {requested, {SensitiveValueFactory::text(path)}};
}
} // namespace detail
} // namespace obcx::onebot11::bot::standard
