#include "core/bot/operation_handler.hpp"
#include "core/bot/operation_response_helpers.hpp"
#include "network/http_call.hpp"
#include "telegram/bot/capability_ids.hpp"
#include "telegram/bot/diagnostics.hpp"
#include "telegram/bot/operation_component.hpp"
#include "telegram/bot/operation_definitions.hpp"
#include "telegram/bot/response_parser.hpp"
#include "telegram/bot/transport.hpp"
#include "telegram/network/detail/media_read.hpp"
#include <boost/asio/this_coro.hpp>

#include "core/bot/reply_evidence_store.hpp"
#include "telegram/bot/domain_binding.hpp"
#include "telegram/bot/domain_send.hpp"
#include <atomic>
#include <type_traits>

namespace obcx::core {
namespace {
using obcx::telegram::bot::parse_telegram_operation_response;
using operation_detail::malformed_read;
using operation_detail::malformed_side_effect;
using operation_detail::optional_string;
using operation_detail::parsed_value;
using operation_detail::provider_failure;
using operation_detail::provider_id;

using FormattedEditResult =
    bot::BotOperationResult<obcx::telegram::bot::EditTextReceipt>;
struct FormattedEditWork {
  std::shared_ptr<TelegramTransport> transport;
  network::TelegramReceiveOrigin origin;
  bot::MessageRef message;
  obcx::telegram::bot::NativeText rendered;
  std::string body;
  network::detail::HttpDeadline deadline;
};
auto ready_formatted_edit(FormattedEditResult result)
    -> boost::asio::awaitable<FormattedEditResult> {
  co_return result;
}
// Telegram may merge adjacent entities. Compare covered UTF-16 intervals, not
// provider array order or segmentation, while rejecting extra struck text.
auto strikethrough_ranges(const bot::Json &entities, std::string_view text)
    -> std::vector<std::pair<std::size_t, std::size_t>> {
  if (!entities.is_array())
    throw std::invalid_argument("invalid Telegram entities");
  bot::detail::validate_utf8(text);
  const auto units = core::telegram_utf16_code_units(text);
  if (units > 4096)
    throw std::invalid_argument("oversized Telegram entity text");
  // Validate individual endpoints BEFORE merging adjacent ranges. Two invalid
  // half-surrogate ranges must not merge into apparently valid coverage.
  std::vector<bool> boundaries(units + 1, false);
  std::size_t position = 0;
  for (unsigned char byte : text) {
    if ((byte & 0xc0U) == 0x80U)
      continue;
    boundaries[position] = true;
    position += byte >= 0xf0U ? 2 : 1;
  }
  boundaries[position] = true;
  std::vector<std::pair<std::size_t, std::size_t>> ranges;
  for (const auto &entity : entities) {
    if (!entity.is_object())
      throw std::invalid_argument("invalid Telegram entity");
    const auto type = entity.find("type");
    if (type == entity.end() || !type->is_string())
      throw std::invalid_argument("invalid Telegram entity type");
    const auto offset =
        bot::detail::bounded_unsigned(entity.at("offset"), units);
    const auto length =
        bot::detail::bounded_unsigned(entity.at("length"), units);
    if (!length || length > units - offset || !boundaries[offset] ||
        !boundaries[offset + length])
      throw std::invalid_argument("invalid Telegram entity character range");
    if (type->get_ref<const std::string &>() == "strikethrough")
      ranges.emplace_back(offset, offset + length);
  }
  std::ranges::sort(ranges);
  std::vector<std::pair<std::size_t, std::size_t>> merged;
  for (const auto &range : ranges) {
    if (!merged.empty() && range.first <= merged.back().second)
      merged.back().second = std::max(merged.back().second, range.second);
    else
      merged.push_back(range);
  }
  return merged;
}
auto finish_formatted_edit(std::shared_ptr<const FormattedEditWork> work)
    -> boost::asio::awaitable<FormattedEditResult> {
  using Code = bot::BotOperationErrorCode;
  bool invoked = false;
  const auto fail = [&](Code code, const char *message) {
    return FormattedEditResult::failure(
        {.code = code,
         .message = message,
         .submission_safety =
             invoked ? bot::SubmissionSafety::PossiblySubmitted
                     : bot::SubmissionSafety::DefinitelyNotSubmitted});
  };
  try {
    const auto cancellation =
        co_await boost::asio::this_coro::cancellation_state;
    const auto eligible = [work]() noexcept {
      try {
        // Revalidate the SAME authority, never capture a replacement origin.
        const auto current =
            work->transport->capture_receive(work->origin.authority);
        return current && current->epoch == work->origin.epoch &&
               current->client == work->origin.client &&
               current->access_token == work->origin.access_token;
      } catch (...) {
        return false;
      }
    };
    const auto stage_failure = [&]() -> std::optional<FormattedEditResult> {
      if (!eligible())
        return fail(Code::CacheUnavailable, "Telegram edit origin retired");
      if (cancellation.cancelled() != boost::asio::cancellation_type::none)
        return fail(Code::Cancelled, "Telegram edit cancelled");
      if (std::chrono::steady_clock::now() >= work->deadline)
        return fail(Code::TransportFailure, "Telegram edit deadline expired");
      return std::nullopt;
    };
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    std::map<std::string, std::string> headers{
        {"Content-Type", "application/json"}, {"User-Agent", "OBCX/1.0"}};
    if (!work->origin.access_token.empty())
      headers["Authorization"] = "Bearer " + work->origin.access_token;
    const auto limit =
        std::min<std::uint64_t>(work->origin.client->response_body_limit(),
                                bot::detail::domain_maximum_bytes);
    invoked = true;
    auto response = co_await network::detail::HttpCallAccess::post_until(
        *work->origin.client,
        "/bot" + work->origin.access_token + "/editMessageText", work->body,
        std::move(headers), limit, work->deadline, eligible);
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    if (response.error == boost::asio::error::operation_aborted)
      co_return fail(Code::Cancelled, "Telegram edit interrupted");
    if (response.error || !response.response_complete)
      co_return fail(Code::TransportFailure, "Telegram edit transport failed");
    try {
      if (response.body.size() > limit)
        throw std::invalid_argument("oversized Telegram edit response");
      const auto document = bot::Json::parse(
          response.body, [](int depth, bot::Json::parse_event_t, bot::Json &) {
            if (depth > 32)
              throw bot::detail::DomainBudgetExceeded{};
            return true;
          });
      bot::detail::validate_domain_wire(document);
      if (!document.is_object() || !document.at("ok").is_boolean())
        throw std::invalid_argument("invalid Telegram edit status");
      const bool ok = document.at("ok").get<bool>();
      if ((!ok && document.contains("result")) ||
          (ok && document.contains("error_code")))
        throw std::invalid_argument("contradictory Telegram edit status");
      if (!ok) {
        auto parsed = parse_telegram_operation_response(response.body, true);
        if (!parsed.error)
          throw std::invalid_argument("invalid Telegram rejection");
        parsed.error->message = "Telegram formatted edit rejected";
        if (parsed.error->provider_code &&
            !std::ranges::all_of(*parsed.error->provider_code,
                                 [](char c) { return c >= '0' && c <= '9'; }))
          parsed.error->provider_code.reset();
        co_return FormattedEditResult::failure(std::move(*parsed.error));
      }
      if (response.status_code != 200)
        co_return fail(Code::TransportFailure,
                       "Telegram edit HTTP status invalid");
      const auto &value = document.at("result");
      const auto &codec = obcx::telegram::bot::reference_codec();
      if (obcx::telegram::bot::native_conversation(
              work->message.conversation.installation, value) !=
              work->message.conversation ||
          provider_id(value, "message_id") !=
              codec.unpack(work->message.id).components[0] ||
          value.at("text") != work->rendered.text)
        throw std::invalid_argument("Telegram edited message mismatch");
      const auto entities = value.find("entities");
      const auto empty = bot::Json::array();
      if (strikethrough_ranges(entities == value.end() ? empty : *entities,
                               work->rendered.text) !=
          strikethrough_ranges(work->rendered.entities, work->rendered.text))
        throw std::invalid_argument("Telegram strikethrough mismatch");
    } catch (...) {
      co_return fail(Code::MalformedResponse,
                     "Telegram formatted edit receipt invalid");
    }
    if (auto failure = stage_failure())
      co_return std::move(*failure);
    co_return FormattedEditResult::success({work->message});
  } catch (const boost::system::system_error &error) {
    co_return fail(error.code() == boost::asio::error::operation_aborted
                       ? Code::Cancelled
                   : invoked ? Code::OutcomeUnknown
                             : Code::TransportFailure,
                   "Telegram formatted edit interrupted");
  } catch (...) {
    co_return fail(invoked ? Code::OutcomeUnknown : Code::TransportFailure,
                   "Telegram formatted edit failed");
  }
}
} // namespace

class TelegramMediaUploadComponent::Uploader final
    : public TelegramMediaUploader {
public:
  void configure(std::shared_ptr<TelegramTransport> transport) {
    if (!transport || transport_)
      throw BotComponentRuntimeError(
          "Telegram uploader requires one transport");
    transport_ = std::move(transport);
  }
  auto upload(
      nlohmann::json parameters,
      const std::vector<std::pair<std::string, TelegramMediaUpload>> &files)
      -> boost::asio::awaitable<std::string> override {
    if (!transport_)
      throw BotComponentRuntimeError("Telegram uploader not prepared");
    co_return co_await transport_->send_domain_request(std::move(parameters),
                                                       files);
  }

private:
  std::shared_ptr<TelegramTransport> transport_;
};

class TelegramOperationsComponent::Endpoint final {
public:
  Endpoint(std::string id, bool upload, std::chrono::milliseconds budget,
           std::shared_ptr<BoundedFileReader> file_reader)
      : installation_{std::move(id), bot::SurfaceId{"telegram.bot_api"}},
        upload_(upload), budget_(budget), file_reader_(std::move(file_reader)) {
  }
  auto installation() const { return installation_; }
  void configure(std::shared_ptr<adapter::telegram::ProtocolAdapter>,
                 std::shared_ptr<TelegramTransport> transport,
                 std::shared_ptr<TelegramMediaUploader> uploader,
                 std::shared_ptr<SelfIdentityCache> cache,
                 std::shared_ptr<ReplyEvidenceStore> evidence) {
    uploader_ = std::move(uploader);
    if (upload_ != static_cast<bool>(uploader_))
      throw BotComponentRuntimeError("Telegram uploader descriptor mismatch");
    transport_ = std::move(transport);
    cache_ = std::move(cache);
    evidence_ = std::move(evidence);
    transport_->configure_identity(cache_);
  }
  auto execute(const bot::GetSelfRequest &)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::SelfInfo>> {
    co_return cache_->read();
  }
  auto fetch_self(SelfIdentityOrigin origin)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::SelfInfo>> {
    auto request = transport_->capture_receive(origin);
    if (!request)
      co_return bot::failed_operation<bot::SelfInfo>(
          bot::BotOperationErrorCode::CacheUnavailable,
          "Telegram self origin retired");
    auto wire = co_await transport_->fetch_self(*request);
    if (!wire)
      co_return bot::failed_operation<bot::SelfInfo>(
          bot::BotOperationErrorCode::CacheUnavailable,
          "Telegram self origin retired");
    auto parsed = parse_telegram_operation_response(*wire, false);
    if (!parsed.ok())
      co_return provider_failure<bot::SelfInfo>(parsed);
    try {
      const auto &v = parsed_value(parsed);
      if (!v.at("is_bot").get<bool>())
        throw std::invalid_argument("not bot");
      auto id = provider_id(v, "id");
      if (!id)
        throw std::invalid_argument("missing user");
      bot::SelfInfo info{
          .user = obcx::telegram::bot::native_user(installation_, *id)};
      auto name = v.at("first_name").get<std::string>();
      if (v.contains("last_name"))
        name += " " + v.at("last_name").get<std::string>();
      if (!name.empty())
        info.display_name = std::move(name);
      if (v.contains("username"))
        info.username =
            codec().normalize_username(v.at("username").get<std::string>());
      bot::binding::validate_native_self(codec(), info);
      const auto observation = transport_->publish_self(*request, info);
      if (!observation.accepted())
        co_return bot::failed_operation<bot::SelfInfo>(
            bot::BotOperationErrorCode::CacheUnavailable,
            "Telegram self origin retired");
      co_return bot::BotOperationResult<bot::SelfInfo>::success(
          std::move(info));
    } catch (...) {
      co_return malformed_read<bot::SelfInfo>(
          "Telegram self identity is invalid");
    }
  }
  auto execute(const bot::SendMessageRequest &r,
               ConfirmedAuthorEvidenceCollector &authors)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::SendOutcome>> {
    co_return bot::BotOperationResult<bot::SendOutcome>::success(
        co_await obcx::telegram::bot::send_domain(
            r, *transport_, uploader_.get(), file_reader_, budget_, &authors));
  }
  auto execute(const obcx::telegram::bot::SendStickerRequest &r,
               ConfirmedAuthorEvidenceCollector &authors)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::SendOutcome>> {
    co_return bot::BotOperationResult<bot::SendOutcome>::success(
        co_await obcx::telegram::bot::send_sticker(r, *transport_, budget_,
                                                   &authors));
  }
  auto execute(const bot::DeleteMessageRequest &r)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::DeleteReceipt>> {
    auto p = address(r.message.conversation);
    p["method"] = "deleteMessage";
    p["message_id"] = std::stoll(codec().unpack(r.message.id).components[0]);
    p.erase("message_thread_id");
    auto parsed = co_await call(std::move(p), true);
    if (!parsed.ok())
      co_return provider_failure<bot::DeleteReceipt>(parsed);
    if (parsed_value(parsed) != true)
      co_return malformed_side_effect<bot::DeleteReceipt>(
          "Telegram deletion not confirmed");
    co_return bot::BotOperationResult<bot::DeleteReceipt>::success({r.message});
  }
  auto execute(const bot::GetMemberRequest &r)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::MemberInfo>> {
    if (codec().unpack(r.conversation.id).kind == "private")
      co_return bot::failed_operation<bot::MemberInfo>(
          bot::BotOperationErrorCode::UnsupportedAction,
          "Telegram private member lookup unavailable");
    auto p = address(r.conversation);
    p.erase("message_thread_id");
    p["method"] = "getChatMember";
    auto uid = codec().unpack(r.user.id).components[0];
    p["user_id"] = std::stoll(uid);
    auto parsed = co_await call(std::move(p), false);
    if (!parsed.ok())
      co_return provider_failure<bot::MemberInfo>(parsed);
    try {
      const auto &v = parsed_value(parsed);
      const auto &u = v.at("user");
      if (provider_id(u, "id") != uid)
        throw std::invalid_argument("member identity mismatch");
      bot::MemberInfo info{.conversation = r.conversation, .user = r.user};
      auto name = u.at("first_name").get<std::string>();
      if (u.contains("last_name"))
        name += " " + u.at("last_name").get<std::string>();
      if (!name.empty())
        info.display_name = std::move(name);
      if (u.contains("username"))
        info.username =
            codec().normalize_username(u.at("username").get<std::string>());
      info.is_bot = u.at("is_bot").get<bool>();
      const auto status = v.at("status").get<std::string>();
      if (status == "creator")
        info.role = bot::MemberRole::Owner;
      if (status == "administrator")
        info.role = bot::MemberRole::Administrator;
      if (status == "member")
        info.role = bot::MemberRole::Member;
      if (status == "creator" || status == "administrator" ||
          status == "member")
        info.membership = bot::Membership::Present;
      else if (status == "restricted")
        info.membership = v.at("is_member").get<bool>()
                              ? bot::Membership::Restricted
                              : bot::Membership::Left;
      else if (status == "left")
        info.membership = bot::Membership::Left;
      else if (status == "kicked")
        info.membership = bot::Membership::Banned;
      info.validate();
      co_return bot::BotOperationResult<bot::MemberInfo>::success(
          std::move(info));
    } catch (...) {
      co_return malformed_read<bot::MemberInfo>(
          "Telegram member result invalid");
    }
  }
  auto execute(const bot::ResolveReplyRequest &r)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::ReplyInfo>> {
    if (auto evidence = evidence_->find(r.reply))
      co_return bot::BotOperationResult<bot::ReplyInfo>::success(*evidence);
    co_return bot::failed_operation<bot::ReplyInfo>(
        bot::BotOperationErrorCode::CacheUnavailable,
        "Telegram exact local reply evidence unavailable; no history provider");
  }
  auto execute(const bot::GetCapabilitiesRequest &r) -> boost::asio::awaitable<
      bot::BotOperationResult<bot::ConversationCapabilities>> {
    auto out = obcx::telegram::bot::domain_capabilities(
        r.conversation, upload_, static_cast<bool>(file_reader_));
    if (codec().unpack(r.conversation.id).kind == "private")
      std::erase(out.operations, bot::CommonOperation::GetMember);
    if (!network::detail::TelegramMediaReadAccess::available(*transport_))
      std::erase(out.operations, bot::CommonOperation::ReadMedia);
    co_return bot::BotOperationResult<bot::ConversationCapabilities>::success(
        std::move(out));
  }
  auto execute(const bot::ReadMediaRequest &r)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::MediaBlob>> {
    const auto deadline = std::chrono::steady_clock::now() + budget_;
    const auto authority = cache_->capture_origin();
    // No coroutine suspension before native ownership is frozen by read().
    return network::detail::TelegramMediaReadAccess::read(
        *transport_, authority, r, deadline);
  }

