#include "received_media.hpp"
#include "network/detail/media_fetch.hpp"
#include "network/image_fetch.hpp"
#include <boost/asio/error.hpp>
#include <boost/system/system_error.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <utility>

namespace obcx::onebot11::bot::detail {
namespace {
namespace bot_model = obcx::bot;
using Result = bot_model::BotOperationResult<bot_model::MediaBlob>;
using Code = bot_model::BotOperationErrorCode;

auto fresh_uuid() -> std::string {
  return boost::uuids::to_string(boost::uuids::random_generator_pure{}());
}
void validate_uuid(std::string_view value) {
  if (value.size() != 36)
    throw std::invalid_argument("invalid received media UUID");
  for (std::size_t i = 0; i < value.size(); ++i) {
    const bool hyphen = i == 8 || i == 13 || i == 18 || i == 23;
    if (hyphen ? value[i] != '-'
               : !((value[i] >= '0' && value[i] <= '9') ||
                   (value[i] >= 'a' && value[i] <= 'f')))
      throw std::invalid_argument("noncanonical received media UUID");
  }
}
auto failure(Code code) -> Result {
  const char *message = "QQ received media transport failed";
  switch (code) {
  case Code::CacheUnavailable:
    message = "QQ received media is unavailable";
    break;
  case Code::MediaTooLarge:
    message = "QQ received media exceeds byte budget";
    break;
  case Code::Cancelled:
    message = "QQ received media read cancelled";
    break;
  case Code::ProviderRejected:
    message = "QQ received media HTTP resource is inaccessible";
    break;
  case Code::MalformedResponse:
    message = "QQ received media response is invalid";
    break;
  case Code::InvalidRequest:
    message = "QQ received media read bound is invalid";
    break;
  default:
    break;
  }
  return Result::failure(
      {.code = code,
       .message = message,
       .retryable = false,
       .submission_safety =
           bot_model::SubmissionSafety::DefinitelyNotSubmitted});
}
auto is_forward_query_kind(std::string_view kind) -> bool {
  return kind == "queried-forward-image" || kind == "queried-forward-record" ||
         kind == "queried-forward-video" || kind == "queried-forward-file";
}
auto kind_of(std::string_view kind) -> bot_model::MediaKind {
  if (kind == "received-image" || kind == "received-mface" ||
      kind == "queried-forward-image")
    return bot_model::MediaKind::Image;
  if (kind == "received-record" || kind == "queried-forward-record")
    return bot_model::MediaKind::Voice;
  if (kind == "received-video" || kind == "queried-forward-video")
    return bot_model::MediaKind::Video;
  if (kind == "received-file" || kind == "queried-forward-file")
    return bot_model::MediaKind::File;
  throw std::invalid_argument("invalid received media kind");
}
} // namespace

auto is_received_media_kind(std::string_view kind) -> bool {
  return kind == "received-image" || kind == "received-record" ||
         kind == "received-video" || kind == "received-mface" ||
         kind == "received-file" || is_forward_query_kind(kind);
}
void validate_received_media_token(std::string_view token) {
  // Two UUIDs, two colons and a canonical positive uint64 decimal.
  if (token.size() < 75 || token.size() > 94 || token[36] != ':' ||
      token[token.size() - 37] != ':')
    throw std::invalid_argument("invalid received media token");
  validate_uuid(token.substr(0, 36));
  validate_uuid(token.substr(token.size() - 36));
  const auto sequence = token.substr(37, token.size() - 74);
  std::uint64_t value{};
  const auto [end, error] = std::from_chars(
      sequence.data(), sequence.data() + sequence.size(), value);
  if (sequence.front() < '1' || sequence.front() > '9' ||
      error != std::errc{} || end != sequence.data() + sequence.size() ||
      !value)
    throw std::invalid_argument("invalid received media sequence");
}

struct ReceivedMediaStore::Receipt {
  const std::string token;
  // Distinguish image-family capabilities that share a public MediaKind.
  const std::string reference_kind;
  const bot_model::MediaKind kind;
  const std::string url;
  const core::SelfIdentityOrigin origin;
  const std::optional<bot_model::UserRef> observed_user;
  const bot_model::MessageRef source;
  const std::size_t ordinal;
  const std::size_t text_bytes;
  const std::optional<std::uint64_t> declared_size;
  // Query-only provenance: source is the wrapper selector and these are
  // returned array positions, not invented node message IDs or source groups.
  const std::optional<bot_model::MediaMetadata> query_metadata;
  const std::optional<std::size_t> query_node;
  auto eligible(core::SelfIdentityCache &identity) const -> bool {
    if (query_metadata && query_node && is_forward_query_kind(reference_kind))
      return identity.observe_user_and_capture(origin, std::nullopt, [] {})
          .accepted();
    return observed_user &&
           identity.matches_observed_user(origin, *observed_user);
  }
  std::atomic_bool available{true};
};

ReceivedMediaStore::ReceivedMediaStore(
    bot_model::BotInstallationRef installation,
    std::shared_ptr<core::SelfIdentityCache> identity,
    std::weak_ptr<core::BotEventCapability> ingress,
    std::shared_ptr<const QqPlatformAdapter> binding,
    std::size_t maximum_entries, std::size_t maximum_retained_text_bytes)
    : installation_(std::move(installation)), identity_(std::move(identity)),
      ingress_(std::move(ingress)), binding_(std::move(binding)),
      maximum_entries_(maximum_entries),
      maximum_retained_text_bytes_(maximum_retained_text_bytes),
      store_uuid_(fresh_uuid()) {
  installation_.validate();
  if (!identity_ || !binding_ || ingress_.expired() || !maximum_entries_ ||
      !maximum_retained_text_bytes_)
    throw std::invalid_argument("invalid received media store capacity/owners");
}
ReceivedMediaStore::~ReceivedMediaStore() {
  for (const auto &receipt : fifo_)
    receipt->available.store(false, std::memory_order_release);
}
void ReceivedMediaStore::evict_oldest() {
  const auto &receipt = fifo_.front();
  receipt->available.store(false, std::memory_order_release);
  retained_text_bytes_ -= receipt->text_bytes;
  receipts_.erase(receipt->token);
  fifo_.pop_front();
}

auto ReceivedMediaStore::capture(
    std::string_view kind, std::string_view url,
    const core::SelfIdentityOrigin &origin,
    const std::optional<bot_model::UserRef> &observed_user,
    const bot_model::MessageRef &source, std::size_t ordinal,
    std::optional<std::uint64_t> declared_size,
    std::optional<bot_model::MediaMetadata> query_metadata,
    std::optional<std::size_t> query_node) -> std::string {
  const bool query = is_forward_query_kind(kind);
  if (query != query_metadata.has_value() || query != query_node.has_value() ||
      (query && (observed_user || *query_node >= 99)))
    throw std::invalid_argument("invalid queried media provenance");
  std::string token;
  {
    std::lock_guard lock(mutex_);
    if (sequence_ == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("received media sequence exhausted");
    token =
        store_uuid_ + ':' + std::to_string(++sequence_) + ':' + fresh_uuid();
  }
  // Once minted, even an unretained token is burned forever. Never fall back
  // to the filename recipe when the original authority cannot be retained.
  const auto ingress = ingress_.lock();
  const bool authority =
      query ? identity_->observe_user_and_capture(origin, std::nullopt, [] {})
                  .accepted()
            : observed_user && observed_user->installation == installation_ &&
                  identity_->matches_observed_user(origin, *observed_user);
  if (!ingress || !ingress->active() || !authority ||
      source.conversation.installation != installation_ || url.size() > 8192 ||
      !network::is_valid_media_http_url(url))
    return token;

  // Account for current receipt provenance within the existing ceiling.
  std::size_t charge = 0;
  if (query_metadata)
    charge += sizeof(bot_model::MediaMetadata) + sizeof(std::size_t);
  if (charge >= maximum_retained_text_bytes_)
    return token;
  const auto add = [&](std::string_view text) {
    // Charge terminators as well as payload, including both token copies.
    if (text.size() >= maximum_retained_text_bytes_ - charge)
      return false;
    charge += text.size() + 1;
    return true;
  };
  if (!add(token) || !add(token) || !add(kind) || !add(url) ||
      (observed_user && (!add(observed_user->installation.installation_id) ||
                         !add(observed_user->installation.surface.value()) ||
                         !add(observed_user->id.serialized()))) ||
      (query_metadata &&
       ((query_metadata->filename && !add(*query_metadata->filename)) ||
        (query_metadata->mime_type && !add(*query_metadata->mime_type)))) ||
      !add(source.conversation.installation.installation_id) ||
      !add(source.conversation.installation.surface.value()) ||
      !add(source.conversation.id.serialized()) || !add(source.id.serialized()))
    return token;

  // No cache/ingress callback runs under this mutex, including HTTP
  // eligibility.
  std::lock_guard lock(mutex_);
  while (!fifo_.empty() &&
         (fifo_.size() >= maximum_entries_ ||
          charge > maximum_retained_text_bytes_ - retained_text_bytes_))
    evict_oldest();
  auto receipt = std::shared_ptr<Receipt>(
      new Receipt{token, std::string(kind), kind_of(kind), std::string(url),
                  origin, observed_user, source, ordinal, charge, declared_size,
                  std::move(query_metadata), query_node});
  receipts_.emplace(token, receipt);
  try {
    fifo_.push_back(std::move(receipt));
  } catch (...) {
    receipts_.erase(token);
    throw;
  }
  retained_text_bytes_ += charge;
  return token;
}

auto ReceivedMediaStore::retain_forward_media(
    const bot_model::MessageRef &selector, std::size_t node_index,
    std::size_t segment_ordinal, std::string_view native_type,
    std::string_view actual_url, bot_model::MediaMetadata metadata,
    const core::SelfIdentityOrigin &origin) -> bot_model::ReceivedMedia {
  binding_->codec().validate(selector);
  if (selector.conversation.installation != installation_ ||
      segment_ordinal >= bot_model::detail::bot_maximum_items)
    throw std::invalid_argument("queried media selector or position mismatch");
  metadata.validate();
  const auto kind = "queried-forward-" + std::string(native_type);
  if (!is_forward_query_kind(kind))
    throw std::invalid_argument("unsupported queried media kind");
  const auto token =
      capture(kind, actual_url, origin, std::nullopt, selector, segment_ordinal,
              metadata.size_bytes, metadata, node_index);
  return {{installation_,
           binding_->codec().make<bot_model::MediaId>(kind, {token})},
          kind_of(kind),
          std::move(metadata),
          std::nullopt,
          {}};
}

auto ReceivedMediaStore::normalize(
    const QqPlatformAdapter &binding,
    const bot_model::BotInstallationRef &installation,
    const common::MessageEvent &event, bot_model::TimePoint received_at,
    std::string_view observation_id, const core::SelfIdentityOrigin &origin)
    -> bot_model::MessageReceived {
  if (installation != installation_)
    throw std::invalid_argument("received media installation mismatch");
  std::vector<std::size_t> positions;
  auto result = binding.normalize_with_positions(
      installation, event, received_at, observation_id, positions);
  const auto observed_user = binding.observed_self(installation, event);
  for (std::size_t ordinal = 0; ordinal < event.message.size(); ++ordinal) {
    const auto &segment = event.message[ordinal];
    if (segment.type != "image" && segment.type != "record" &&
        segment.type != "video" && segment.type != "mface" &&
        segment.type != "file")
      continue;
    if (ordinal >= positions.size() || positions[ordinal] >= result.body.size())
      continue;
    const auto url = segment.data.find("url");
    if (url == segment.data.end() || !url->is_string())
      continue;
    const auto &text = url->get_ref<const std::string &>();
    if (text.size() > 8192 || !network::is_valid_media_http_url(text))
      continue;
    if (segment.type == "file") {
      // Preserve direct-URL priority even when file_id is missing/malformed.
      // Only this live origin may mint the receipt, never archive restoration.
      bot_model::MediaMetadata metadata;
      try {
        metadata = QqPlatformAdapter::file_metadata(segment.data);
      } catch (const std::invalid_argument &) {
        // The pure normalizer used the same validation and retained this
        // malformed file as UnsupportedContent. Keep its position, not a read.
        continue;
      }
      const auto token =
          capture("received-file", text, origin, observed_user, result.message,
                  ordinal, metadata.size_bytes, std::nullopt, std::nullopt);
      // Capture failure propagates: never fall back to a file-ID query under
      // another authority when the selected live URL recipe cannot be retained.
      result.body[positions[ordinal]] = bot_model::ReceivedMedia{
          {installation,
           binding.codec().make<bot_model::MediaId>("received-file", {token})},
          bot_model::MediaKind::File,
          std::move(metadata),
          std::nullopt,
          {}};
      continue;
    }
    if (segment.type == "mface") {
      const auto *unsupported = std::get_if<bot_model::UnsupportedContent>(
          &result.body[positions[ordinal]]);
      if (!unsupported || unsupported->category != "onebot11.mface")
        continue;
      try {
        bot_model::ReceivedRichText caption;
        if (const auto summary = segment.data.find("summary");
            summary != segment.data.end() && !summary->is_null()) {
          if (!summary->is_string())
            continue;
          const auto &value = summary->get_ref<const std::string &>();
          if (!value.empty()) {
            bot_model::detail::validate_identifier(
                value, "QQ market face summary", 2048);
            bot_model::Text label{value, {}};
            label.validate();
            caption.emplace_back(std::move(label));
          }
        }
        const auto token = capture("received-mface", text, origin,
                                   observed_user, result.message, ordinal,
                                   std::nullopt, std::nullopt, std::nullopt);
        result.body[positions[ordinal]] = bot_model::ReceivedMedia{
            {installation, binding.codec().make<bot_model::MediaId>(
                               "received-mface", {token})},
            bot_model::MediaKind::Image,
            {},
            std::nullopt,
            std::move(caption)};
      } catch (...) {
        // Keep the unsupported projection; never substitute an archive URL or
        // filename capability. The token, if minted, remains burned.
      }
      continue;
    }
    auto *media =
        std::get_if<bot_model::ReceivedMedia>(&result.body[positions[ordinal]]);
    if (!media)
      continue;
    const auto kind = "received-" + segment.type;
    if (media->kind != kind_of(kind))
      continue;
    const auto token =
        capture(kind, text, origin, observed_user, result.message, ordinal,
                std::nullopt, std::nullopt, std::nullopt);
    media->media = {installation,
                    binding.codec().make<bot_model::MediaId>(kind, {token})};
  }
  result.validate();
  return result;
}

auto ReceivedMediaStore::read(const bot_model::MediaRef &media,
                              std::uint64_t maximum_bytes,
                              network::detail::HttpDeadline deadline)
    -> boost::asio::awaitable<Result> {
  std::shared_ptr<Receipt> receipt;
  QqNativeReferenceCodec codec;
  codec.validate(media);
  const auto parts = codec.unpack(media.id);
  if (media.installation == installation_ &&
      is_received_media_kind(parts.kind)) {
    std::lock_guard lock(mutex_);
    const auto found = receipts_.find(parts.components[0]);
    if (found != receipts_.end() &&
        found->second->reference_kind == parts.kind &&
        found->second->kind == kind_of(parts.kind))
      receipt = found->second;
  }
  // Deliberately not a coroutine: no borrowed this/ref survives admission.
  return read_owned(std::move(receipt), identity_, ingress_, maximum_bytes,
                    deadline);
}

auto ReceivedMediaStore::read_owned(
    std::shared_ptr<Receipt> receipt,
    std::shared_ptr<core::SelfIdentityCache> identity,
    std::weak_ptr<core::BotEventCapability> ingress,
    std::uint64_t maximum_bytes, network::detail::HttpDeadline deadline)
    -> boost::asio::awaitable<Result> {
  const auto eligible = [receipt, identity, ingress] {
    const auto fence = ingress.lock();
    return receipt && receipt->available.load(std::memory_order_acquire) &&
           fence && fence->active() && receipt->eligible(*identity) &&
           receipt->available.load(std::memory_order_acquire) &&
           fence->active();
  };
  if (!eligible())
    co_return failure(Code::CacheUnavailable);
  if (!maximum_bytes || maximum_bytes > bot_model::detail::bot_maximum_bytes)
    co_return failure(Code::InvalidRequest);
  if (receipt->declared_size && *receipt->declared_size > maximum_bytes)
    co_return failure(Code::MediaTooLarge);
  try {
    auto response = co_await network::detail::fetch_media_until(
        receipt->url, maximum_bytes, deadline, eligible);
    if (!eligible())
      co_return failure(Code::CacheUnavailable);
    if (response.error == boost::asio::error::message_size)
      co_return failure(Code::MediaTooLarge);
    if (response.error == boost::asio::error::operation_aborted)
      co_return failure(Code::Cancelled);
    if (response.error || !response.response_complete ||
        std::chrono::steady_clock::now() >= deadline)
      co_return failure(Code::TransportFailure);
    if (response.status_code != 200)
      co_return failure(Code::ProviderRejected);
    if (response.body.size() > maximum_bytes)
      co_return failure(Code::MediaTooLarge);
    if (response.body.empty() ||
        (receipt->declared_size &&
         *receipt->declared_size != response.body.size()))
      co_return failure(Code::MalformedResponse);
    bot_model::BytesSource bytes{
        {response.body.begin(), response.body.end()},
        receipt->query_metadata.value_or(bot_model::MediaMetadata{})};
    bytes.metadata.size_bytes = bytes.bytes.size();
    bytes.validate();
    if (!eligible())
      co_return failure(Code::CacheUnavailable);
    if (std::chrono::steady_clock::now() >= deadline)
      co_return failure(Code::TransportFailure);
    co_return Result::success({std::move(bytes), receipt->kind, std::nullopt});
  } catch (const boost::system::system_error &error) {
    if (!eligible())
      co_return failure(Code::CacheUnavailable);
    co_return failure(error.code() == boost::asio::error::operation_aborted
                          ? Code::Cancelled
                      : error.code() == boost::asio::error::message_size
                          ? Code::MediaTooLarge
                          : Code::TransportFailure);
  } catch (...) {
    co_return failure(eligible() ? Code::TransportFailure
                                 : Code::CacheUnavailable);
  }
}
} // namespace obcx::onebot11::bot::detail
