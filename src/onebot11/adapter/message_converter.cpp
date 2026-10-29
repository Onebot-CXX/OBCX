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

auto MessageConverter::cq_unescape(std::string s) -> std::string {
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

  replace_all(s, "&#91;", "[");
  replace_all(s, "&#93;", "]");
  replace_all(s, "&#44;", ",");
  replace_all(s, "&amp;", "&");

  return s;
}

auto MessageConverter::from_v11_string(const std::string &raw_message)
    -> common::Message {
  common::Message message;
  if (raw_message.empty()) {
    return message;
  }

  // Recognize protocol syntax BEFORE decoding escapes. Escaped CQ-looking
  // text is literal text, not a mention/reply/image segment.

  static const re2::RE2 cq_regex(R"(\[CQ:([a-zA-Z0-9\-\._]+)(?:,([^\]]*))?\])");
  static const re2::RE2 param_regex(R"(([a-zA-Z0-9\-_]+)=([^,\]]*))");

  re2::StringPiece input(raw_message);
  re2::StringPiece remaining = input;
  std::string type_match;
  std::string params_match;

  size_t last_pos = 0;

  while (
      RE2::FindAndConsume(&remaining, cq_regex, &type_match, &params_match)) {
    // RE2::FindAndConsume only exposes captures; recover match_start by
    // subtracting the rebuilt match length ("[CQ:" + type + optional ","+params
    // + "]") from match_end.
    size_t match_end = raw_message.size() - remaining.size();
    size_t match_len = 4 + type_match.size() + 1;
    if (!params_match.empty()) {
      match_len += 1 + params_match.size();
    }
    size_t current_pos = match_end - match_len;

    if (current_pos > last_pos) {
      std::string text =
          cq_unescape(raw_message.substr(last_pos, current_pos - last_pos));
      if (!text.empty()) {
        message.push_back({.type = "text", .data = {{"text", text}}});
      }
    }

    nlohmann::json data;

    if (!params_match.empty()) {
      re2::StringPiece params_input(params_match);
      std::string key;
      std::string value;
      while (RE2::FindAndConsume(&params_input, param_regex, &key, &value)) {
        if (!key.empty()) {
          data[key] = cq_unescape(value);
        }
      }
    }

    if (!type_match.empty()) {
      message.push_back({.type = type_match, .data = data});
    }

    last_pos = match_end;
  }

  if (last_pos < raw_message.length()) {
    std::string remaining_text = cq_unescape(raw_message.substr(last_pos));
    if (!remaining_text.empty()) {
      message.push_back({.type = "text", .data = {{"text", remaining_text}}});
    }
  }

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