  auto execute(const obcx::telegram::bot::EditTextRequest &r)
      -> boost::asio::awaitable<
          bot::BotOperationResult<obcx::telegram::bot::EditTextReceipt>> {
    using Result = obcx::telegram::bot::EditTextReceipt;
    bot::Json p;
    try {
      r.validate();
      codec().validate(r.message);
      p = address(r.message.conversation);
      p.erase("message_thread_id");
      p["method"] = "editMessageText";
      p["message_id"] = std::stoll(codec().unpack(r.message.id).components[0]);
      auto text = obcx::telegram::bot::native_text(r.text, 4096);
      p["text"] = text.text;
      p["entities"] = text.entities;
    } catch (const std::exception &) {
      co_return bot::failed_operation<Result>(
          bot::BotOperationErrorCode::InvalidRequest,
          "Telegram edit failed native scope or text preflight");
    }
    auto parsed = co_await call(std::move(p), true);
    if (!parsed.ok())
      co_return provider_failure<Result>(parsed);
    try {
      const auto &v = parsed_value(parsed);
      if (obcx::telegram::bot::native_conversation(installation_, v) !=
              r.message.conversation ||
          provider_id(v, "message_id") !=
              codec().unpack(r.message.id).components[0])
        throw std::invalid_argument("scope mismatch");
    } catch (...) {
      co_return malformed_side_effect<Result>("Telegram edit receipt invalid");
    }
    co_return bot::BotOperationResult<Result>::success({r.message});
  }
  auto execute(const obcx::telegram::bot::EditFormattedTextRequest &r)
      -> boost::asio::awaitable<FormattedEditResult> {
    // Synchronous ownership/preflight before the returned coroutine can
    // suspend.
    const auto deadline = std::chrono::steady_clock::now() + budget_;
    try {
      r.validate();
      codec().validate(r.message);
      if (r.message.conversation.installation != installation_)
        throw std::invalid_argument("Telegram edit installation mismatch");
      auto rendered = obcx::telegram::bot::native_formatted_text(r.text, 4096);
      auto parameters = address(r.message.conversation);
      parameters.erase("message_thread_id");
      parameters["message_id"] =
          std::stoll(codec().unpack(r.message.id).components[0]);
      parameters["text"] = rendered.text;
      parameters["entities"] = rendered.entities;
      auto body = parameters.dump();
      const auto authority = cache_->capture_origin();
      auto origin =
          authority ? transport_->capture_receive(*authority) : std::nullopt;
      if (!origin || !origin->client)
        return ready_formatted_edit(
            bot::failed_operation<obcx::telegram::bot::EditTextReceipt>(
                bot::BotOperationErrorCode::CacheUnavailable,
                "Telegram edit origin unavailable"));
      auto work = std::make_shared<FormattedEditWork>(
          FormattedEditWork{transport_, std::move(*origin), r.message,
                            std::move(rendered), std::move(body), deadline});
      return finish_formatted_edit(std::move(work));
    } catch (...) {
      return ready_formatted_edit(
          bot::failed_operation<obcx::telegram::bot::EditTextReceipt>(
              bot::BotOperationErrorCode::InvalidRequest,
              "Telegram formatted edit failed native preflight"));
    }
  }
  auto execute(const obcx::telegram::bot::CreateTopicRequest &r)
      -> boost::asio::awaitable<
          bot::BotOperationResult<obcx::telegram::bot::TopicReceipt>> {
    using Result = obcx::telegram::bot::TopicReceipt;
    codec().validate(r.parent);
    auto native = codec().unpack(r.parent.id);
    if (native.kind != "group")
      co_return bot::failed_operation<Result>(
          bot::BotOperationErrorCode::InvalidRequest,
          "Telegram topic parent must be main group");
    auto p = address(r.parent);
    p["method"] = "createForumTopic";
    p["name"] = r.name;
    auto parsed = co_await call(std::move(p), true);
    if (!parsed.ok())
      co_return provider_failure<Result>(parsed);
    try {
      const auto &v = parsed_value(parsed);
      auto topic = provider_id(v, "message_thread_id");
      if (!topic)
        throw std::invalid_argument("missing topic");
      Result result{
          {installation_, codec().make<bot::ConversationId>(
                              "topic", {native.components[0], *topic})},
          v.at("name").get<std::string>()};
      result.validate();
      co_return bot::BotOperationResult<Result>::success(std::move(result));
    } catch (...) {
      co_return malformed_side_effect<Result>("Telegram topic receipt invalid");
    }
  }

private:
  static const obcx::telegram::bot::TelegramReferenceCodec &codec() {
    return obcx::telegram::bot::reference_codec();
  }
  bot::Json address(const bot::ConversationRef &c) {
    auto p = codec().unpack(c.id);
    bot::Json out{{"chat_id", p.components[0]}};
    if (p.kind == "topic")
      out["message_thread_id"] = std::stoll(p.components[1]);
    return out;
  }
  auto call(bot::Json parameters, bool side_effect)
      -> boost::asio::awaitable<bot::BotOperationResult<bot::Json>> {
    co_return parse_telegram_operation_response(
        co_await transport_->send_action(parameters.dump(), echo_++),
        side_effect);
  }
  bot::BotInstallationRef installation_;
  bool upload_;
  std::chrono::milliseconds budget_;
  std::shared_ptr<BoundedFileReader> file_reader_;
  std::shared_ptr<TelegramTransport> transport_;
  std::shared_ptr<SelfIdentityCache> cache_;
  std::shared_ptr<TelegramMediaUploader> uploader_;
  std::shared_ptr<ReplyEvidenceStore> evidence_;
  std::atomic_uint64_t echo_{};
};

TelegramMediaUploadComponent::TelegramMediaUploadComponent()
    : uploader_(std::make_shared<Uploader>()) {}

auto TelegramMediaUploadComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"telegram.media-upload"},
      .provides = {CapabilityId{
          std::string{obcx::telegram::bot::capability_ids::media_upload}}},
      .required = {CapabilityId{
          std::string{obcx::telegram::bot::capability_ids::transport}}},
  };
}

void TelegramMediaUploadComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<TelegramMediaUploader>(
      ComponentId{"telegram.media-upload"},
      CapabilityId{
          std::string{obcx::telegram::bot::capability_ids::media_upload}},
      uploader_);
}

void TelegramMediaUploadComponent::prepare(const CapabilityRegistry &registry) {
  uploader_->configure(registry.get<TelegramTransport>(CapabilityId{
      std::string{obcx::telegram::bot::capability_ids::transport}}));
}

void TelegramMediaUploadComponent::start() {}
void TelegramMediaUploadComponent::stop() {}

TelegramOperationsComponent::TelegramOperationsComponent(
    std::string installation_id, bool include_upload,
    boost::asio::any_io_executor executor,
    std::chrono::milliseconds self_refresh_interval,
    std::chrono::milliseconds action_timeout)
    : TelegramOperationsComponent(std::move(installation_id), include_upload,
                                  std::move(executor), self_refresh_interval,
                                  action_timeout, nullptr) {}

TelegramOperationsComponent::TelegramOperationsComponent(
    std::string installation_id, bool include_upload,
    boost::asio::any_io_executor executor,
    std::chrono::milliseconds self_refresh_interval,
    std::chrono::milliseconds action_timeout,
    std::shared_ptr<BoundedFileReader> file_reader)
    : endpoint_(std::make_shared<Endpoint>(std::move(installation_id),
                                           include_upload, action_timeout,
                                           std::move(file_reader))),
      operations_(std::make_shared<OperationRegistry>(
          endpoint_->installation(), obcx::telegram::bot::domain_binding())),
      self_identity_(std::make_shared<SelfIdentityCache>(
          std::move(executor), endpoint_->installation(),
          self_refresh_interval)),
      include_upload_(include_upload) {}

