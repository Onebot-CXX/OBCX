#include "standard_ws_wire.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <stdexcept>

namespace obcx::network::detail {
namespace {
constexpr std::string_view prefix = "obcx.standard:";

class EchoScan {
public:
  explicit EchoScan(std::string_view input) : input_(input) {}

  auto scan() -> StandardWsEchoCandidate {
    if (input_.starts_with("\xef\xbb\xbf"))
      position_ = 3;
    space();
    if (!take('{'))
      return result();
    space();
    if (!take('}')) {
      for (;;) {
        std::size_t key_length = 0;
        bool echo_key = true;
        if (!text([&](unsigned value) {
              constexpr std::string_view key = "echo";
              if (key_length >= key.size() || value != key[key_length])
                echo_key = false;
              if (key_length < key.size() + 1)
                ++key_length;
            }))
          return result();
        echo_key = echo_key && key_length == 4;
        space();
        if (!take(':'))
          return result();
        space();
        if (echo_key) {
          if (echo_found_)
            usable_ = false;
          echo_found_ = true;
          if (peek() == '"') {
            if (!echo_text())
              return result();
          } else {
            usable_ = false;
            if (!skip_value())
              return result();
          }
        } else if (!skip_value()) {
          return result();
        }
        space();
        if (take('}'))
          break;
        if (!take(','))
          return result();
        space();
      }
    }
    space();
    complete_ = position_ == input_.size();
    return result();
  }

private:
  auto result() const -> StandardWsEchoCandidate {
    return {seen_, complete_ && usable_ && seen_ ? echo_ : std::nullopt};
  }
  auto peek() const -> char {
    return position_ < input_.size() ? input_[position_] : '\0';
  }
  auto take(char value) -> bool {
    if (position_ == input_.size() || input_[position_] != value)
      return false;
    ++position_;
    return true;
  }
  void space() {
    while (position_ < input_.size()) {
      const char c = input_[position_];
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
        break;
      ++position_;
    }
  }
  static auto hex(char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  }
  template <typename Consume> auto text(Consume consume) -> bool {
    if (!take('"'))
      return false;
    while (position_ < input_.size()) {
      unsigned value = static_cast<unsigned char>(input_[position_++]);
      if (value == '"')
        return true;
      if (value < 0x20)
        return false; // Raw NUL is not an end-of-input sentinel.
      if (value == '\\') {
        if (position_ == input_.size())
          return false;
        value = static_cast<unsigned char>(input_[position_++]);
        switch (value) {
        case '"':
        case '\\':
        case '/':
          break;
        case 'b':
          value = '\b';
          break;
        case 'f':
          value = '\f';
          break;
        case 'n':
          value = '\n';
          break;
        case 'r':
          value = '\r';
          break;
        case 't':
          value = '\t';
          break;
        case 'u':
          value = 0;
          for (unsigned i = 0; i != 4; ++i) {
            if (position_ == input_.size())
              return false;
            const int digit = hex(input_[position_++]);
            if (digit < 0)
              return false;
            value = value * 16 + static_cast<unsigned>(digit);
          }
          break;
        default:
          return false;
        }
      }
      consume(value);
    }
    return false;
  }
  auto echo_text() -> bool {
    std::size_t matched = 0;
    std::size_t digits = 0;
    std::uint64_t number = 0;
    bool spelling = true;
    bool leading_zero = false;
    const bool closed = text([&](unsigned value) {
      if (matched < prefix.size()) {
        if (value != static_cast<unsigned char>(prefix[matched]))
          spelling = false;
        ++matched;
        if (matched == prefix.size() && spelling)
          seen_ = true; // Sticky even for truncated/invalid suffixes.
        return;
      }
      if (value < '0' || value > '9' || (digits && leading_zero)) {
        spelling = false;
        return;
      }
      const auto digit = value - '0';
      if (!digits)
        leading_zero = digit == 0;
      if (number > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
        spelling = false;
      if (spelling)
        number = number * 10 + digit;
      if (digits < 21)
        ++digits;
    });
    if (closed && spelling && matched == prefix.size() && digits)
      echo_ = number;
    else
      usable_ = false;
    return closed;
  }
  auto skip_value() -> bool {
    if (peek() == '"')
      return text([](unsigned) {});
    if (peek() != '{' && peek() != '[') {
      const auto start = position_;
      while (position_ < input_.size()) {
        const char c = peek();
        if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\r' ||
            c == '\n' || c == '\t')
          break;
        if (c == '\0' || c == '"' || c == '{' || c == '[')
          return false;
        ++position_;
      }
      return position_ != start;
    }
    // Existing logical JSON depth ceiling, not a frame/memory policy. Beyond
    // it no candidate is usable, but keep scanning for reserved root traffic.
    std::array<char, 32> stack{};
    std::size_t depth = 0;
    do {
      const char c = peek();
      if (c == '"') {
        if (!text([](unsigned) {}))
          return false;
        continue;
      }
      if (position_ == input_.size() || c == '\0')
        return false;
      ++position_;
      if (c == '{' || c == '[') {
        if (depth == std::numeric_limits<std::size_t>::max())
          return false;
        if (depth < stack.size())
          stack[depth] = c;
        else
          usable_ = false;
        ++depth;
      } else if (c == '}' || c == ']') {
        --depth;
        if (depth < stack.size() && stack[depth] != (c == '}' ? '{' : '['))
          return false;
      }
    } while (depth);
    return true;
  }
  std::string_view input_;
  std::size_t position_{};
  bool seen_{};
  bool echo_found_{};
  bool usable_{true};
  bool complete_{};
  std::optional<std::uint64_t> echo_;
};
} // namespace

auto locate_standard_ws_echo(std::string_view frame)
    -> StandardWsEchoCandidate {
  return EchoScan{frame}.scan();
}

auto make_standard_ws_envelope(
    obcx::onebot11::bot::standard::detail::NativeAction action,
    obcx::onebot11::bot::standard::ExecutionMode mode,
    std::string_view parameters, std::uint64_t echo) -> std::string {
  const auto name =
      obcx::onebot11::bot::standard::detail::native_action_name(action, mode);
  std::array<char, 20> decimal{};
  const auto converted =
      std::to_chars(decimal.data(), decimal.data() + decimal.size(), echo);
  const std::string_view digits{decimal.data(), converted.ptr};
  constexpr std::string_view begin = "{\"action\":\"";
  constexpr std::string_view params = "\",\"params\":";
  constexpr std::string_view echo_key = ",\"echo\":\"";
  constexpr std::string_view end = "\"}";
  std::string result;
  std::size_t size = 0;
  for (const auto part : {begin, std::string_view{name}, params, parameters,
                          echo_key, prefix, digits, end}) {
    if (part.size() > result.max_size() - size)
      throw std::length_error("standard WS envelope exceeds string capacity");
    size += part.size();
  }
  result.reserve(size);
  for (const auto part : {begin, std::string_view{name}, params, parameters,
                          echo_key, prefix, digits, end})
    result.append(part);
  return result;
}
} // namespace obcx::network::detail
