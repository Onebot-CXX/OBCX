#include "standard_composition_codec.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <memory>
#include <type_traits>

namespace obcx::onebot11::bot::standard::detail {
namespace {
using Json = obcx::bot::Json;
using Codec = obcx::bot::binding::NativeReferenceCodec;
using Field = ContentField;

// All arithmetic is checked before it can affect allocation or indexing.
auto add_size(std::size_t a, std::size_t b) -> std::size_t {
  if (b > std::numeric_limits<std::size_t>::max() - a)
    invalid_content();
  return a + b;
}
auto multiply_size(std::size_t a, std::size_t b) -> std::size_t {
  if (a && b > std::numeric_limits<std::size_t>::max() / a)
    invalid_content();
  return a * b;
}

// A parameter escape replaces each &, [, ], or comma with five bytes, with
// exactly one escapable ampersand remaining. This composes nested CQ lengths
// without materializing strings or rescanning descendants during planning.
struct CqMeasure {
  std::size_t bytes{};
  std::size_t escapes{};
  void append(CqMeasure other) {
    bytes = add_size(bytes, other.bytes);
    escapes = add_size(escapes, other.escapes);
  }
  void append(std::string_view text) {
    bytes = add_size(bytes, text.size());
    for (char c : text)
      if (c == '&' || c == '[' || c == ']' || c == ',')
        escapes = add_size(escapes, 1);
  }
  auto parameter() const -> CqMeasure {
    return {add_size(bytes, multiply_size(escapes, 4)), escapes};
  }
};
struct SequencePlan {
  bool has_parent{};
  std::size_t depth{};
  CqMeasure cq;
};

void inspect_message(ContentBudget &budget, const obcx::bot::MessageRef &ref,
                     std::size_t depth) {
  budget.node(depth);
  inspect_reference(budget, ref.conversation, depth + 1);
  budget.string_value(ref.id.serialized(), depth + 1);
}
void inspect_composition(ContentBudget &budget, const MessageInput &input) {
  budget.node(0);
  if (const auto *literal = std::get_if<LiteralText>(&input)) {
    budget.string_value(literal->text, 1);
    return;
  }
  const auto &composition = std::get<Composition>(input);
  budget.node(1); // sequences
  for (const auto &sequence : composition.sequences) {
    budget.node(2); // Sequence
    budget.node(3); // encoding
    budget.node(3); // segments
    for (const auto &segment : sequence.segments)
      std::visit(
          [&](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, SendLeaf>) {
              inspect_leaf(budget, value, 4); // no phantom variant wrapper
            } else {
              budget.node(4);
              if constexpr (std::is_same_v<T, Reply>) {
                inspect_message(budget, value.target, 5);
              } else if constexpr (std::is_same_v<T, ScopedReferenceNode>) {
                inspect_message(budget, value.message, 5);
                budget.node(5); // work
              } else if constexpr (std::is_same_v<T, IdSelectedNode>) {
                budget.node(5); // MessageSelector
                inspect_installation(budget, value.message.installation(), 6);
                budget.node(6); // private signed message ID
                budget.node(5); // work
              } else if constexpr (std::is_same_v<T, AuthoredNode>) {
                inspect_reference(budget, value.author, 5);
                budget.string_value(value.nickname, 5);
                budget.node(5); // SequenceIndex
                budget.node(6); // value
              }
            }
          },
          segment);
  }
}
void delegated(DelegatedWork work) {
  // This pure slice has no remote expansion evidence, including for scoped
  // refs.
  if (work != DelegatedWork::AcceptUnmeasured)
    invalid_content();
}
auto message_id(const obcx::bot::MessageRef &message,
                const obcx::bot::ConversationRef &destination,
                const Codec &codec, bool reply) -> std::string {
  codec.validate(message);
  if (message.conversation.installation != destination.installation ||
      (reply && message.conversation != destination))
    invalid_content();
  auto parts = codec.unpack(message.id);
  if (parts.kind != "message" || parts.components.size() != 1)
    invalid_content();
  // Scoped references retain the existing native codec's canonical signed
  // identity range. Only the distinct MessageSelector is signed-int32.
  return std::move(parts.components[0]);
}
auto author_id(const obcx::bot::UserRef &author,
               const obcx::bot::ConversationRef &destination,
               const Codec &codec) -> std::string {
  codec.validate(author);
  if (author.installation != destination.installation)
    invalid_content();
  auto parts = codec.unpack(author.id);
  if (parts.kind != "user" || parts.components.size() != 1)
    invalid_content();
  return std::move(parts.components[0]);
}

// Requires the COMPLETE borrowed graph preflight. Views and native ID scratch
// survive only through the callback; no retained payload or budget reset here.
template <class Visit>
void visit_segment(const Segment &segment,
                   const obcx::bot::ConversationRef &destination,
                   const Codec &codec, const ValueLimits &limits,
                   Visit &&visit) {
  std::visit(
      [&](const auto &value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, SendLeaf>) {
          visit_leaf(value, destination.installation, codec,
                     [&](std::string_view type, std::span<const Field> fields) {
                       visit(type, fields,
                             static_cast<const SequenceIndex *>(nullptr));
                     });
        } else {
          std::string native;
          std::array<char, 12> decimal;
          std::array<Field, 2> fields;
          std::string_view type = "node";
          std::size_t count = 1;
          const SequenceIndex *child = nullptr;
          if constexpr (std::is_same_v<T, Reply>) {
            type = "reply";
            native = message_id(value.target, destination, codec, true);
            fields[0] = {"id", native};
          } else if constexpr (std::is_same_v<T, ScopedReferenceNode>) {
            delegated(value.work);
            native = message_id(value.message, destination, codec, false);
            fields[0] = {"id", native};
          } else if constexpr (std::is_same_v<T, IdSelectedNode>) {
            delegated(value.work);
            if (value.message.installation() != destination.installation)
              invalid_content();
            // Public scalar export performs local canonical validation only.
            // Its borrowed selector and generated spelling are charged by OUR
            // ledger.
            const auto id =
                QuerySelectors::export_message(value.message, limits);
            const auto [end, error] = std::to_chars(
                decimal.data(), decimal.data() + decimal.size(), id);
            if (error != std::errc{})
              invalid_content();
            fields[0] = {"id", std::string_view(decimal.data(), end)};
          } else if constexpr (std::is_same_v<T, AuthoredNode>) {
            native = author_id(value.author, destination, codec);
            fields[0] = {"user_id", native};
            fields[1] = {"nickname", value.nickname};
            count = 2;
            child = &value.content;
          }
          visit(type, std::span<const Field>(fields.data(), count), child);
        }
      },
      segment);
}