auto TelegramOperationsComponent::descriptor() const -> ComponentDescriptor {
  std::vector<CapabilityId> dependencies;
  for (const auto &id :
       obcx::telegram::bot::operation_dependencies(include_upload_)) {
    dependencies.emplace_back(id);
  }
  return {.id = ComponentId{"telegram.operations"},
          .provides = {CapabilityId{"bot.operations"}},
          .required = std::move(dependencies)};
}

void TelegramOperationsComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<OperationRegistry>(ComponentId{"telegram.operations"},
                                      CapabilityId{"bot.operations"},
                                      operations_);
}

void TelegramOperationsComponent::prepare(const CapabilityRegistry &registry) {
  std::shared_ptr<TelegramMediaUploader> uploader;
  if (include_upload_) {
    uploader = registry.get<TelegramMediaUploader>(
        CapabilityId{"telegram.media-upload"});
  }
  endpoint_->configure(
      registry.get<adapter::telegram::ProtocolAdapter>(CapabilityId{
          std::string{obcx::telegram::bot::capability_ids::protocol}}),
      registry.get<TelegramTransport>(CapabilityId{
          std::string{obcx::telegram::bot::capability_ids::transport}}),
      std::move(uploader), self_identity_, operations_->reply_evidence());
  self_identity_->configure(
      [weak = std::weak_ptr(endpoint_)](SelfIdentityOrigin origin)
          -> boost::asio::awaitable<SelfIdentityCache::Result> {
        auto endpoint = weak.lock();
        if (!endpoint)
          throw std::runtime_error("self identity endpoint retired");
        co_return co_await endpoint->fetch_self(std::move(origin));
      });
  operations_->set_self_identity_cache(self_identity_);
  obcx::telegram::bot::for_each_operation(
      include_upload_, [&](const auto &definition) {
        using Request =
            typename std::remove_cvref_t<decltype(definition)>::request_type;
        if constexpr (std::same_as<Request, bot::SendMessageRequest> ||
                      std::same_as<Request,
                                   obcx::telegram::bot::SendStickerRequest>) {
          operations_->install(definition.bind_with_author_evidence(
              [endpoint = endpoint_](const Request &r,
                                     ConfirmedAuthorEvidenceCollector &authors)
                  -> boost::asio::awaitable<
                      bot::BotOperationResult<bot::SendOutcome>> {
                co_return co_await endpoint->execute(r, authors);
              }));
        } else
          operations_->install(definition.bind(bind_operation_handler<Request>(
              endpoint_, obcx::telegram::bot::redact_diagnostic)));
      });
  std::vector<std::string> available;
  for (const auto &id :
       obcx::telegram::bot::operation_dependencies(include_upload_)) {
    if (registry.contains(CapabilityId{id})) {
      available.push_back(id);
    }
  }
  operations_->seal(endpoint_->installation(), available);
}

void TelegramOperationsComponent::start() {}
void TelegramOperationsComponent::stop() { operations_->close(); }

} // namespace obcx::core
