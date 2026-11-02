#pragma once

// Process/platform binding seam; not an actor service. No singleton or provider
// dependency. Construct the registry once from the installed platform codecs.
#include "core/bot/domain_queries.hpp"
#include <map>
#include <memory>
#include <span>

namespace obcx::bot::binding {
class NativeReferenceCodec {
public:
  virtual ~NativeReferenceCodec() = default;
  virtual auto surface() const -> SurfaceId = 0;
  // Must enforce exact native kind, component count, canonical native spelling,
  // ranges and sub-conversation rules. Never perform I/O or return credentials.
  virtual void validate_native(
      std::string_view tag, std::string_view kind,
      std::span<const std::string> components) const = 0;
  virtual auto normalize_username(std::string_view username) const
      -> std::string = 0;

  template <class Id>
  auto make(std::string kind, std::vector<std::string> components) const -> Id {
    using Tag = typename Id::tag_type;
    detail::OpaqueParts parts{surface(), std::move(kind),
                              std::move(components)};
    auto encoded = detail::encode_opaque(Tag::name, parts);
    validate_native(Tag::name, parts.kind, parts.components);
    return Id{std::move(encoded)};
  }
  template <class Id> auto unpack(const Id &id) const -> detail::OpaqueParts {
    using Tag = typename Id::tag_type;
    auto parts = detail::decode_opaque(Tag::name, id.serialized());
    if (parts.surface != surface())
      throw std::invalid_argument("native codec surface mismatch");
    validate_native(Tag::name, parts.kind, parts.components);
    return parts;
  }
  void validate(const BotInstallationRef &installation) const {
    installation.validate();
    if (installation.surface != surface())
      throw std::invalid_argument("native installation surface mismatch");
  }
  void validate(const ConversationRef &v) const {
    v.validate();
    validate(v.installation);
    (void)unpack(v.id);
  }
  void validate(const UserRef &v) const {
    v.validate();
    validate(v.installation);
    (void)unpack(v.id);
  }
  void validate(const MessageRef &v) const {
    v.validate();
    validate(v.conversation);
    (void)unpack(v.id);
  }
  void validate(const MediaRef &v) const {
    v.validate();
    validate(v.installation);
    (void)unpack(v.id);
  }
  void validate(const MediaIdentity &v) const {
    v.validate();
    validate(v.installation);
    (void)unpack(v.id);
  }
  void validate(const MediaGroupRef &v) const {
    v.validate();
    validate(v.conversation);
    (void)unpack(v.id);
  }
  void validate(const ReplyRef &v) const {
    v.validate();
    validate(v.source());
    validate(v.target());
  }
};
class NativeReferenceCodecs final {
public:
  explicit NativeReferenceCodecs(
      std::vector<std::shared_ptr<const NativeReferenceCodec>> codecs) {
    for (auto &codec : codecs) {
      if (!codec || !codecs_.emplace(codec->surface(), codec).second)
        throw std::invalid_argument("missing or duplicate native codec");
    }
  }
  auto for_surface(const SurfaceId &surface) const
      -> const NativeReferenceCodec & {
    const auto found = codecs_.find(surface);
    if (found == codecs_.end())
      throw std::invalid_argument("native codec not installed");
    return *found->second;
  }

private:
  std::map<SurfaceId, std::shared_ptr<const NativeReferenceCodec>> codecs_;
};
inline void validate_native_request(const NativeReferenceCodec &codec,
                                    const SendMessageRequest &request) {
  request.validate();
  codec.validate(request.target);
  if (request.reply_to)
    codec.validate(*request.reply_to);
  auto text = [&](const RichText &value) {
    for (const auto &span : value)
      if (const auto *mention = std::get_if<Mention>(&span))
        codec.validate(mention->user);
  };
  for (const auto &block : request.body)
    std::visit(
        [&](const auto &v) {
          if constexpr (std::same_as<std::decay_t<decltype(v)>, RichText>)
            text(v);
          else {
            text(v.caption);
            if (const auto *media = std::get_if<MediaRef>(&v.source))
              codec.validate(*media);
          }
        },
        block);
}
inline void validate_native_request(const NativeReferenceCodec &codec,
                                    const DeleteMessageRequest &v) {
  v.validate();
  codec.validate(v.message);
}
inline void validate_native_request(const NativeReferenceCodec &codec,
                                    const GetSelfRequest &v) {
  v.validate();
  codec.validate(v.installation);
}
inline void validate_native_request(const NativeReferenceCodec &codec,
                                    const GetMemberRequest &v) {
  v.validate();
  codec.validate(v.conversation);
  codec.validate(v.user);
}
inline void validate_native_request(const NativeReferenceCodec &codec,
                                    const ResolveReplyRequest &v) {
  v.validate();
  codec.validate(v.reply);
}
inline void validate_native_request(const NativeReferenceCodec &codec,
                                    const ReadMediaRequest &v) {
  v.validate();
  codec.validate(v.media);
}
inline void validate_native_request(const NativeReferenceCodec &codec,
                                    const GetCapabilitiesRequest &v) {
  v.validate();
  codec.validate(v.conversation);
}

inline void validate_native_sender(const NativeReferenceCodec &codec,
                                   const Sender &sender) {
  std::visit(
      [&](const auto &v) {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::same_as<V, KnownUser>)
          codec.validate(v.user);
        else if constexpr (std::same_as<V, ConversationSender>)
          codec.validate(v.conversation);
      },
      sender);
}
inline void validate_native_reply(const NativeReferenceCodec &codec,
                                  const ReplyInfo &v) {
  v.validate();
  codec.validate(v.source());
  if (v.reference())
    codec.validate(*v.reference());
  if (v.author())
    validate_native_sender(codec, *v.author());
}
inline void validate_native_self(const NativeReferenceCodec &codec,
                                 const SelfInfo &v) {
  v.validate();
  codec.validate(v.user);
  if (v.username && codec.normalize_username(*v.username) != *v.username)
    throw std::invalid_argument("noncanonical self username");
}
inline void validate_native_event(const NativeReferenceCodec &codec,
                                  const MessageReceived &event) {
  event.validate();
  codec.validate(event.message);
  validate_native_sender(codec, event.sender);
  if (event.reply)
    validate_native_reply(codec, *event.reply);
  if (event.media_group)
    codec.validate(*event.media_group);
  auto text = [&](const ReceivedRichText &value) {
    for (const auto &span : value)
      std::visit(
          [&](const auto &v) {
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::same_as<V, Mention>)
              codec.validate(v.user);
            else if constexpr (std::same_as<V, UnresolvedMention>) {
              if (codec.normalize_username(v.username) != v.username)
                throw std::invalid_argument("noncanonical mention username");
            }
          },
          span);
  };
  for (const auto &block : event.body)
    std::visit(
        [&](const auto &v) {
          using V = std::decay_t<decltype(v)>;
          if constexpr (std::same_as<V, ReceivedRichText>)
            text(v);
          else if constexpr (std::same_as<V, ReceivedMedia>) {
            codec.validate(v.media);
            if (v.identity)
              codec.validate(*v.identity);
            text(v.caption);
          }
        },
        block);
}