// Common authored-node framing used by the counting and emitting passes.
template <class Emit, class Child>
void emit_authored_cq(std::span<const Field> fields, Emit &&emit,
                      Child &&child) {
  emit("[CQ:node");
  for (const auto &[key, value] : fields) {
    emit(",");
    emit(key);
    emit("=");
    escape_cq(value, true, emit);
  }
  emit(",content=");
  child();
  emit("]");
}

template <bool Materialize>
auto prepare_composition(const MessageInput &input,
                         const obcx::bot::ConversationRef &destination,
                         const Codec &codec, ContentBudget &budget,
                         std::size_t message_depth)
    -> std::conditional_t<Materialize, EncodedContent, bool> {
  budget.limits().validate();
  inspect_reference(budget, destination, 0);
  inspect_composition(budget, input);
  // Nothing input-sized was allocated and no opaque reference was decoded
  // above.
  codec.validate(destination);
  if (destination.installation.surface.value() != "onebot11.qq")
    invalid_content();
  budget.node(0); // EncodedContent
  budget.node(1); // auto_escape
  if (const auto *literal = std::get_if<LiteralText>(&input)) {
    budget.string_value(literal->text, message_depth);
    if constexpr (Materialize)
      return {literal->text, true};
    else
      return true;
  }
  const auto &sequences = std::get<Composition>(input).sequences;
  const auto count = sequences.size();
  if (!count)
    invalid_content();
  (void)composition_scratch_bytes(count);
  auto plan = std::make_unique<SequencePlan[]>(count);
  plan[0].depth = message_depth; // Actual native message placement
  // Forward-only indices permit linear tree validation and depth propagation.
  for (std::size_t i = 0; i < count; ++i) {
    const auto &sequence = sequences[i];
    if (i && !plan[i].has_parent)
      invalid_content();
    const bool cq = sequence.encoding == ContentEncoding::CqString;
    const bool single =
        sequence.encoding == ContentEncoding::SingleSegmentObject;
    if ((!cq && !single &&
         sequence.encoding != ContentEncoding::SegmentArray) ||
        (single && sequence.segments.size() != 1))
      invalid_content();
    for (const auto &segment : sequence.segments)
      if (const auto *node = std::get_if<AuthoredNode>(&segment)) {
        const auto child = node->content.value;
        if (child <= i || child >= count || plan[child].has_parent)
          invalid_content();
        if (cq && sequences[child].encoding != ContentEncoding::CqString)
          invalid_content();
        plan[child].has_parent = true;
        // A CQ child's retained intermediate is a standalone string; a JSON
        // child moves into the actual parent's data.content without a copy.
        plan[child].depth = cq ? 0 : add_size(plan[i].depth, single ? 2 : 3);
      }
  }
  // Reject every invalid scope/delegation/leaf before generated reservation.
  for (const auto &sequence : sequences)
    for (const auto &segment : sequence.segments)
      visit_segment(segment, destination, codec, budget.limits(),
                    [](std::string_view, std::span<const Field>,
                       const SequenceIndex *) {});

  // Reverse order supplies child measurements without recursion or rescans.
  for (std::size_t i = count; i-- > 0;) {
    const auto &sequence = sequences[i];
    const bool cq = sequence.encoding == ContentEncoding::CqString;
    const bool single =
        sequence.encoding == ContentEncoding::SingleSegmentObject;
    const auto depth = plan[i].depth;
    if (!single)
      budget.node(depth); // message array or retained CQ string
    for (const auto &segment : sequence.segments)
      visit_segment(
          segment, destination, codec, budget.limits(),
          [&](std::string_view type, std::span<const Field> fields,
              const SequenceIndex *child) {
            if (cq) {
              auto emit = [&](std::string_view text) {
                plan[i].cq.append(text);
              };
              if (child)
                emit_authored_cq(fields, emit, [&] {
                  plan[i].cq.append(plan[child->value].cq.parameter());
                });
              else
                emit_cq(type, fields, emit);
            } else {
              const auto segment_depth = single ? depth : add_size(depth, 1);
              inspect_segment(budget, type, fields, segment_depth);
              if (child)
                budget.string(
                    "content"); // child value charged at its actual depth
            }
          });
    if (cq) {
      std::size_t length = 0;
      budget.string_part(length, plan[i].cq.bytes);
    }
  }

  if constexpr (!Materialize) {
    return false;
  } else {
    // Fixed-size slots are private scratch, not fictitious null payload nodes.
    // All payload strings/trees (including CQ intermediates) are now reserved.
    auto output = std::make_unique<Json[]>(count);
    for (std::size_t i = count; i-- > 0;) {
      const auto &sequence = sequences[i];
      const bool cq = sequence.encoding == ContentEncoding::CqString;
      const bool single =
          sequence.encoding == ContentEncoding::SingleSegmentObject;
      std::string text;
      if (cq)
        text.reserve(plan[i].cq.bytes);
      else if (!single) {
        output[i] = Json::array();
        output[i].get_ref<Json::array_t &>().reserve(sequence.segments.size());
      }
      for (const auto &segment : sequence.segments)
        visit_segment(
            segment, destination, codec, budget.limits(),
            [&](std::string_view type, std::span<const Field> fields,
                const SequenceIndex *child) {
              if (cq) {
                auto emit = [&](std::string_view part) { text.append(part); };
                if (child) {
                  emit_authored_cq(fields, emit, [&] {
                    escape_cq(
                        output[child->value].get_ref<const std::string &>(),
                        true, emit);
                  });
                  output[child->value] = nullptr;
                } else
                  emit_cq(type, fields, emit);
              } else {
                Json value = Json::object();
                value["type"] = std::string(type);
                value["data"] = Json::object();
                auto &data = value["data"];
                for (const auto &[key, field] : fields)
                  data[std::string(key)] = std::string(field);
                if (child)
                  data["content"] = std::move(output[child->value]);
                if (single)
                  output[i] = std::move(value);
                else
                  output[i].push_back(std::move(value));
              }
            });
      if (cq)
        output[i] = std::move(text);
    }
    return {std::move(output[0]), false};
  }
}
} // namespace

