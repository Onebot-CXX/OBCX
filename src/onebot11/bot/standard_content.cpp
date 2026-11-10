#include "standard_content_codec.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace obcx::onebot11::bot::standard::detail {
using Json = obcx::bot::Json;
using Field = ContentField;

static void inspect_send_media(ContentBudget &budget,
                               const SendMediaSource &source,
                               std::size_t depth) {
  budget.node(depth);
  std::visit(
      [&](const auto &v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, ProviderFileName>) {
          inspect_installation(budget, v.installation, depth + 1);
          budget.string_value(v.name, depth + 1);
          budget.node(depth + 1);
        } else if constexpr (std::is_same_v<T, CallerFileUri>) {
          budget.string_value(v.uri, depth + 1);
        } else if constexpr (std::is_same_v<T, InlineBase64>) {
          budget.string_value(v.data, depth + 1);
          // Prefix construction in the field visitor is bounded before
          // allocation.
          std::size_t generated = 0;
          budget.string_part(generated, std::string_view("base64://").size());
          budget.string_part(generated, v.data.size());
        } else {
          budget.string_value(v.url, depth + 1);
          budget.node(depth + 1);
          budget.node(depth + 1);
          budget.node(depth + 1);
          if (v.timeout_seconds)
            budget.node(depth + 1);
        }
      },
      source);
}

static void validate_send_base64(std::string_view text) {
  if (text.empty() || text.size() % 4)
    invalid_content();
  const auto first_pad = text.find('=');
  const auto end =
      first_pad == std::string_view::npos ? text.size() : first_pad;
  if (text.size() - end > 2)
    invalid_content();
  for (std::size_t i = 0; i < end; ++i) {
    const auto c = text[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '+' || c == '/'))
      invalid_content();
  }
  for (std::size_t i = end; i < text.size(); ++i)
    if (text[i] != '=')
      invalid_content();
}

// Typed input has logical containers/field values, not hypothetical JSON keys.
// Variants select a value; absent optionals add no value. Present optionals and
// enums/bools are nodes, without sizeof(scalar) byte charges.
void inspect_leaf(ContentBudget &budget, const SendLeaf &leaf,
                  std::size_t depth) {
  budget.node(depth);
  std::visit(
      [&](const auto &v) {
        using T = std::decay_t<decltype(v)>;
        auto text = [&](std::string_view value) {
          budget.string_value(value, depth + 1);
        };
        auto optional = [&](const std::optional<std::string> &value) {
          if (value)
            text(*value);
        };
        if constexpr (std::is_same_v<T, Image> || std::is_same_v<T, Record> ||
                      std::is_same_v<T, Video>) {
          inspect_send_media(budget, v.source, depth + 1);
          if constexpr (!std::is_same_v<T, Video>)
            budget.node(depth + 1);
        } else if constexpr (std::is_same_v<T, Text>) {
          text(v.text);
        } else if constexpr (std::is_same_v<T, Face>) {
          text(v.id);
        } else if constexpr (std::is_same_v<T, AtUser> ||
                             std::is_same_v<T, UserContact>) {
          inspect_reference(budget, v.user, depth + 1);
        } else if constexpr (std::is_same_v<T, GroupContact>) {
          inspect_reference(budget, v.group, depth + 1);
        } else if constexpr (std::is_same_v<T, Poke>) {
          text(v.type);
          text(v.id);
        } else if constexpr (std::is_same_v<T, Anonymous>) {
          if (v.ignore)
            budget.node(depth + 1);
        } else if constexpr (std::is_same_v<T, Share>) {
          text(v.url);
          text(v.title);
          optional(v.content);
          optional(v.image_url);
        } else if constexpr (std::is_same_v<T, Location>) {
          budget.node(depth + 1);
          budget.string_value(v.latitude.decimal, depth + 2);
          budget.node(depth + 1);
          budget.string_value(v.longitude.decimal, depth + 2);
          optional(v.title);
          optional(v.content);
        } else if constexpr (std::is_same_v<T, Music>) {
          budget.node(depth + 1);
          text(v.id);
        } else if constexpr (std::is_same_v<T, CustomMusic>) {
          text(v.url);
          text(v.audio_url);
          text(v.title);
          optional(v.content);
          optional(v.image_url);
        } else if constexpr (std::is_same_v<T, XmlContent> ||
                             std::is_same_v<T, JsonContent>) {
          text(v.document);
        }
      },
      leaf);
}

