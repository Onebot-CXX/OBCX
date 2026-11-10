#include "telegram/bot/bot_send.hpp"
#include "core/bot/bounded_file_reader.hpp"
#include "core/bot/message_operation_binding.hpp"
#include "network/http_client.hpp"
#include "network/image_fetch.hpp"
#include "telegram/bot/platform_adapter.hpp"
#include "telegram/bot/response_parser.hpp"
#include <algorithm>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <fmt/format.h>
#include <set>

namespace obcx::telegram::bot {
namespace d = obcx::bot;
namespace asio = boost::asio;
namespace {
// Fixed prepared-media payload ceiling, not a configuration default. This
// bounds multipart/HTTP copy amplification; the wire ceiling is independent.
constexpr std::size_t media_bound = 16U * 1024U * 1024U;
class PreflightError : public std::invalid_argument {
public:
  PreflightError(d::BotOperationErrorCode code, const char *message)
      : std::invalid_argument(message), code(code) {}
  d::BotOperationErrorCode code;
};
std::size_t item_bound(std::string_view kind) {
  return kind == "photo" ? 10U * 1024U * 1024U : media_bound;
}
void photo_dimensions(const d::MediaMetadata &metadata) {
  if ((metadata.width && *metadata.width >= 10000) ||
      (metadata.height && *metadata.height >= 10000))
    throw PreflightError(d::BotOperationErrorCode::UnsupportedAction,
                         "Telegram photo dimensions exceed native bounds");
  if (metadata.width && metadata.height) {
    const auto w = std::uint64_t{*metadata.width},
               h = std::uint64_t{*metadata.height};
    if (w + h > 10000 || w > 20 * h || h > 20 * w)
      throw PreflightError(d::BotOperationErrorCode::UnsupportedAction,
                           "Telegram photo shape exceeds native bounds");
  }
}
struct Step {
  d::Json parameters;
  std::vector<std::pair<std::string, core::TelegramMediaUpload>> files;
  std::vector<std::size_t> blocks;
};
struct MediaAcquisition {
  std::size_t step;
  std::size_t file;
  // The request outlives the joined preparation coroutine; never detached.
  std::variant<const d::HttpSource *, const d::LocalFile *> source;
};
struct Plan {
  std::vector<Step> steps;
  std::vector<d::MediaRef> references;
  std::vector<MediaAcquisition> acquisitions;
  // Actual prepared bytes plus native sizes and pending declared source sizes.
  // An absent source size reserves nothing, but is measured before any send.
  std::size_t bytes{};
};
std::string media_type(const d::MessageBlock &b) {
  if (std::holds_alternative<d::Image>(b))
    return "photo";
  if (std::holds_alternative<d::Animation>(b))
    return "animation";
  if (std::holds_alternative<d::Video>(b))
    return "video";
  if (auto a = std::get_if<d::Audio>(&b))
    return a->kind == d::AudioKind::Voice ? "voice" : "audio";
  return "document";
}
void validate_upload_data(std::string_view data) {
  // Encoding support is decided by Telegram, not a local MIME/magic allowlist.
  // Keep transport safety and resource checks independent of file format.
  if (data.empty())
    throw std::invalid_argument("empty Telegram media");
  if (data.contains("----OBCXBoundary7MA4YWxk"))
    throw std::invalid_argument("multipart delimiter collision");
}
Plan plan(const d::SendMessageRequest &r, bool upload, bool file_read) {
  d::binding::validate_native_request(reference_codec(), r);
  if (r.body.size() > r.limits.maximum_messages ||
      (r.delivery == d::DeliveryPolicy::SingleMessageOnly &&
       r.body.size() != 1))
    throw std::invalid_argument(
        "Telegram body requires more physical messages than permitted");
  auto target = reference_codec().unpack(r.target.id);
  Plan p;
  for (std::size_t index = 0; index < r.body.size();) {
    Step step{.parameters = {{"chat_id", target.components[0]}}};
    if (target.kind == "topic")
      step.parameters["message_thread_id"] = std::stoll(target.components[1]);
    if (index == 0 && r.reply_to)
      step.parameters["reply_parameters"] = {
          {"message_id",
           std::stoll(reference_codec().unpack(r.reply_to->id).components[0])}};
    if (const auto *text = std::get_if<d::RichText>(&r.body[index])) {
      auto t = native_text(*text, 4096);
      if (t.text.empty())
        throw std::invalid_argument("empty Telegram text");
      step.parameters["method"] = "sendMessage";
      step.parameters["text"] = std::move(t.text);
      step.parameters["entities"] = std::move(t.entities);
      step.blocks.push_back(index++);
    } else {
      std::size_t count = 1;
      if (std::holds_alternative<d::Image>(r.body[index]) &&
          r.delivery == d::DeliveryPolicy::AllowSplit &&
          !(index == 0 && r.reply_to)) {
        std::size_t run = 0;
        while (index + run < r.body.size() &&
               std::holds_alternative<d::Image>(r.body[index + run]))
          ++run;
        count = std::min<std::size_t>(10, run);
        if (run > 10 && run - count == 1)
          --count;
      }
      d::Json album = d::Json::array();
      for (std::size_t n = 0; n < count; ++n) {
        const auto bi = index + n;
        const auto &block = r.body[bi];
        const auto type = media_type(block);
        d::Json item{{"type", type}};
        std::visit(
            [&](const auto &b) {
              using B = std::decay_t<decltype(b)>;
              if constexpr (!std::same_as<B, d::RichText>) {
                auto caption = native_text(b.caption, 1024);
                item["caption"] = caption.text;
                item["caption_entities"] = caption.entities;
                std::visit(
                    [&](const auto &source) {
                      using S = std::decay_t<decltype(source)>;
                      if constexpr (std::same_as<S, d::MediaRef>) {
                        const auto parts = reference_codec().unpack(source.id);
                        if (type == "document")
                          throw PreflightError(
                              d::BotOperationErrorCode::UnsupportedAction,
                              "Telegram document reference cannot guarantee "
                              "requested filename; use owned bytes");
                        if (parts.kind != type)
                          throw PreflightError(
                              d::BotOperationErrorCode::UnsupportedAction,
                              "Telegram media reference format cannot be "
                              "reinterpreted");
                        item["media"] = parts.components[0];
                        p.references.push_back(source);
                      } else if constexpr (std::same_as<S, d::BytesSource> ||
                                           std::same_as<S, d::HttpSource> ||
                                           std::same_as<S, d::LocalFile>) {
                        if (!upload)
                          throw PreflightError(
                              d::BotOperationErrorCode::UnsupportedAction,
                              "Telegram upload component unavailable");
                        if constexpr (std::same_as<S, d::LocalFile>) {
                          if (!file_read)
                            throw PreflightError(
                                d::BotOperationErrorCode::UnsupportedAction,
                                "Telegram LocalFile reader unavailable");
                        }
                        if (type == "photo")
                          photo_dimensions(source.metadata);
                        const auto size =
                            [&]() -> std::optional<std::uint64_t> {
                          if constexpr (std::same_as<S, d::BytesSource>)
                            return source.bytes.size();
                          else
                            return source.metadata.size_bytes;
                        }();
                        if (size) {
                          if (*size == 0)
                            throw std::invalid_argument("empty Telegram media");
                          if (*size >
                                  std::min(r.limits.maximum_media_item_bytes,
                                           item_bound(type)) ||
                              *size >
                                  std::min(r.limits.maximum_media_total_bytes,
                                           media_bound) -
                                      p.bytes)
                            throw PreflightError(
                                d::BotOperationErrorCode::MediaTooLarge,
                                "Telegram preparation byte budget exceeded");
                          p.bytes += *size;
                        }
                        auto filename =
                            source.metadata.filename.value_or("media.bin");
                        if constexpr (std::same_as<B, d::File>)
                          filename = b.filename;
                        auto mime = source.metadata.mime_type.value_or(
                            "application/octet-stream");
                        if (filename.find_first_of("/\\\r\n\"") !=
                                std::string::npos ||
                            mime.find_first_of("\r\n") != std::string::npos)
                          throw std::invalid_argument(
                              "unsafe Telegram multipart metadata");
                        std::string data;
                        if constexpr (std::same_as<S, d::BytesSource>) {
                          const std::string_view bytes{
                              reinterpret_cast<const char *>(
                                  source.bytes.data()),
                              source.bytes.size()};
                          validate_upload_data(bytes);
                          data.assign(bytes);
                        } else {
                          p.acquisitions.push_back(
                              {p.steps.size(), step.files.size(), &source});
                        }
                        auto field = "file" + std::to_string(n);
                        item["media"] = "attach://" + field;
                        step.files.emplace_back(
                            field, core::TelegramMediaUpload{
                                       type, filename, mime, std::move(data)});
                      }
                    },
                    b.source);
              }
            },
            block);
        if (count == 1) {
          const std::map<std::string, std::string> methods{
              {"photo", "sendPhoto"}, {"animation", "sendAnimation"},
              {"video", "sendVideo"}, {"audio", "sendAudio"},
              {"voice", "sendVoice"}, {"document", "sendDocument"}};
          step.parameters["method"] = methods.at(type);
          step.parameters[type] = item.at("media");
          step.parameters["caption"] = item.at("caption");
          step.parameters["caption_entities"] = item.at("caption_entities");
        } else
          album.push_back(std::move(item));
        step.blocks.push_back(bi);
      }
      if (count > 1) {
        step.parameters["method"] = "sendMediaGroup";
        step.parameters["media"] = std::move(album);
      }
      index += count;
    }
    if (!step.files.empty() &&
        step.parameters.dump().contains("----OBCXBoundary7MA4YWxk"))
      throw std::invalid_argument("multipart field delimiter collision");
    p.steps.push_back(std::move(step));
  }
  return p;
}
// Telegram normalizes tabs to spaces and trims trailing spaces/newlines in
// message text and captions. Accept those native representations without
// changing the submitted body or mistaking a confirmed send for a failed one.
// All other bytes, including internal newlines and UTF-8 content, must match.
bool matches_sent_text(std::string_view returned, std::string_view submitted) {
  if (returned == submitted)
    return true;
  const auto matches = [&](std::string_view expected) {
    return std::ranges::equal(expected, returned, [](char source, char actual) {
      return actual == (source == '\t' ? ' ' : source);
    });
  };
  if (matches(submitted))
    return true;
  while (!submitted.empty() &&
         (submitted.back() == ' ' || submitted.back() == '\t' ||
          submitted.back() == '\n'))
    submitted.remove_suffix(1);
  return matches(submitted);
}
struct Execution {
  Execution() {
    evidence.inspect(d::GatewayCodec<d::SendOutcome>::encode(
        d::send_failure(d::detail::unknown_send_error(), {}, true)));
  }
  d::detail::BotWireBudget evidence;
  d::detail::BotWireBudget recovery_work;
  std::set<std::string> seen;
  std::vector<d::SendOutputDetails> confirmed;
  std::optional<d::BotOperationError> error;
  bool in_flight{};
  bool unknown{};
  bool complete{};
};
d::BotOperationError failure(d::BotOperationErrorCode code, std::string message,
                             bool possible = false) {
  return {.code = code,
          .message = std::move(message),
          .submission_safety =
              possible ? d::SubmissionSafety::PossiblySubmitted
                       : d::SubmissionSafety::DefinitelyNotSubmitted};
}
// Only direct result messages are candidates. Commit scoped identity before
// inspecting optional media, so malformed metadata cannot erase a confirmation.
// Native validators check borrowed fields before constructing references.
bool retain_send_evidence(const d::ConversationRef &target,
                          const d::Json &candidate, Execution &state,
                          core::ConfirmedAuthorEvidenceCollector *authors) {
  state.recovery_work.nodes();
  try {
    if (candidate.contains("is_topic_message") &&
        !candidate.at("is_topic_message").is_boolean())
      return false;
    auto message = native_message_reference(target.installation, candidate);
    if (message.conversation != target)
      return false;
    const auto collect_author = [&]() noexcept {
      if (!authors)
        return;
      try {
        authors->native_author(message,
                               native_sender(target.installation, candidate),
                               reference_codec());
      } catch (...) {
        // A malformed selected sender_chat is unknown, not from/self fallback.
      }
    };
    if (state.seen.contains(message.id.serialized())) {
      collect_author(); // Still compare duplicate native authors, not just IDs.
      return false;
    }
    d::SendOutputDetails detail{message, std::nullopt, {}};
    auto charge = state.evidence;
    charge.inspect(d::Json(message));
    charge.inspect(d::detail::bot_encode(detail));
    charge.nodes(10);
    state.confirmed.push_back(std::move(detail));
    state.evidence = charge;
    collect_author();
    state.seen.insert(message.id.serialized());

    // Match native_media's selected field, bounding borrowed work before it
    // constructs owned metadata. Captions and unrelated fields are not copied.
    for (const auto key : {"photo", "animation", "video", "video_note", "audio",
                           "voice", "document", "sticker"})
      if (candidate.contains(key)) {
        state.recovery_work.inspect(candidate.at(key));
        break;
      }
    if (auto media = native_media(target.installation, candidate)) {
      d::PublishedMedia published{media->media, media->identity, std::nullopt};
      charge = state.evidence;
      charge.inspect(d::detail::bot_encode(published));
      state.confirmed.back().reusable_media.push_back(std::move(published));
      state.evidence = charge;
    }
    return true;
  } catch (const d::detail::BotBudgetExceeded &) {
    throw;
  } catch (const std::exception &) {
    return false;
  }
}
// Send-only preflight: the shared operation parser still decides status, but a
// contradictory or malformed rejection must not claim definite non-submission.
// Check borrowed bounds before that parser copies a result or diagnostic.
void validate_send_envelope(const d::Json &document) {
  d::detail::validate_bot_wire(document);
  if (!document.is_object() || !document.contains("ok") ||
      !document.at("ok").is_boolean())
    return;
  const bool ok = document.at("ok").get<bool>();
  if ((!ok && document.contains("result")) ||
      (ok && document.contains("error_code")))
    throw std::invalid_argument("contradictory Telegram send status");
  if (ok)
    return;
  if (document.contains("error_code") &&
      !d::detail::bounded_unsigned(document.at("error_code"), INT64_MAX))
    throw std::invalid_argument("invalid Telegram error code");
  for (const auto key : {"description", "message"})
    if (document.contains(key))
      (void)d::detail::require_identifier(document, key, "Telegram error",
                                          2048);
  if (document.contains("parameters")) {
    const auto &parameters = document.at("parameters");
    d::detail::require_object(parameters, "Telegram response parameters");
    if (parameters.contains("retry_after"))
      (void)d::detail::bounded_unsigned(parameters.at("retry_after"),
                                        INT64_MAX / 1000);
  }
}
asio::awaitable<void> run(const d::SendMessageRequest &r,
                          core::TelegramTransport &transport,
                          core::TelegramMediaUploader *uploader,
                          std::shared_ptr<core::BoundedFileReader> file_reader,
                          std::chrono::steady_clock::time_point deadline,
                          Execution &state,
                          core::ConfirmedAuthorEvidenceCollector *authors) {
  try {
    auto p = plan(r, uploader != nullptr, file_reader != nullptr);
    auto check = [&] {
      if (std::chrono::steady_clock::now() >= deadline)
        throw std::runtime_error("logical send deadline exhausted");
    };
    check();
    // Native reuse still consumes the caller's media budget. Unknown sizes fail
    // closed during preparation, before any message-producing request.
    for (const auto &ref : p.references) {
      check();
      auto parts = reference_codec().unpack(ref.id);
      d::Json params{{"method", "getFile"}, {"file_id", parts.components[0]}};
      auto parsed = parse_telegram_operation_response(
          co_await transport.send_action(params.dump(), 0), false);
      if (!parsed.ok()) {
        state.error = *parsed.error;
        co_return;
      }
      const auto &file = parsed.value.value();
      if (file.at("file_id") != parts.components[0])
        throw std::invalid_argument("Telegram getFile identity mismatch");
      auto bytes =
          d::detail::bounded_unsigned(file.at("file_size"), UINT64_MAX);
      if (bytes == 0 ||
          bytes > std::min(r.limits.maximum_media_item_bytes,
                           item_bound(parts.kind)) ||
          bytes > std::min(media_bound, r.limits.maximum_media_total_bytes) -
                      p.bytes)
        throw PreflightError(d::BotOperationErrorCode::MediaTooLarge,
                             "Telegram reference exceeds media budget");
      p.bytes += bytes;
    }
    // All structural/metadata planning and native reference sizes precede
    // acquisition. Every acquisition completes before any message-producing
    // I/O.
    for (const auto &pending : p.acquisitions) {
      check();
      auto cancellation = co_await asio::this_coro::cancellation_state;
      if (cancellation.cancelled() != asio::cancellation_type::none)
        throw std::runtime_error("send cancelled");
      const auto &metadata = std::visit(
          [](const auto *source) -> const d::MediaMetadata & {
            return source->metadata;
          },
          pending.source);
      auto &file = p.steps[pending.step].files[pending.file].second;
      // Replace the declaration's reservation with measured bytes, never with
      // an assumed zero or an overwritten metadata declaration.
      auto retained_bytes = p.bytes;
      if (metadata.size_bytes)
        retained_bytes -= *metadata.size_bytes;
      const auto maximum = std::min(
          std::min(r.limits.maximum_media_item_bytes, item_bound(file.type)),
          std::min(r.limits.maximum_media_total_bytes, media_bound) -
              retained_bytes);
      if (!maximum)
        throw PreflightError(d::BotOperationErrorCode::MediaTooLarge,
                             "Telegram preparation byte budget exhausted");
      std::string data;
      if (const auto *http =
              std::get_if<const d::HttpSource *>(&pending.source)) {
        const auto &source = **http;
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0)
          throw std::runtime_error("logical send deadline exhausted");
        network::ImageFetchRequest fetch{source.url, remaining, maximum,
                                         std::nullopt};
        auto response = co_await network::fetch_image(std::move(fetch));
        check();
        // No Range was requested. Partial, empty and redirect responses are not
        // complete media; the isolated transport does not follow redirects.
        if (response.status_code != 200)
          throw PreflightError(d::BotOperationErrorCode::TransportFailure,
                               "Telegram HTTP media acquisition did not return "
                               "a complete successful response");
        if (response.body.size() > maximum)
          throw PreflightError(
              d::BotOperationErrorCode::MediaTooLarge,
              "Telegram HTTP media exceeds preparation budget");
        if (metadata.size_bytes && *metadata.size_bytes != response.body.size())
          throw std::invalid_argument(
              "Telegram HTTP media size declaration mismatch");
        data = std::move(response.body);
      } else {
        const auto &source = *std::get<const d::LocalFile *>(pending.source);
        core::FileReadRequest read{source.path, maximum, deadline};
        data = co_await file_reader->read(std::move(read));
        check();
        if (data.size() > maximum)
          throw PreflightError(
              d::BotOperationErrorCode::MediaTooLarge,
              "Telegram local media exceeds preparation budget");
        if (metadata.size_bytes && *metadata.size_bytes != data.size())
          throw std::invalid_argument(
              "Telegram local media size declaration mismatch");
      }
      validate_upload_data(data);
      p.bytes = retained_bytes + data.size();
      file.data = std::move(data);
    }
    for (auto &step : p.steps) {
      check();
      auto cancellation = co_await asio::this_coro::cancellation_state;
      if (cancellation.cancelled() != asio::cancellation_type::none)
        throw std::runtime_error("send cancelled");
      state.in_flight = true;
      const auto method = step.parameters.value("method", std::string{});
      auto response = step.files.empty()
                          ? co_await transport.send_bot_request(
                                std::move(step.parameters), step.files)
                          : co_await uploader->upload(
                                std::move(step.parameters), step.files);
      if (response.size() > d::detail::bot_maximum_bytes)
        throw d::detail::BotBudgetExceeded{};
      const auto document = d::Json::parse(response, nullptr, false);
      const auto *value = document.is_object() && document.contains("result")
                              ? &document.at("result")
                              : nullptr;
      bool full = value && (step.blocks.size() == 1
                                ? value->is_object()
                                : value->is_array() &&
                                      value->size() == step.blocks.size());
      std::string mismatch;
      if (!value)
        mismatch = "the response has no result";
      else if (!full)
        mismatch =
            fmt::format("expected {} message(s), the response contains {}",
                        step.blocks.size(),
                        value->is_array() ? value->size() : std::size_t{1});
      const auto begin = state.confirmed.size();
      // Recover only the direct result object/array, never nested replies,
      // updates or arbitrary document identities. No candidate-list copy.
      if (value && value->is_array()) {
        for (const auto &candidate : *value)
          full =
              retain_send_evidence(r.target, candidate, state, authors) && full;
      } else if (value && value->is_object()) {
        full = retain_send_evidence(r.target, *value, state, authors) && full;
      }
      validate_send_envelope(document);
      auto parsed = parse_telegram_operation_response(response, true);
      if (!parsed.ok()) {
        state.error = *parsed.error;
        state.unknown = state.error->submission_safety ==
                        d::SubmissionSafety::PossiblySubmitted;
        state.in_flight = false;
        co_return;
      }
      if (mismatch.empty() &&
          state.confirmed.size() - begin != step.blocks.size())
        mismatch = fmt::format(
            "only {} of {} returned message(s) could be matched to the target "
            "chat",
            state.confirmed.size() - begin, step.blocks.size());
      if (full && state.confirmed.size() - begin == step.blocks.size()) {
        for (std::size_t n = 0; n < step.blocks.size(); ++n) {
          const auto &block = r.body[step.blocks[n]];
          const auto &native = value->is_array() ? value->at(n) : *value;
          const auto mismatched = [&](std::string reason) {
            full = false;
            if (mismatch.empty())
              mismatch = fmt::format("message {} {}", n + 1, reason);
          };
          std::visit(
              [&](const auto &input) {
                using B = std::decay_t<decltype(input)>;
                if constexpr (std::same_as<B, d::RichText>) {
                  if (!native.contains("text") ||
                      !native.at("text").is_string())
                    mismatched("has no text");
                  else if (!matches_sent_text(
                               native.at("text").get_ref<const std::string &>(),
                               native_text(input, 4096).text))
                    mismatched("text differs from what was sent");
                } else {
                  const auto kind = media_type(block);
                  if (!native.contains(kind))
                    mismatched(fmt::format("was sent as {} but Telegram "
                                           "returned a different type",
                                           kind));
                  else if (!matches_sent_text(
                               native.value("caption", std::string{}),
                               native_text(input.caption, 1024).text))
                    mismatched("caption differs from what was sent");
                  else if (state.confirmed[begin + n].reusable_media.empty())
                    mismatched(fmt::format("has no reusable {} file", kind));
                }
              },
              block);
        }
      }
      if (full && state.confirmed.size() - begin == step.blocks.size()) {
        // A complete ordered Bot API batch establishes positional association;
        // an incomplete/malformed batch never does.
        for (std::size_t n = 0; n < step.blocks.size(); ++n) {
          auto &detail = state.confirmed[begin + n];
          detail.content =
              std::vector<d::ContentSlice>{{step.blocks[n], std::nullopt}};
          for (auto &media : detail.reusable_media)
            media.block_index = step.blocks[n];
        }
        state.in_flight = false;
      } else {
        state.error = failure(
            d::BotOperationErrorCode::MalformedResponse,
            fmt::format("Telegram {} response does not match what was sent: {}",
                        method.empty() ? "send" : method,
                        mismatch.empty() ? "unrecognized response" : mismatch),
            true);
        state.unknown = true;
        state.in_flight = false;
        co_return;
      }
    }
    state.complete = true;
  } catch (const core::FileReadError &error) {
    const auto code = [&] {
      switch (error.failure()) {
      case core::FileReadFailure::InvalidRequest:
        return d::BotOperationErrorCode::InvalidRequest;
      case core::FileReadFailure::OverLimit:
        return d::BotOperationErrorCode::MediaTooLarge;
      case core::FileReadFailure::Timeout:
        return d::BotOperationErrorCode::Cancelled;
      case core::FileReadFailure::Unavailable:
        return d::BotOperationErrorCode::TransportFailure;
      }
      return d::BotOperationErrorCode::TransportFailure;
    }();
    state.unknown = state.in_flight;
    state.error =
        failure(state.unknown ? d::BotOperationErrorCode::OutcomeUnknown : code,
                "Telegram local media acquisition failed", state.unknown);
  } catch (const network::ImageFetchError &error) {
    const auto code = [&] {
      switch (error.failure()) {
      case network::ImageFetchFailure::InvalidUrl:
      case network::ImageFetchFailure::InvalidLimits:
        return d::BotOperationErrorCode::InvalidRequest;
      case network::ImageFetchFailure::OverLimit:
        return d::BotOperationErrorCode::MediaTooLarge;
      case network::ImageFetchFailure::Timeout:
        return d::BotOperationErrorCode::Cancelled;
      case network::ImageFetchFailure::Transport:
        return d::BotOperationErrorCode::TransportFailure;
      }
      return d::BotOperationErrorCode::TransportFailure;
    }();
    state.unknown = state.in_flight;
    state.error =
        failure(state.unknown ? d::BotOperationErrorCode::OutcomeUnknown : code,
                "Telegram HTTP media acquisition failed", state.unknown);
  } catch (const network::HttpClientError &e) {
    state.unknown =
        state.in_flight &&
        e.submission_state() !=
            network::HttpRequestSubmissionState::DefinitelyNotSubmitted;
    const bool interrupted =
        e.kind() == network::HttpClientErrorKind::Cancelled ||
        e.kind() == network::HttpClientErrorKind::Timeout;
    state.error =
        failure(state.unknown ? d::BotOperationErrorCode::OutcomeUnknown
                : interrupted ? d::BotOperationErrorCode::Cancelled
                              : d::BotOperationErrorCode::TransportFailure,
                "Telegram transport did not complete", state.unknown);
  } catch (const PreflightError &error) {
    state.unknown = state.in_flight;
    state.error = failure(
        state.unknown ? d::BotOperationErrorCode::OutcomeUnknown : error.code,
        error.what(), state.unknown);
  } catch (const std::invalid_argument &) {
    state.unknown = state.in_flight;
    state.error =
        failure(state.unknown ? d::BotOperationErrorCode::OutcomeUnknown
                              : d::BotOperationErrorCode::InvalidRequest,
                "Telegram content, source, scope or budget is unsupported",
                state.unknown);
  } catch (...) {
    state.unknown = state.in_flight;
    state.error =
        failure(state.unknown ? d::BotOperationErrorCode::OutcomeUnknown
                              : d::BotOperationErrorCode::Cancelled,
                "Telegram send interrupted", state.unknown);
  }
}
} // namespace
NativeText native_text(const d::RichText &rich, std::size_t maximum_units) {
  NativeText out{"", d::Json::array()};
  std::size_t offset = 0;
  for (const auto &part : rich) {
    if (const auto *text = std::get_if<d::Text>(&part)) {
      auto size = core::telegram_utf16_code_units(text->content);
      if (std::ranges::find(text->styles, d::TextStyle::Code) !=
              text->styles.end() &&
          text->styles.size() > 1)
        throw std::invalid_argument(
            "Telegram code cannot overlap other styles");
      for (auto style : text->styles)
        if (size)
          out.entities.push_back(
              {{"type", style == d::TextStyle::Bold     ? "bold"
                        : style == d::TextStyle::Italic ? "italic"
                                                        : "code"},
               {"offset", offset},
               {"length", size}});
      out.text += text->content;
      offset += size;
    } else {
      auto &mention = std::get<d::Mention>(part);
      auto user = reference_codec().unpack(mention.user.id).components[0];
      // Text-link mentions do not require a fabricated provider User profile.
      std::string label = "@" + user;
      out.entities.push_back({{"type", "text_link"},
                              {"offset", offset},
                              {"length", label.size()},
                              {"url", "tg://user?id=" + user}});
      out.text += label;
      offset += label.size();
    }
    if (offset > maximum_units)
      throw std::invalid_argument("Telegram text exceeds native limit");
  }
  return out;
}
NativeText native_formatted_text(const FormattedText &rich,
                                 std::size_t maximum_units) {
  NativeText out{"", d::Json::array()};
  std::size_t offset = 0;
  for (const auto &part : rich) {
    std::visit(
        [&](const auto &value) {
          using V = std::decay_t<decltype(value)>;
          value.validate();
          if constexpr (std::same_as<V, d::Mention>) {
            reference_codec().validate(value.user);
            const auto id =
                reference_codec().unpack(value.user.id).components[0];
            const std::string label = "@" + id;
            if (label.size() > maximum_units - offset)
              throw std::invalid_argument("Telegram text exceeds native limit");
            out.entities.push_back({{"type", "text_link"},
                                    {"offset", offset},
                                    {"length", label.size()},
                                    {"url", "tg://user?id=" + id}});
            out.text += label;
            offset += label.size();
          } else {
            const auto units = core::telegram_utf16_code_units(value.content);
            // Check the whole span before copying it or constructing its
            // entities.
            if (units > maximum_units - offset)
              throw std::invalid_argument("Telegram text exceeds native limit");
            if constexpr (std::same_as<V, StrikethroughText>) {
              out.entities.push_back({{"type", "strikethrough"},
                                      {"offset", offset},
                                      {"length", units}});
            } else {
              if (std::ranges::find(value.styles, d::TextStyle::Code) !=
                      value.styles.end() &&
                  value.styles.size() > 1)
                throw std::invalid_argument(
                    "Telegram code cannot overlap other styles");
              for (const auto style : value.styles)
                out.entities.push_back(
                    {{"type", style == d::TextStyle::Bold     ? "bold"
                              : style == d::TextStyle::Italic ? "italic"
                                                              : "code"},
                     {"offset", offset},
                     {"length", units}});
            }
            out.text += value.content;
            offset += units;
          }
        },
        part);
  }
  if (out.text.empty())
    throw std::invalid_argument("Telegram edit text is empty");
  return out;
}
d::ConversationCapabilities bot_capabilities(const d::ConversationRef &c,
                                             bool upload) {
  return bot_capabilities(c, upload, false);
}
d::ConversationCapabilities bot_capabilities(const d::ConversationRef &c,
                                             bool upload, bool file_read) {
  d::ConversationCapabilities out{
      .conversation = c,
      .operations = {d::CommonOperation::Send, d::CommonOperation::Delete,
                     d::CommonOperation::GetSelf, d::CommonOperation::GetMember,
                     d::CommonOperation::ResolveReply,
                     d::CommonOperation::ReadMedia,
                     d::CommonOperation::Capabilities},
      .text_styles = {d::TextStyle::Bold, d::TextStyle::Italic,
                      d::TextStyle::Code}};
  for (auto kind :
       {d::MediaKind::Image, d::MediaKind::Animation, d::MediaKind::Video,
        d::MediaKind::Audio, d::MediaKind::Voice, d::MediaKind::File}) {
    auto type = kind == d::MediaKind::Image       ? "photo"
                : kind == d::MediaKind::Animation ? "animation"
                : kind == d::MediaKind::Video     ? "video"
                : kind == d::MediaKind::Audio     ? "audio"
                : kind == d::MediaKind::Voice     ? "voice"
                                                  : "document";
    // Acquired HTTP/local bytes use the same bounded upload combinations.
    for (auto source :
         {d::MediaSourceKind::Reference, d::MediaSourceKind::Bytes,
          d::MediaSourceKind::Http, d::MediaSourceKind::LocalFile}) {
      if (source != d::MediaSourceKind::Reference && !upload)
        continue;
      if (source == d::MediaSourceKind::LocalFile && !file_read)
        continue;
      if (source == d::MediaSourceKind::Reference && kind == d::MediaKind::File)
        continue;
      // No SDK-imposed encoding list; provider acceptance remains dynamic.
      out.media.push_back({kind, source, std::nullopt, true, item_bound(type),
                           std::nullopt, std::nullopt});
    }
  }
  const std::vector<d::MediaKind> kinds{
      d::MediaKind::Image, d::MediaKind::Animation, d::MediaKind::Video,
      d::MediaKind::Audio, d::MediaKind::Voice,     d::MediaKind::File};
  out.combinations.push_back({d::DeliveryPolicy::SingleMessageOnly, kinds, true,
                              true, false, 1, std::nullopt, std::nullopt});
  out.combinations.push_back({d::DeliveryPolicy::AllowSplit, kinds, true, true,
                              true, std::nullopt, std::nullopt, std::nullopt});
  out.combinations.push_back({d::DeliveryPolicy::AllowSplit,
                              {d::MediaKind::Image},
                              false,
                              true,
                              false,
                              std::nullopt,
                              2,
                              10});
  // Conservative UTF-8 byte guarantees; actual preflight uses UTF-16 units.
  out.maximum_text_utf8_bytes = 4096;
  out.maximum_caption_utf8_bytes = 1024;
  out.maximum_media_total_bytes = media_bound;
  return out;
}
asio::awaitable<d::SendOutcome> send_bot_message(
    const d::SendMessageRequest &r, core::TelegramTransport &transport,
    core::TelegramMediaUploader *uploader, std::chrono::milliseconds budget) {
  return send_bot_message(r, transport, uploader, nullptr, budget);
}
asio::awaitable<d::SendOutcome> send_bot_message(
    const d::SendMessageRequest &r, core::TelegramTransport &transport,
    core::TelegramMediaUploader *uploader,
    std::shared_ptr<core::BoundedFileReader> file_reader,
    std::chrono::milliseconds budget) {
  return send_bot_message(r, transport, uploader, std::move(file_reader),
                          budget, nullptr);
}
asio::awaitable<d::SendOutcome> send_bot_message(
    const d::SendMessageRequest &r, core::TelegramTransport &transport,
    core::TelegramMediaUploader *uploader,
    std::shared_ptr<core::BoundedFileReader> file_reader,
    std::chrono::milliseconds budget,
    core::ConfirmedAuthorEvidenceCollector *authors) {
  const auto deadline = std::chrono::steady_clock::now() + budget;
  Execution state;
  try {
    asio::steady_timer timer(co_await asio::this_coro::executor);
    timer.expires_at(deadline);
    // parallel_group's OR cancels AND JOINS its losing operation. The sender
    // writes durable local state before completion; timeout cannot discard it.
    using asio::experimental::awaitable_operators::operator||;
    co_await (run(r, transport, uploader, std::move(file_reader), deadline,
                  state, authors) ||
              timer.async_wait(asio::use_awaitable));
  } catch (...) {
    if (!state.error)
      state.error = failure(d::BotOperationErrorCode::Cancelled,
                            "Telegram logical send cancelled", state.in_flight);
    state.unknown |= state.in_flight;
  }
  if (state.complete) {
    d::SendReceipt receipt;
    receipt.output_details = std::move(state.confirmed);
    for (const auto &v : receipt.output_details)
      receipt.messages.push_back(v.message);
    co_return d::SendOutcome{std::move(receipt)};
  }
  co_return d::send_failure(
      state.error.value_or(failure(d::BotOperationErrorCode::Cancelled,
                                   "Telegram logical send expired",
                                   state.unknown)),
      std::move(state.confirmed), state.unknown);
}
} // namespace obcx::telegram::bot
