#pragma once

#include "onebot11/bot/standard_content.hpp"
#include "standard_content_budget.hpp"
#include <span>
#include <type_traits>

namespace obcx::onebot11::bot::standard::detail {

using ContentField = std::pair<std::string_view, std::string_view>;
// All views are valid only during the callback. No callback
// ownership/allocation.
struct ContentLeafVisitor {
  void *context;
  void (*call)(void *, std::string_view, std::span<const ContentField>);
  void operator()(std::string_view type,
                  std::span<const ContentField> fields) const {
    call(context, type, fields);
  }
};
// Charges the complete borrowed leaf at depth (variants add no wrapper).
void inspect_leaf(ContentBudget &, const SendLeaf &, std::size_t depth);
// Requires complete enclosing borrowed preflight. Native decoding scratch is
// separate from payload accounting. Does not charge or retain field views.
void visit_leaf_fields(const SendLeaf &, const obcx::bot::BotInstallationRef &,
                       const obcx::bot::binding::NativeReferenceCodec &,
                       ContentLeafVisitor);
template <class Visit>
void visit_leaf(const SendLeaf &leaf,
                const obcx::bot::BotInstallationRef &scope,
                const obcx::bot::binding::NativeReferenceCodec &codec,
                Visit &&visit) {
  visit_leaf_fields(leaf, scope, codec,
                    {&visit, [](void *context, std::string_view type,
                                std::span<const ContentField> fields) {
                       (*static_cast<std::remove_reference_t<Visit> *>(
                           context))(type, fields);
                     }});
}
// Charges one generated JSON segment, including actual keys and string values.
void inspect_segment(ContentBudget &, std::string_view,
                     std::span<const ContentField>, std::size_t depth);
// These streaming helpers neither retain nor charge bytes: the caller's sink
// must reserve the entire generated string before a separate emission pass.
template <class Emit>
void escape_cq(std::string_view text, bool parameter, Emit &&emit) {
  for (const char &c : text) {
    switch (c) {
    case '&':
      emit("&amp;");
      break;
    case '[':
      emit("&#91;");
      break;
    case ']':
      emit("&#93;");
      break;
    case ',':
      if (parameter)
        emit("&#44;");
      else
        emit(",");
      break;
    default:
      emit(std::string_view(&c, 1));
      break;
    }
  }
}

template <class Emit>
void emit_cq(std::string_view type, std::span<const ContentField> fields,
             Emit &&emit) {
  if (type == "text") {
    escape_cq(fields[0].second, false, emit);
    return;
  }
  emit("[CQ:");
  emit(type);
  for (const auto &[key, value] : fields) {
    emit(",");
    emit(key);
    emit("=");
    escape_cq(value, true, emit);
  }
  emit("]");
}

struct EncodedContent {
  obcx::bot::Json message;
  bool auto_escape;
};

// Pure content encoding, not a native action or send envelope. One ValueLimits
// work budget covers borrowed installation/content (including opaque refs) and
// retained EncodedContent, counting logical containers/values and raw strings
// and generated JSON keys. The text cap applies to each whole string, including
// the entire generated CQ message. No hypothetical JSON framing is charged.
// All borrowed fields are inspected before native decoding or payload copies.
// The caller must separately bound exact wire envelopes, serialization/codec
// scratch and preparation buffers. No I/O or provider-work guarantee.
auto encode_content(const SendContent &content,
                    const obcx::bot::BotInstallationRef &installation,
                    const obcx::bot::binding::NativeReferenceCodec &codec,
                    const ValueLimits &limits) -> EncodedContent;

} // namespace obcx::onebot11::bot::standard::detail
