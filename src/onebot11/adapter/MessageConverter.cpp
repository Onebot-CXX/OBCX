#include "onebot11/adapter/MessageConverter.hpp"
#include <algorithm>
#include <regex>
#include <sstream>

namespace obcx::adapter::onebot11 {

/*
 * \if CHINESE
 * CQ 码中的特殊字符需要转义和反转义
 * Go-CQHTTP 的规则: `&` -> `&amp;`, `[` -> `&#91;`, `]` -> `&#93;`, `,` ->
 * `&#44;` 注意：转义顺序很重要，必须先转义 & 字符，再转义其他字符，避免重复转义
 * \endif
 * \if ENGLISH
 * Special characters in CQ codes need to be escaped and unescaped
 * Go-CQHTTP rules: `&` -> `&amp;`, `[` -> `&#91;`, `]` -> `&#93;`, `,` ->
 * `&#44;` Note: Escape order is important, must escape & first to avoid double
 * escaping
 * \endif
 */
auto MessageConverter::cq_escape(std::string s) -> std::string {
  if (s.empty()) {
    return s;
  }

  auto replace_all = [](std::string &str, const std::string &from,
                        const std::string &to) {
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
                        const std::string &to) {
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

/*
 * \if CHINESE
 * 正则表达式，用于匹配 [CQ:type,key=value,...]
 * 优化后的正则表达式，支持更多边缘情况
 * \endif
 * \if ENGLISH
 * Regular expression for matching [CQ:type,key=value,...]
 * Optimized regex with better edge case support
 * \endif
 */
auto MessageConverter::from_v11_string(const std::string &raw_message)
    -> common::Message {
  common::Message message;
  if (raw_message.empty()) {
    return message;
  }

  // 首先对整个字符串进行CQ码反转义处理
  // 这样可以将 &#91;CQ:image&#93; 转换为 [CQ:image] 以便正则匹配
  std::string unescaped_message = cq_unescape(raw_message);

  // 改进的正则表达式，更准确地匹配 CQ 码
  // 支持类型名中的点号、数字、下划线等字符
  const static std::regex cq_regex(
      R"(\[CQ:([a-zA-Z0-9\-\._]+)(?:,([^\]]*))?\])");

  auto it = std::sregex_iterator(unescaped_message.begin(),
                                 unescaped_message.end(), cq_regex);
  auto end = std::sregex_iterator();

  size_t last_pos = 0;

  for (; it != end; ++it) {
    const std::smatch &match = *it;
    size_t current_pos = match.position();

    if (current_pos > last_pos) {
      std::string text =
          unescaped_message.substr(last_pos, current_pos - last_pos);
      if (!text.empty()) {
        message.push_back({"text", {{"text", text}}});
      }
    }

    std::string type = match[1].str();
    nlohmann::json data;

    /*
     * \if CHINESE
     * 匹配CQ码内部的 key=value 对
     * 改进的正则表达式，支持更复杂的参数值
     * \endif
     * \if ENGLISH
     * Match key=value pairs inside CQ code
     * Improved regex with support for more complex parameter values
     * \endif
     */
    if (match[2].matched && !match[2].str().empty()) {
      std::string params_str = match[2].str();

      // 改进的参数解析正则表达式
      const static std::regex param_regex(R"(([a-zA-Z0-9\-_]+)=([^,\]]*))");
      auto pit = std::sregex_iterator(params_str.begin(), params_str.end(),
                                      param_regex);
      auto pend = std::sregex_iterator();

      for (; pit != pend; ++pit) {
        const std::smatch &param_match = *pit;
        std::string key = param_match[1].str();
        std::string value = param_match[2].str();

        if (!key.empty()) {
          data[key] = value; // 不需要再次反转义，因为整个字符串已经处理过了
        }
      }
    }

    if (!type.empty()) {
      message.push_back({type, data});
    }

    last_pos = current_pos + match.length();
  }

  /*
   * \if CHINESE
   * 3. 添加最后一个CQ码之后的剩余纯文本
   * \endif
   * \if ENGLISH
   * 3. Add remaining plain text after last CQ code
   * \endif
   */
  if (last_pos < raw_message.length()) {
    std::string remaining_text = raw_message.substr(last_pos);
    if (!remaining_text.empty()) {
      message.push_back({"text", {{"text", cq_unescape(remaining_text)}}});
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
      // Special handling for image segments
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