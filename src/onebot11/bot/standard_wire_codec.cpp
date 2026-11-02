#include "standard_wire_codec.hpp"

#include <cmath>
#include <cstddef>
#include <deque>
#include <set>
#include <stdexcept>
#include <string>

namespace obcx::onebot11::bot::standard::detail {
namespace {
using Json = obcx::bot::Json;

class ReplyPreflight final : public nlohmann::json_sax<Json> {
public:
  explicit ReplyPreflight(const ValueLimits &limits) : limits_(limits) {}

  auto null() -> bool override { return value(false); }
  auto boolean(bool) -> bool override { return value(false); }
  auto number_integer(number_integer_t) -> bool override {
    return value(false);
  }
  auto number_unsigned(number_unsigned_t) -> bool override {
    return value(false);
  }
  auto number_float(number_float_t number, const string_t &) -> bool override {
    if (!std::isfinite(number))
      return false;
    return value(false);
  }
  auto string(string_t &text) -> bool override {
    value(false);
    input_text(text);
    return true;
  }
  auto binary(binary_t &) -> bool override { return false; }

  auto start_object(std::size_t) -> bool override {
    value(true);
    frames_.emplace_back();
    return true;
  }
  auto key(string_t &text) -> bool override {
    input_text(text);
    auto &keys = frames_.back();
    if (keys.contains(text))
      throw std::invalid_argument("standard reply contains duplicate keys");
    // Charge the actual decoded key copy before insertion. Object teardown
    // releases scratch, but never refunds this cumulative logical cost.
    charge(text.size(), bytes_, limits_.maximum_bytes);
    keys.insert(text);
    return true;
  }
  auto end_object() -> bool override {
    frames_.pop_back();
    return true;
  }
  auto start_array(std::size_t) -> bool override {
    value(false);
    frames_.emplace_back();
    return true;
  }
  auto end_array() -> bool override {
    frames_.pop_back();
    return true;
  }
  auto parse_error(std::size_t, const std::string &, const Json::exception &)
      -> bool override {
    return false;
  }

  void reserve_retained_tree() {
    // No DOM exists yet. Reserve its full cost against the SAME ledger that
    // already contains preflight input and duplicate-detection key copies.
    charge(input_nodes_, nodes_, limits_.maximum_nodes);
    charge(input_bytes_, bytes_, limits_.maximum_bytes);
  }

private:
  static void charge(std::size_t count, std::size_t &used,
                     std::size_t maximum) {
    if (count > maximum - used)
      throw std::invalid_argument("standard reply exceeds value limits");
    used += count;
  }

  auto value(bool object) -> bool {
    if (input_nodes_ == 0 && !object)
      throw std::invalid_argument("standard reply requires an object root");
    // Open containers equal the next value's root-zero depth; keys do not
    // enter this path. Validated maximum_depth <= 32 also bounds stack growth.
    if (frames_.size() > limits_.maximum_depth)
      throw std::invalid_argument("standard reply exceeds depth limit");
    charge(1, nodes_, limits_.maximum_nodes);
    charge(1, input_nodes_, limits_.maximum_nodes);
    return true;
  }
  void input_text(std::string_view text) {
    if (text.size() > limits_.maximum_text_bytes)
      throw std::invalid_argument("standard reply exceeds text limit");
    // The strict JSON lexer validates UTF-8 and decodes escapes before these
    // callbacks. Empty strings/keys remain legal with a zero text allowance.
    charge(text.size(), bytes_, limits_.maximum_bytes);
    charge(text.size(), input_bytes_, limits_.maximum_bytes);
  }

  const ValueLimits &limits_;
  std::size_t nodes_{};
  std::size_t bytes_{};
  std::size_t input_nodes_{};
  std::size_t input_bytes_{};
  // Deque growth does not copy existing key sets. Arrays have empty sets;
  // neither stack/set bookkeeping nor lexer buffers are logical JSON nodes.
  std::deque<std::set<Json::string_t>> frames_;
};
} // namespace

auto parse_standard_reply(std::string_view wire, const CallLimits &calls,
                          const ValueLimits &values) -> obcx::bot::Json {
  calls.validate();
  values.validate();
  if (wire.size() > calls.maximum_response_bytes)
    throw std::invalid_argument("standard reply exceeds response byte limit");
  // The pinned lexer treats a raw NUL as EOF even in strict mode. JSON never
  // permits raw NUL; reject it so an embedded terminator cannot hide a suffix.
  if (wire.find('\0') != std::string_view::npos)
    throw std::invalid_argument("standard reply contains invalid JSON");
  try {
    ReplyPreflight preflight(values);
    if (!Json::sax_parse(wire.begin(), wire.end(), &preflight,
                         Json::input_format_t::json, true, false))
      throw std::invalid_argument("standard reply contains invalid JSON");
    preflight.reserve_retained_tree();
    return Json::parse(wire.begin(), wire.end(), nullptr, true, false);
  } catch (const Json::exception &) {
    // Never expose parser diagnostics, tokens, keys or body snippets. Ordinary
    // allocation failure may propagate; no submission evidence is fabricated.
    throw std::invalid_argument("standard reply contains invalid JSON");
  }
}

} // namespace obcx::onebot11::bot::standard::detail