static void inspect_input(ContentBudget &budget, const SendContent &content) {
  budget.node(0);
  if (const auto *literal = std::get_if<LiteralText>(&content)) {
    budget.string_value(literal->text, 1);
  } else {
    const auto &composed = std::get<ComposedContent>(content);
    budget.node(1); // encoding
    budget.node(1); // segments
    for (const auto &leaf : composed.segments)
      inspect_leaf(budget, leaf, 2);
  }
}

static void decimal(std::string_view value) {
  std::size_t i = 0;
  if (i < value.size() && (value[i] == '+' || value[i] == '-'))
    ++i;
  auto digits = [&] {
    const auto start = i;
    while (i < value.size() && value[i] >= '0' && value[i] <= '9')
      ++i;
    return i != start;
  };
  const bool integral = digits();
  bool fractional = false;
  if (i < value.size() && value[i] == '.') {
    ++i;
    fractional = digits();
  }
  if (!integral && !fractional)
    invalid_content();
  if (i < value.size() && (value[i] == 'e' || value[i] == 'E')) {
    ++i;
    if (i < value.size() && (value[i] == '+' || value[i] == '-'))
      ++i;
    if (!digits())
      invalid_content();
  }
  if (i != value.size())
    invalid_content();
}

static auto provider_name(MusicProvider provider) -> std::string_view {
  switch (provider) {
  case MusicProvider::Qq:
    return "qq";
  case MusicProvider::NetEase163:
    return "163";
  case MusicProvider::Xiami:
    return "xm";
  }
  invalid_content();
}

template <class Ref>
auto native_id(const Ref &ref,
               const obcx::bot::BotInstallationRef &installation,
               const obcx::bot::binding::NativeReferenceCodec &codec,
               std::string_view kind) -> std::string {
  try {
    codec.validate(ref);
    if (ref.installation != installation)
      invalid_content();
    auto parts = codec.unpack(ref.id);
    if (parts.kind != kind || parts.components.size() != 1)
      invalid_content();
    return std::move(parts.components[0]);
  } catch (const std::exception &) {
    // Codec diagnostics must not turn caller content into log material.
    invalid_content();
  }
}