class EvidenceFactory {
public:
  static auto reply_ref(MessageRef source, MessageRef target,
                        std::string observation_id) -> ReplyRef {
    ReplyRef value{std::move(source), std::move(target),
                   std::move(observation_id)};
    value.validate();
    return value;
  }
  static auto reply_info(MessageRef source, std::optional<ReplyRef> reference,
                         std::optional<Sender> author, ReplyRelation relation,
                         ReplyAvailability availability) -> ReplyInfo {
    ReplyInfo value{std::move(source), std::move(reference), std::move(author),
                    relation, availability};
    value.validate();
    return value;
  }
};
// Verification must bind the source/target tuple to still-admitted evidence.
// A restored or factory-constructed ReplyRef is not authority for get_msg.
class ReplyEvidenceVerifier {
public:
  virtual ~ReplyEvidenceVerifier() = default;
  virtual auto admits(const ReplyRef &reply) const -> bool = 0;
};
inline void validate_reply_admission(const NativeReferenceCodec &codec,
                                     const ReplyEvidenceVerifier &evidence,
                                     const ResolveReplyRequest &request) {
  validate_native_request(codec, request);
  if (!evidence.admits(request.reply))
    throw std::invalid_argument("reply relationship is not admitted evidence");
}
} // namespace obcx::bot::binding
