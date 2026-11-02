#pragma once

#include "core/bot/references.hpp"
#include "onebot11/bot/standard_common.hpp"
#include "onebot11/bot/standard_sensitive.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace obcx::onebot11::bot::standard {
namespace detail {
class MediaQuerySelectorAccess;
}

enum class RecordFormat : std::uint8_t {
  Mp3,
  Amr,
  Wma,
  M4a,
  Spx,
  Ogg,
  Wav,
  Flac
};

// Installation-only query intent, not received-media evidence or acquisition
// authority. Filename access is explicit and does not authorize disclosure.
class ImageFileSelector final {
public:
  ImageFileSelector(const ImageFileSelector &) = delete;
  auto operator=(const ImageFileSelector &) -> ImageFileSelector & = delete;
  ImageFileSelector(ImageFileSelector &&) noexcept = default;
  auto operator=(ImageFileSelector &&) noexcept
      -> ImageFileSelector & = default;
  auto installation() const noexcept -> const obcx::bot::BotInstallationRef &;
  void with_filename(SensitiveUse,
                     const std::function<void(std::string_view)> &) const;

private:
  ImageFileSelector(obcx::bot::BotInstallationRef, std::string);
  obcx::bot::BotInstallationRef installation_;
  std::string filename_;
  friend class MediaQuerySelectors;
  friend class detail::MediaQuerySelectorAccess;
};

class RecordFileSelector final {
public:
  RecordFileSelector(const RecordFileSelector &) = delete;
  auto operator=(const RecordFileSelector &) -> RecordFileSelector & = delete;
  RecordFileSelector(RecordFileSelector &&) noexcept = default;
  auto operator=(RecordFileSelector &&) noexcept
      -> RecordFileSelector & = default;
  auto installation() const noexcept -> const obcx::bot::BotInstallationRef &;
  void with_filename(SensitiveUse,
                     const std::function<void(std::string_view)> &) const;

private:
  RecordFileSelector(obcx::bot::BotInstallationRef, std::string);
  obcx::bot::BotInstallationRef installation_;
  std::string filename_;
  friend class MediaQuerySelectors;
  friend class detail::MediaQuerySelectorAccess;
};

class MediaQuerySelectors final {
public:
  static auto import_image(const obcx::bot::BotInstallationRef &,
                           std::string_view, const ValueLimits &)
      -> ImageFileSelector;
  static auto import_record(const obcx::bot::BotInstallationRef &,
                            std::string_view, const ValueLimits &)
      -> RecordFileSelector;
};

struct GetImageParameters {
  ImageFileSelector image;
};
struct GetRecordParameters {
  RecordFileSelector record;
  RecordFormat out_format;
};
// Provider-local locator only: no host mapping, open/fetch authority or
// observed MIME/size/availability guarantee follows from a successful query.
struct ProviderLocalFile {
  SensitiveText path;
};
struct ImageFileData {
  ProviderLocalFile file;
};
struct ConvertedRecordData {
  RecordFormat requested_format; // Request context, not verified byte metadata.
  ProviderLocalFile file;
};

} // namespace obcx::onebot11::bot::standard
