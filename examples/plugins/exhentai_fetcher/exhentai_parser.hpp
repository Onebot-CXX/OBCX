#pragma once

#include <string>
#include <vector>

namespace plugins {

struct GalleryEntry {
  std::string title;
  std::string url;           // https://exhentai.org/g/XXXXXXX/TOKEN/
  std::string thumbnail_url; // absolute CDN URL
  std::vector<std::string> tags;
};

class ExHentaiParser {
public:
  static std::vector<GalleryEntry> parse(const std::string &html);
};

} // namespace plugins