// Views borrow caller strings; only bounded native ID decoding owns storage.
void visit_leaf_fields(const SendLeaf &leaf,
                       const obcx::bot::BotInstallationRef &installation,
                       const obcx::bot::binding::NativeReferenceCodec &codec,
                       ContentLeafVisitor visit) {
  std::visit(
      [&](const auto &v) {
        using T = std::decay_t<decltype(v)>;
        std::array<Field, 6> fields;
        std::size_t count = 0;
        std::string_view type;
        std::string id;
        std::string timeout;
        auto field = [&](std::string_view key, std::string_view value) {
          fields[count++] = {key, value};
        };
        auto optional = [&](std::string_view key,
                            const std::optional<std::string> &value) {
          if (value)
            field(key, *value);
        };
        if constexpr (std::is_same_v<T, Image> || std::is_same_v<T, Record> ||
                      std::is_same_v<T, Video>) {
          if constexpr (std::is_same_v<T, Image>) {
            type = "image";
            if (v.flash)
              field("type", "flash");
          } else if constexpr (std::is_same_v<T, Record>) {
            type = "record";
            field("magic", v.magic ? "1" : "0");
          } else
            type = "video";
          std::visit(
              [&](const auto &source) {
                using S = std::decay_t<decltype(source)>;
                if constexpr (std::is_same_v<S, ProviderFileName>) {
                  if (source.installation != installation ||
                      source.work != DelegatedWork::AcceptUnmeasured ||
                      source.name.empty() || source.name == "." ||
                      source.name == ".." ||
                      source.name.find_first_of("/\\:") != std::string::npos ||
                      source.name.find('\0') != std::string::npos)
                    invalid_content();
                  field("file", source.name);
                } else if constexpr (std::is_same_v<S, CallerFileUri>) {
                  if (!source.uri.starts_with("file:///") ||
                      source.uri.size() <= 8 ||
                      source.uri.find('\0') != std::string::npos)
                    invalid_content();
                  field("file", source.uri);
                } else if constexpr (std::is_same_v<S, InlineBase64>) {
                  validate_send_base64(source.data);
                  id = "base64://" + source.data;
                  field("file", id);
                } else {
                  if (source.work != DelegatedWork::AcceptUnmeasured ||
                      !(source.url.starts_with("http://") ||
                        source.url.starts_with("https://")) ||
                      source.url.find('\0') != std::string::npos)
                    invalid_content();
                  const auto start = source.url.find("://") + 3;
                  if (start == source.url.size() || source.url[start] == '/' ||
                      source.url[start] == '?' || source.url[start] == '#')
                    invalid_content();
                  field("file", source.url);
                  field("cache", source.cache ? "1" : "0");
                  field("proxy", source.proxy ? "1" : "0");
                  if (source.timeout_seconds) {
                    timeout = std::to_string(*source.timeout_seconds);
                    field("timeout", timeout);
                  }
                }
              },
              v.source);
        } else if constexpr (std::is_same_v<T, Text>) {
          type = "text";
          field("text", v.text);
        } else if constexpr (std::is_same_v<T, Face>) {
          type = "face";
          field("id", v.id);
        } else if constexpr (std::is_same_v<T, AtUser>) {
          type = "at";
          id = native_id(v.user, installation, codec, "user");
          field("qq", id);
        } else if constexpr (std::is_same_v<T, AtAll>) {
          type = "at";
          field("qq", "all");
        } else if constexpr (std::is_same_v<T, Rps>) {
          type = "rps";
        } else if constexpr (std::is_same_v<T, Dice>) {
          type = "dice";
        } else if constexpr (std::is_same_v<T, Shake>) {
          type = "shake";
        } else if constexpr (std::is_same_v<T, Poke>) {
          type = "poke";
          field("type", v.type);
          field("id", v.id);
        } else if constexpr (std::is_same_v<T, Anonymous>) {
          type = "anonymous";
          if (v.ignore)
            field("ignore", *v.ignore ? "1" : "0");
        } else if constexpr (std::is_same_v<T, Share>) {
          type = "share";
          field("url", v.url);
          field("title", v.title);
          optional("content", v.content);
          optional("image", v.image_url);
        } else if constexpr (std::is_same_v<T, UserContact>) {
          type = "contact";
          id = native_id(v.user, installation, codec, "user");
          field("type", "qq");
          field("id", id);
        } else if constexpr (std::is_same_v<T, GroupContact>) {
          type = "contact";
          id = native_id(v.group, installation, codec, "group");
          field("type", "group");
          field("id", id);
        } else if constexpr (std::is_same_v<T, Location>) {
          type = "location";
          field("lat", v.latitude.decimal);
          field("lon", v.longitude.decimal);
          optional("title", v.title);
          optional("content", v.content);
        } else if constexpr (std::is_same_v<T, Music>) {
          type = "music";
          field("type", provider_name(v.provider));
          field("id", v.id);
        } else if constexpr (std::is_same_v<T, CustomMusic>) {
          type = "music";
          field("type", "custom");
          field("url", v.url);
          field("audio", v.audio_url);
          field("title", v.title);
          optional("content", v.content);
          optional("image", v.image_url);
        } else if constexpr (std::is_same_v<T, XmlContent>) {
          type = "xml";
          field("data", v.document);
        } else if constexpr (std::is_same_v<T, JsonContent>) {
          type = "json";
          field("data", v.document);
        }
        visit(type, std::span<const Field>(fields.data(), count));
        // The borrowed-input pass bounded these strings before this scan.
        if constexpr (std::is_same_v<T, Location>) {
          decimal(v.latitude.decimal);
          decimal(v.longitude.decimal);
        }
      },
      leaf);
}