auto composition_scratch_bytes(std::size_t sequence_count) -> std::size_t {
  const auto element = add_size(sizeof(SequencePlan), sizeof(Json));
  const auto controls = add_size(sizeof(std::unique_ptr<SequencePlan[]>),
                                 sizeof(std::unique_ptr<Json[]>));
  return add_size(controls, multiply_size(sequence_count, element));
}
auto validate_composition(const MessageInput &input,
                          const obcx::bot::ConversationRef &destination,
                          const Codec &codec, ContentBudget &budget,
                          std::size_t message_depth) -> bool {
  try {
    return prepare_composition<false>(input, destination, codec, budget,
                                      message_depth);
  } catch (const std::exception &) {
    invalid_content();
  }
}
auto encode_composition(const MessageInput &input,
                        const obcx::bot::ConversationRef &destination,
                        const Codec &codec, ContentBudget &budget)
    -> EncodedContent {
  return encode_composition(input, destination, codec, budget, 1);
}
auto encode_composition(const MessageInput &input,
                        const obcx::bot::ConversationRef &destination,
                        const Codec &codec, ContentBudget &budget,
                        std::size_t message_depth) -> EncodedContent {
  try {
    return prepare_composition<true>(input, destination, codec, budget,
                                     message_depth);
  } catch (const std::exception &) {
    // No native IDs, selector values, paths or caller content in diagnostics.
    invalid_content();
  }
}
auto encode_composition(const MessageInput &input,
                        const obcx::bot::ConversationRef &destination,
                        const Codec &codec, const ValueLimits &limits)
    -> EncodedContent {
  ContentBudget budget(limits);
  return encode_composition(input, destination, codec, budget);
}
} // namespace obcx::onebot11::bot::standard::detail
