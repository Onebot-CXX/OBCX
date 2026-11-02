#pragma once

#include "onebot11/bot/standard_content.hpp"
#include "onebot11/bot/standard_queries.hpp"

namespace obcx::onebot11::bot::standard {

struct Reply {
  obcx::bot::MessageRef target;
};
struct ScopedReferenceNode {
  obcx::bot::MessageRef message;
  DelegatedWork work;
};
// An installation-scoped selector grants no conversation/reply/source
// authority.
struct IdSelectedNode {
  MessageSelector message;
  DelegatedWork work;
};
struct SequenceIndex {
  std::uint32_t value;
};
struct AuthoredNode {
  obcx::bot::UserRef author;
  std::string nickname;
  SequenceIndex content;
};
using Segment = std::variant<SendLeaf, Reply, ScopedReferenceNode,
                             IdSelectedNode, AuthoredNode>;
struct Sequence {
  ContentEncoding encoding;
  std::vector<Segment> segments;
};
// Owned rooted tree: root 0, later-only child indices, exactly one parent per
// nonroot sequence. No aliases, orphans or cycles. No provider node-count cap.
// CQ parents require CQ children; arrays/objects preserve each child's form.
struct Composition {
  std::vector<Sequence> sequences;
};
using MessageInput = std::variant<LiteralText, Composition>;

} // namespace obcx::onebot11::bot::standard
