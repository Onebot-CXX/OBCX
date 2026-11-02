#include "core/bot/reply_evidence_store.hpp"

#include <algorithm>
#include <iterator>
#include <limits>

namespace obcx::core {
namespace {

auto same_author(const bot::Sender &left, const bot::Sender &right) -> bool {
  return std::visit(
      [&]<typename Author>(const Author &author) {
        const auto *other = std::get_if<Author>(&right);
        if (!other)
          return false;
        if constexpr (std::same_as<Author, bot::KnownUser>)
          return author.user == other->user;
        else if constexpr (std::same_as<Author, bot::ConversationSender>)
          return author.conversation == other->conversation;
        else
          return false;
      },
      left);
}

} // namespace

ReplyEvidenceStore::ReplyEvidenceStore(bot::BotInstallationRef installation)
    : installation_(std::move(installation)) {
  installation_.validate();
}

auto ReplyEvidenceStore::generation() const -> std::optional<std::uint64_t> {
  std::scoped_lock lock(mutex_);
  return closed_ ? std::nullopt : std::optional{generation_};
}

auto ReplyEvidenceStore::conflicts(const Entry &entry) const -> bool {
  return std::ranges::any_of(entries_, [&](const Entry &existing) {
    return existing.message == entry.message &&
           (existing.author_conflicted ||
            (existing.author && entry.author &&
             !same_author(*existing.author, *entry.author)));
  });
}

void ReplyEvidenceStore::insert(Entry entry) {
  // A subsequent observation cannot overwrite contradictory evidence into
  // confidence within the same connected account generation.
  entry.author_conflicted = entry.author_conflicted || conflicts(entry);
  if (entry.author_conflicted) {
    for (auto &existing : entries_)
      if (existing.message == entry.message)
        existing.author_conflicted = true;
  }
  // Allocate before replacing. If allocation fails, existing facts
  // (especially the conflict bits just marked above) remain resident.
  entries_.push_back(std::move(entry));
  for (auto it = entries_.begin(); it != std::prev(entries_.end());) {
    const auto &added = entries_.back();
    const bool replace =
        added.reply && it->reply
            ? it->reply->reference()->observation_id() ==
                  added.reply->reference()->observation_id()
            : !added.reply && !it->reply && it->message == added.message;
    if (replace)
      it = entries_.erase(it);
    else
      ++it;
  }
}

auto ReplyEvidenceStore::admit(const bot::MessageReceived &message,
                               std::uint64_t expected_generation) -> bool {
  message.validate();
  if (message.message.conversation.installation != installation_)
    throw std::invalid_argument(
        "reply evidence belongs to another installation");
  std::scoped_lock lock(mutex_);
  if (closed_ || generation_ != expected_generation)
    return false;
  std::optional<Entry> relation;
  if (message.reply && message.reply->reference()) {
    relation.emplace(Entry{message.reply->reference()->target(),
                           message.reply->author(), message.reply, false});
    // Preserve contradictions before inserting the source fact.
    relation->author_conflicted = conflicts(*relation);
  }
  if (!std::holds_alternative<bot::UnknownSender>(message.sender))
    insert({message.message, message.sender, std::nullopt, false});
  if (relation)
    insert(std::move(*relation));
  return true;
}

auto ReplyEvidenceStore::admit_confirmation(const bot::MessageRef &message,
                                            const bot::Sender &author,
                                            std::uint64_t expected_generation)
    -> bool {
  message.validate();
  const bool known = std::visit(
      [&]<class Author>(const Author &value) {
        if constexpr (std::same_as<Author, bot::KnownUser>) {
          value.validate();
          return value.user.installation == installation_;
        } else if constexpr (std::same_as<Author, bot::ConversationSender>) {
          value.validate();
          return value.conversation.installation == installation_;
        } else
          return false;
      },
      author);
  if (message.conversation.installation != installation_ || !known)
    throw std::invalid_argument("confirmation evidence installation mismatch");
  std::scoped_lock lock(mutex_);
  if (closed_ || generation_ != expected_generation)
    return false;
  // Mark an already-retained contradiction before any replacement copy.
  const bool conflict = std::ranges::any_of(entries_, [&](const Entry &entry) {
    return entry.message == message &&
           (entry.author_conflicted ||
            (entry.author && !same_author(*entry.author, author)));
  });
  if (conflict)
    for (auto &entry : entries_)
      if (entry.message == message)
        entry.author_conflicted = true;
  insert({message, author, std::nullopt, conflict});
  return true;
}

auto ReplyEvidenceStore::admit_confirmation_conflict(
    const bot::MessageRef &message, std::uint64_t expected_generation) -> bool {
  message.validate();
  if (message.conversation.installation != installation_)
    throw std::invalid_argument("confirmation conflict installation mismatch");
  std::scoped_lock lock(mutex_);
  if (closed_ || generation_ != expected_generation)
    return false;
  for (auto &entry : entries_)
    if (entry.message == message)
      entry.author_conflicted = true;
  insert({message, std::nullopt, std::nullopt, true});
  return true;
}

auto ReplyEvidenceStore::find(const bot::ReplyRef &reply) const
    -> std::optional<bot::ReplyInfo> {
  reply.validate();
  if (reply.source().conversation.installation != installation_)
    return std::nullopt;
  std::scoped_lock lock(mutex_);
  if (closed_)
    return std::nullopt;
  const auto found = std::ranges::find_if(entries_, [&](const Entry &entry) {
    return entry.reply && *entry.reply->reference() == reply;
  });
  if (found == entries_.end())
    return std::nullopt;
  // Author facts cannot create a relation. Full MessageRef
  // equality includes installation and the topic-bearing conversation.
  std::optional<bot::Sender> author;
  for (const auto &entry : entries_) {
    if (entry.message != reply.target())
      continue;
    // Contradictory local evidence is unavailable, not provider permission.
    if (entry.author_conflicted)
      return std::nullopt;
    if (entry.author) {
      if (author && !same_author(*author, *entry.author))
        return std::nullopt;
      author = entry.author;
    }
  }
  if (!author)
    return found->reply;
  return bot::binding::EvidenceFactory::reply_info(
      reply.source(), reply, std::move(author), found->reply->relation(),
      bot::ReplyAvailability::AuthorKnown);
}

auto ReplyEvidenceStore::admits(const bot::ReplyRef &reply) const -> bool {
  return find(reply).has_value();
}

void ReplyEvidenceStore::invalidate() noexcept {
  std::scoped_lock lock(mutex_);
  entries_.clear();
  if (generation_ == std::numeric_limits<std::uint64_t>::max())
    closed_ = true;
  else
    ++generation_;
}

void ReplyEvidenceStore::close() noexcept {
  std::scoped_lock lock(mutex_);
  closed_ = true;
  if (generation_ != std::numeric_limits<std::uint64_t>::max())
    ++generation_;
  entries_.clear();
}

} // namespace obcx::core
