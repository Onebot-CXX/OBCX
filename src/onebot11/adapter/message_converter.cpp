#include "onebot11/adapter/message_converter.hpp"

#include <algorithm>
#include <re2/re2.h>
#include <sstream>

namespace obcx::adapter::onebot11 {

/*
 * Go-CQHTTP CQ-code escape rules: `&` -> `&amp;`, `[` -> `&#91;`,
 * `]` -> `&#93;`, `,` -> `&#44;`. Order matters: escape `&` first so the
 * entity sequences emitted for the others are not re-escaped.
 */
auto MessageConverter::cq_escape(std::string s) -> std::string {
  if (s.empty()) {
    return s;
  }

  auto replace_all = [](std::string &str, const std::string &from,
                        const std::string &to) -> void {
    if (from.empty()) {
      return;
    }
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
      str.replace(start_pos, from.length(), to);
      start_pos += to.length();
    }
  };

  replace_all(s, "&", "&amp;");
  replace_all(s, "[", "&#91;");
  replace_all(s, "]", "&#93;");
  replace_all(s, ",", "&#44;");

  return s;
}

void MessageConverter::visit_cq_unescaped(
    std::string_view raw, const std::function<void(std::string_view)> &visit) {
  // One pass has the same precedence as the historical ordered replacements:
  // an &amp; result is never reinterpreted as another entity.
  while (!raw.empty()) {
    auto amp = raw.find('&');
    if (amp != 0) {
      visit(raw.substr(0, amp));
      if (amp == std::string_view::npos)
        break;
      raw.remove_prefix(amp);
    }
    bool decoded = false;
    for (const auto &[encoded, plain] :
         {std::pair<std::string_view, std::string_view>{"&#91;", "["},
          {"&#93;", "]"},
          {"&#44;", ","},
          {"&amp;", "&"}}) {
      if (raw.starts_with(encoded)) {
        visit(plain);
        raw.remove_prefix(encoded.size());
        decoded = true;
        break;
      }
    }
    if (!decoded) {
      visit(raw.substr(0, 1));
      raw.remove_prefix(1);
    }
  }
}

auto MessageConverter::cq_unescape(std::string s) -> std::string {
  std::string result;
  visit_cq_unescaped(s, [&](auto part) { result.append(part); });
  return result;
}

void MessageConverter::visit_v11_parameters(
    std::string_view raw,
    const std::function<void(std::string_view, std::string_view)> &visit) {
  static const re2::RE2 pattern(R"(([a-zA-Z0-9\-_]+)=([^,\]]*))");
  re2::StringPiece remaining(raw), key, value;
  while (RE2::FindAndConsume(&remaining, pattern, &key, &value))
    visit(std::string_view(key.data(), key.size()),
          std::string_view(value.data(), value.size()));
}

void MessageConverter::visit_v11_string(
    std::string_view raw, const std::function<void(CqSegmentView)> &visit) {
  static const re2::RE2 pattern(R"(\[CQ:([a-zA-Z0-9\-\._]+)(?:,([^\]]*))?\])");
  re2::StringPiece remaining(raw), type, params;
  std::size_t last = 0;
  while (RE2::FindAndConsume(&remaining, pattern, &type, &params)) {
    const auto end = raw.size() - remaining.size();
    // Preserve the predecessor's empty-parameter match boundary as well.
    const auto length =
        5 + type.size() + (params.empty() ? 0 : 1 + params.size());
    const auto start = end - length;
    if (start > last)
      visit({"text", {}, raw.substr(last, start - last)});
    visit({std::string_view(type.data(), type.size()),
           std::string_view(params.data(), params.size()), std::nullopt});
    last = end;
  }
  if (last < raw.size())
    visit({"text", {}, raw.substr(last)});
}

auto MessageConverter::from_v11_string(const std::string &raw_message)
    -> common::Message {
  common::Message message;
  visit_v11_string(raw_message, [&](CqSegmentView view) {
    nlohmann::json data;
    if (view.literal_text)
      data = {{"text", cq_unescape(std::string(*view.literal_text))}};
    else
      visit_v11_parameters(view.parameters, [&](auto key, auto value) {
        data[std::string(key)] = cq_unescape(std::string(value));
      });
    message.push_back(
        {.type = std::string(view.type), .data = std::move(data)});
  });
  return message;
}

auto MessageConverter::to_v11_string(const common::Message &message)
    -> std::string {
  if (message.empty()) {
    return "";
  }

  std::stringstream ss;

  for (const auto &segment : message) {
    if (segment.type.empty()) {
      continue;
    }

    if (segment.type == "text") {
      std::string text = segment.data.value("text", "");
      ss << cq_escape(text);
    } else if (segment.type == "image") {
      ss << "[CQ:image";

      if (segment.data.is_object() && !segment.data.empty()) {
        std::vector<std::string> keys;
        for (auto const &[key, val] : segment.data.items()) {
          keys.push_back(key);
        }
        std::ranges::sort(keys);

        for (const auto &key : keys) {
          const auto &val = segment.data[key];
          ss << "," << key << "=";

          if (val.is_string()) {
            ss << cq_escape(val.get<std::string>());
          } else if (val.is_number_integer()) {
            ss << val.get<int64_t>();
          } else if (val.is_number_float()) {
            ss << val.get<double>();
          } else if (val.is_boolean()) {
            ss << (val.get<bool>() ? "true" : "false");
          } else if (val.is_null()) {
          } else {
            ss << cq_escape(val.dump());
          }
        }
      }
      ss << "]";
    } else {
      ss << "[CQ:" << segment.type;

      if (segment.data.is_object() && !segment.data.empty()) {
        std::vector<std::string> keys;
        for (auto const &[key, val] : segment.data.items()) {
          keys.push_back(key);
        }
        std::ranges::sort(keys);

        for (const auto &key : keys) {
          const auto &val = segment.data[key];
          ss << "," << key << "=";

          if (val.is_string()) {
            ss << cq_escape(val.get<std::string>());
          } else if (val.is_number_integer()) {
            ss << val.get<int64_t>();
          } else if (val.is_number_float()) {
            ss << val.get<double>();
          } else if (val.is_boolean()) {
            ss << (val.get<bool>() ? "true" : "false");
          } else if (val.is_null()) {
          } else {
            ss << cq_escape(val.dump());
          }
        }
      }
      ss << "]";
    }
  }

  return ss.str();
}

} // namespace obcx::adapter::onebot11