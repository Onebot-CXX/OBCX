#pragma once
#include "core/bot/domain_messaging.hpp"
#include "onebot11/bot/media_path.hpp"

namespace obcx::onebot11::bot::detail {
// Implemented inside the platform module; no native ID escapes to actors.
auto forward_group_width(const obcx::bot::ConversationRef &target)
    -> std::size_t;
auto forward_user_width(const obcx::bot::UserRef &user) -> std::size_t;
enum class ForwardBudgetScope { WholeSend, IndependentNodes };

// Borrowed traversal only: no JSON dumps, body copies or base64 buffers.
// Bounds are derived from this adapter's native node/segment envelope. Unknown
// source lengths are checked against the remaining envelope during preparation.
// Version the implementation name: old installed actor headers emit weak inline
// methods with the original class layout. Never interpose a new layout on them.
class ForwardPreflightV2 {
public:
  ForwardPreflightV2(const obcx::bot::ConversationRef &target,
                     const obcx::bot::SendLimits &limits, std::size_t envelope,
                     ForwardBudgetScope scope)
      : limits_(limits), installation_(target.installation),
        envelope_(envelope), scope_(scope) {
    limits_.validate();
    if (!envelope_ || envelope_ > obcx::bot::detail::domain_maximum_bytes)
      throw std::invalid_argument("QQ forward envelope bound is invalid");
    charge(sizeof("{\"action\":\"send_group_forward_msg\",\"params\":{\"group_"
                  "id\":\"\",\"messages\":[]},\"echo\":}") -
           1 + forward_group_width(target) + 20);
  }
  void node(const obcx::bot::UserRef &author, std::string_view name,
            const obcx::bot::MessageBody &body) {
    node_header(author, name);
    node_body(body);
  }
  void authorless_node(const obcx::bot::MessageBody &body) {
    authorless_node_header();
    node_body(body);
  }

private:
  void node_body(const obcx::bot::MessageBody &body) {
    for (const auto &block : body)
      std::visit(
          [&](const auto &value) {
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<T, obcx::bot::RichText>)
              rich_text(value);
            else {
              if constexpr (!std::same_as<T, obcx::bot::Image> &&
                            !std::same_as<T, obcx::bot::Animation>)
                throw std::invalid_argument(
                    "QQ forward media recipe is unsupported");
              media_header();
              std::visit(
                  [&](const auto &source) {
                    using S = std::remove_cvref_t<decltype(source)>;
                    if constexpr (std::same_as<S, obcx::bot::BytesSource>)
                      media_size(source.bytes.size());
                    else if constexpr (std::same_as<S, obcx::bot::LocalFile>) {
                      if (source.metadata.size_bytes)
                        media_size(*source.metadata.size_bytes, false);
                      charge(media_file_uri_size(source.path));
                    } else if constexpr (std::same_as<S,
                                                      obcx::bot::HttpSource>) {
                      if (source.metadata.size_bytes)
                        media_size(*source.metadata.size_bytes);
                    }
                  },
                  value.source);
              rich_text(value.caption);
            }
          },
          block);
  }

public:
  void node(const obcx::bot::Json &node) {
    const auto &name = node.at("display_name");
    if (!name.is_string())
      throw std::invalid_argument("QQ forward name is not text");
    node_header(node.at("author").get<obcx::bot::UserRef>(),
                name.get_ref<const std::string &>());
    node_body(node.at("body"));
  }
  void authorless_node(const obcx::bot::Json &node) {
    authorless_node_header();
    node_body(node.at("body"));
  }

private:
  void node_body(const obcx::bot::Json &body) {
    if (!body.is_array() || body.empty() ||
        body.size() > obcx::bot::detail::domain_maximum_items)
      throw std::invalid_argument("QQ forward body exceeds bounds");
    for (const auto &block : body) {
      auto type =
          obcx::bot::detail::bounded_unsigned(block.at("alternative"), 5);
      const auto &value = block.at("value");
      if (type == 0)
        rich_text(value);
      else {
        if (type != 1 && type != 2)
          throw std::invalid_argument("QQ forward media recipe is unsupported");
        media_header();
        const auto &source = value.at("source");
        auto source_type =
            obcx::bot::detail::bounded_unsigned(source.at("alternative"), 3);
        const auto &data = source.at("value");
        if (source_type == 1) {
          const auto &bytes = data.at("bytes");
          if (!bytes.is_binary())
            throw std::invalid_argument("QQ forward bytes must be binary");
          media_size(bytes.get_binary().size());
        } else {
          if (source_type != 0 &&
              !data.at("metadata").at("size_bytes").is_null())
            media_size(obcx::bot::detail::bounded_unsigned(
                           data.at("metadata").at("size_bytes"), UINT64_MAX),
                       source_type != 2);
          if (source_type == 2)
            charge(media_file_uri_size(obcx::bot::detail::require_identifier(
                data, "path", "LocalFile", 4096)));
        }
        rich_text(value.at("caption"));
      }
    }
  }

public:
  void prepared_media(std::size_t encoded_bytes,
                      std::optional<std::uint64_t> known_bytes) {
    const auto reserved =
        known_bytes ? 9 + 4 * ((static_cast<std::size_t>(*known_bytes) + 2) / 3)
                    : 0;
    if (encoded_bytes > reserved)
      charge(encoded_bytes - reserved);
  }
  auto maximum_owned_bytes(std::optional<std::uint64_t> known_bytes) const
      -> std::size_t {
    const auto reserved =
        known_bytes ? 9 + 4 * ((static_cast<std::size_t>(*known_bytes) + 2) / 3)
                    : 0;
    const auto capacity = envelope_ - used_ + reserved;
    return capacity <= 9 ? 0 : ((capacity - 9) / 4) * 3;
  }
  static auto escaped_size(std::string_view value) -> std::size_t {
    std::size_t result = 0;
    for (unsigned char c : value) {
      const auto count = escaped_character_size(c);
      if (count > obcx::bot::detail::domain_maximum_bytes - result)
        throw std::invalid_argument("QQ native string exceeds envelope bound");
      result += count;
    }
    return result;
  }

private:
  void charge(std::size_t n) {
    if (n > envelope_ - used_)
      throw std::invalid_argument("QQ forward native envelope exceeds budget");
    used_ += n;
  }
  static auto escaped_character_size(unsigned char c) -> std::size_t {
    switch (c) {
    case '"':
    case '\\':
    case '\b':
    case '\f':
    case '\n':
    case '\r':
    case '\t':
      return 2;
    default:
      return c < 0x20 ? 6 : 1;
    }
  }
  void quoted_content(std::string_view text) {
    for (unsigned char c : text)
      charge(escaped_character_size(c));
  }
  void begin_node() {
    if (!first_node_)
      charge(1);
    first_node_ = false;
    first_segment_ = true;
    if (scope_ == ForwardBudgetScope::IndependentNodes)
      known_ = 0;
  }
  void begin_segment() {
    if (!first_segment_)
      charge(1);
    first_segment_ = false;
  }
  auto user_width(const obcx::bot::UserRef &user) const -> std::size_t {
    if (user.installation != installation_)
      throw std::invalid_argument("QQ forward user scope mismatch");
    return forward_user_width(user);
  }
  void node_header(const obcx::bot::UserRef &author, std::string_view name) {
    const auto width = user_width(author);
    begin_node();
    charge(sizeof("{\"type\":\"node\",\"data\":{\"uin\":\"\",\"name\":\"\","
                  "\"content\":[]}}") -
           1 + width);
    quoted_content(name);
  }
  void authorless_node_header() {
    begin_node();
    charge(sizeof("{\"type\":\"node\",\"data\":{\"content\":[]}}") - 1);
  }
  void media_header() {
    begin_segment();
    charge(sizeof("{\"type\":\"image\",\"data\":{\"file\":\"\"}}") - 1);
  }
  void media_size(std::uint64_t size, bool owned = true) {
    if (!size || size > limits_.maximum_media_item_bytes ||
        size > limits_.maximum_media_total_bytes - known_)
      throw std::invalid_argument("QQ forward aggregate media exceeds budget");
    known_ += static_cast<std::size_t>(size);
    if (owned)
      charge(9 + 4 * ((static_cast<std::size_t>(size) + 2) / 3));
  }
  void text(std::string_view content) {
    begin_segment();
    charge(sizeof("{\"type\":\"text\",\"data\":{\"text\":\"\"}}") - 1);
    quoted_content(content);
  }
  void mention(const obcx::bot::UserRef &user) {
    const auto width = user_width(user);
    begin_segment();
    charge(sizeof("{\"type\":\"at\",\"data\":{\"qq\":\"\"}}") - 1 + width);
  }
  void rich_text(const obcx::bot::RichText &value) {
    for (const auto &part : value)
      std::visit(
          [&](const auto &span) {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(span)>,
                                       obcx::bot::Text>) {
              if (!span.styles.empty())
                throw std::invalid_argument("QQ text styles unsupported");
              text(span.content);
            } else
              mention(span.user);
          },
          part);
  }
  void rich_text(const obcx::bot::Json &value) {
    if (!value.is_array() ||
        value.size() > obcx::bot::detail::domain_maximum_items)
      throw std::invalid_argument("QQ rich text exceeds bounds");
    for (const auto &part : value) {
      auto type =
          obcx::bot::detail::bounded_unsigned(part.at("alternative"), 1);
      if (type == 0) {
        const auto &span = part.at("value");
        if (!span.at("styles").is_array() || !span.at("styles").empty() ||
            !span.at("content").is_string())
          throw std::invalid_argument("QQ text/style shape unsupported");
        text(span.at("content").get_ref<const std::string &>());
      } else
        mention(part.at("value").at("user").get<obcx::bot::UserRef>());
    }
  }
  const obcx::bot::SendLimits &limits_;
  obcx::bot::BotInstallationRef installation_;
  std::size_t envelope_, used_{}, known_{};
  ForwardBudgetScope scope_;
  bool first_node_ = true;
  bool first_segment_ = true;
};
using ForwardPreflight = ForwardPreflightV2;
} // namespace obcx::onebot11::bot::detail