void inspect_segment(ContentBudget &budget, std::string_view type,
                     std::span<const Field> fields, std::size_t depth) {
  budget.node(depth);
  budget.string("type");
  budget.string_value(type, depth + 1);
  budget.string("data");
  budget.node(depth + 1);
  for (const auto &[key, value] : fields) {
    budget.string(key);
    budget.string_value(value, depth + 2);
  }
}

auto encode_content(const SendContent &content,
                    const obcx::bot::BotInstallationRef &installation,
                    const obcx::bot::binding::NativeReferenceCodec &codec,
                    const ValueLimits &limits) -> EncodedContent {
  limits.validate();
  ContentBudget budget(limits);
  inspect_installation(budget, installation, 0);
  inspect_input(budget, content);
  // All actual borrowed strings/refs have been charged before native decoding
  // or retained output copies. Codec scratch residency is an integration gate.
  try {
    codec.validate(installation);
    if (installation.surface != obcx::bot::SurfaceId{"onebot11.qq"})
      invalid_content();
  } catch (const std::exception &) {
    invalid_content();
  }
  budget.node(0); // EncodedContent result
  budget.node(1); // auto_escape
  if (const auto *literal = std::get_if<LiteralText>(&content)) {
    budget.string_value(literal->text, 1); // retained message copy
    return {literal->text, true};
  }
  const auto &composed = std::get<ComposedContent>(content);
  const bool cq = composed.encoding == ContentEncoding::CqString;
  const bool single = composed.encoding == ContentEncoding::SingleSegmentObject;
  if ((!cq && !single && composed.encoding != ContentEncoding::SegmentArray) ||
      (single && composed.segments.size() != 1) ||
      composed.segments.size() >
          std::min(limits.maximum_nodes, obcx::bot::detail::bot_maximum_items))
    invalid_content();
  if (!single)
    budget.node(1); // message array or CQ string
  std::size_t cq_length = 0;
  for (const auto &leaf : composed.segments) {
    visit_leaf(leaf, installation, codec,
               [&](std::string_view type, std::span<const Field> fields) {
                 if (cq) {
                   emit_cq(type, fields, [&](std::string_view part) {
                     budget.string_part(cq_length, part.size());
                   });
                 } else {
                   inspect_segment(budget, type, fields, single ? 1 : 2);
                 }
               });
  }
  // No payload is copied until every leaf and the chosen expansion fit.
  std::string cq_text;
  Json message = single || cq ? Json{} : Json::array();
  for (const auto &leaf : composed.segments) {
    visit_leaf(leaf, installation, codec,
               [&](std::string_view type, std::span<const Field> fields) {
                 if (cq) {
                   emit_cq(type, fields, [&](std::string_view part) {
                     cq_text.append(part);
                   });
                 } else {
                   Json data = Json::object();
                   for (const auto &[key, value] : fields)
                     data[std::string(key)] = std::string(value);
                   Json segment{{"type", type}, {"data", std::move(data)}};
                   if (single)
                     message = std::move(segment);
                   else
                     message.push_back(std::move(segment));
                 }
               });
  }
  if (cq)
    message = std::move(cq_text);
  return {std::move(message), false};
}
} // namespace obcx::onebot11::bot::standard::detail
