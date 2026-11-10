#include "core/bot/message_operation_binding.hpp"
#include "network/http_client.hpp"
#include "telegram/bot/bot_send.hpp"
#include "telegram/bot/platform_adapter.hpp"
#include "telegram/bot/response_parser.hpp"
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/steady_timer.hpp>
#include <set>

namespace obcx::telegram::bot {
namespace d = obcx::bot;
namespace asio = boost::asio;
void validate_sticker_outcome(const SendStickerRequest &request,
                              const d::SendOutcome &outcome) {
  d::detail::validate_single_message_outcome(request.target, outcome);
}
namespace {
struct State {
  explicit State(const d::ConversationRef &target)
      : evidence(target, d::SendOutcomeEnvelope::for_reply(
                             d::SendOutcomeEnvelope::standalone())) {}
  d::detail::SendEvidenceRecovery evidence;
  std::optional<d::BotOperationError> error;
  bool in_flight{};
  bool unknown{};
  bool complete{};
};
asio::awaitable<void> execute_sticker(
    const SendStickerRequest &r, core::TelegramTransport &transport,
    std::chrono::steady_clock::time_point deadline, State &state,
    core::ConfirmedAuthorEvidenceCollector *authors) {
  try {
    r.validate();
    reference_codec().validate(r.target);
    reference_codec().validate(r.sticker);
    if (r.reply_to)
      reference_codec().validate(*r.reply_to);
    const auto native = reference_codec().unpack(r.sticker.id);
    if (!native.kind.starts_with("sticker-"))
      throw std::invalid_argument("not a sticker reference");
    const auto check = [&] {
      if (std::chrono::steady_clock::now() >= deadline)
        throw std::runtime_error("sticker deadline elapsed");
    };
    check();
    d::Json inspect{{"method", "getFile"}, {"file_id", native.components[0]}};
    auto file = parse_telegram_operation_response(
        co_await transport.send_action(inspect.dump(), 0), false);
    if (!file.ok()) {
      state.error = *file.error;
      co_return;
    }
    const auto bytes = d::detail::bounded_unsigned(
        file.value->at("file_size"), d::detail::bot_maximum_bytes);
    const std::size_t native_bound =
        native.kind == "sticker-static" ? 512U * 1024U
        : native.kind == "sticker-webm" ? 256U * 1024U
                                        : 64U * 1024U;
    if (file.value->at("file_id") != native.components[0] || bytes == 0 ||
        bytes > std::min(r.maximum_bytes, native_bound))
      throw std::invalid_argument("invalid or oversized sticker");
    check();
    const auto target = reference_codec().unpack(r.target.id);
    d::Json params{{"method", "sendSticker"},
                   {"chat_id", target.components[0]},
                   {"sticker", native.components[0]}};
    if (target.kind == "topic")
      params["message_thread_id"] = std::stoll(target.components[1]);
    if (r.reply_to)
      params["reply_parameters"] = {
          {"message_id",
           std::stoll(reference_codec().unpack(r.reply_to->id).components[0])}};
    const std::vector<std::pair<std::string, core::TelegramMediaUpload>>
        no_files;
    state.in_flight = true;
    auto parsed = parse_telegram_operation_response(
        co_await transport.send_bot_request(std::move(params), no_files), true);
    if (!parsed.ok()) {
      state.error = *parsed.error;
      state.unknown = state.error->submission_safety ==
                      d::SubmissionSafety::PossiblySubmitted;
      state.in_flight = false;
      co_return;
    }
    const auto &value = *parsed.value;
    std::vector<const d::Json *> candidates;
    if (value.is_array()) {
      for (const auto &candidate : value)
        candidates.push_back(&candidate);
    } else if (value.is_object())
      candidates.push_back(&value);
    bool complete = value.is_object();
    std::set<std::string> seen;
    for (const auto *candidate : candidates) {
      try {
        auto ref = native_message_reference(r.target.installation, *candidate);
        if (ref.conversation != r.target) {
          complete = false;
          continue;
        }
        const auto collect_author = [&]() noexcept {
          if (!authors)
            return;
          try {
            authors->native_author(
                ref, native_sender(r.target.installation, *candidate),
                reference_codec());
          } catch (...) {
            // Author failure cannot erase the physical checkpoint below.
          }
        };
        if (!seen.insert(ref.id.serialized()).second) {
          collect_author();
          complete = false;
          continue;
        }
        // Checkpoint the physical effect under the complete reply allowance
        // before inspecting optional media. Exhaustion must retain this prefix.
        state.evidence.reference(ref);
        collect_author();
        d::SendOutputDetails detail{ref, std::nullopt, {}};
        try {
          if (auto media = native_media(r.target.installation, *candidate))
            detail.reusable_media.push_back(
                {media->media, media->identity, std::nullopt});
          else
            complete = false;
        } catch (...) {
          complete = false;
        }
        if (!candidate->contains("sticker"))
          complete = false;
        if (!detail.reusable_media.empty() &&
            reference_codec().unpack(detail.reusable_media[0].media.id).kind !=
                native.kind)
          complete = false;
        state.evidence.details(detail);
      } catch (const d::detail::BotBudgetExceeded &) {
        throw;
      } catch (...) {
        complete = false;
      }
    }
    state.in_flight = false;
    if (complete && seen.size() == 1)
      state.complete = true;
    else {
      state.unknown = true;
      state.error = d::detail::unknown_send_error();
    }
  } catch (const network::HttpClientError &e) {
    state.unknown =
        state.in_flight &&
        e.submission_state() !=
            network::HttpRequestSubmissionState::DefinitelyNotSubmitted;
    state.error = d::BotOperationError{
        .code = state.unknown ? d::BotOperationErrorCode::OutcomeUnknown
                              : d::BotOperationErrorCode::TransportFailure,
        .message = "Telegram sticker transport interrupted",
        .submission_safety = state.unknown
                                 ? d::SubmissionSafety::PossiblySubmitted
                                 : d::SubmissionSafety::DefinitelyNotSubmitted};
  } catch (...) {
    state.unknown = state.in_flight;
    state.error = d::BotOperationError{
        .code = state.unknown ? d::BotOperationErrorCode::OutcomeUnknown
                              : d::BotOperationErrorCode::InvalidRequest,
        .message = "Telegram sticker request interrupted or invalid",
        .submission_safety = state.unknown
                                 ? d::SubmissionSafety::PossiblySubmitted
                                 : d::SubmissionSafety::DefinitelyNotSubmitted};
  }
}
} // namespace
d::SendOutcome decode_sticker_outcome(const d::Json &wire,
                                      const SendStickerRequest &r,
                                      const d::SendOutcomeEnvelope &envelope) {
  return d::decode_single_message_outcome(wire, r.target, envelope);
}
d::SendOutcome normalize_sticker_outcome(
    const SendStickerRequest &r, d::SendOutcome value,
    const d::SendOutcomeEnvelope &envelope) {
  return d::normalize_single_message_outcome(r.target, std::move(value),
                                             envelope);
}
asio::awaitable<d::SendOutcome> send_sticker(const SendStickerRequest &r,
                                             core::TelegramTransport &transport,
                                             std::chrono::milliseconds budget) {
  return send_sticker(r, transport, budget, nullptr);
}
asio::awaitable<d::SendOutcome> send_sticker(
    const SendStickerRequest &r, core::TelegramTransport &transport,
    std::chrono::milliseconds budget,
    core::ConfirmedAuthorEvidenceCollector *authors) {
  const auto deadline = std::chrono::steady_clock::now() + budget;
  State state(r.target);
  try {
    asio::steady_timer timer(co_await asio::this_coro::executor);
    timer.expires_at(deadline);
    using asio::experimental::awaitable_operators::operator||;
    co_await (execute_sticker(r, transport, deadline, state, authors) ||
              timer.async_wait(asio::use_awaitable));
  } catch (...) {
    state.unknown |= state.in_flight;
    if (!state.error)
      state.error =
          d::BotOperationError{.code = d::BotOperationErrorCode::Cancelled,
                               .message = "Telegram sticker send cancelled"};
  }
  auto checkpoint = state.evidence.finish();
  auto &known = std::get<d::SendFailure>(checkpoint);
  if (state.complete)
    co_return d::SendReceipt{std::move(known.confirmed_messages),
                             std::move(known.output_details)};
  co_return d::send_failure(
      state.error.value_or(
          d::BotOperationError{.code = d::BotOperationErrorCode::Cancelled,
                               .message = "Telegram sticker deadline expired"}),
      std::move(known.output_details), state.unknown);
}
} // namespace obcx::telegram::bot
