#include "standard_operation_binding.hpp"
#include "standard_event_action_codec.hpp"

namespace obcx::onebot11::bot::standard::detail {
namespace {
// Exhaustive request semantics, not a platform/action/destination whitelist.
// New variants require an explicit authorship decision here.
auto ordinary_send_leaf(const SendLeaf &leaf) -> bool {
  return std::visit(
      []<class Leaf>(const Leaf &) -> bool {
        if constexpr (std::same_as<Leaf, Text> || std::same_as<Leaf, Face> ||
                      std::same_as<Leaf, AtUser> || std::same_as<Leaf, AtAll> ||
                      std::same_as<Leaf, Rps> || std::same_as<Leaf, Dice> ||
                      std::same_as<Leaf, Shake> || std::same_as<Leaf, Poke> ||
                      std::same_as<Leaf, Share> ||
                      std::same_as<Leaf, UserContact> ||
                      std::same_as<Leaf, GroupContact> ||
                      std::same_as<Leaf, Location> ||
                      std::same_as<Leaf, Music> ||
                      std::same_as<Leaf, CustomMusic> ||
                      std::same_as<Leaf, Image> || std::same_as<Leaf, Record> ||
                      std::same_as<Leaf, Video>)
          return true;
        else if constexpr (std::same_as<Leaf, Anonymous> ||
                           std::same_as<Leaf, XmlContent> ||
                           std::same_as<Leaf, JsonContent>)
          return false; // Any ignore state; opaque documents are uninterpreted.
        else
          static_assert(!sizeof(Leaf),
                        "classify standard leaf author semantics");
      },
      leaf);
}
auto ordinary_send_segment(const Segment &segment) -> bool {
  return std::visit(
      []<class Part>(const Part &part) -> bool {
        if constexpr (std::same_as<Part, SendLeaf>)
          return ordinary_send_leaf(part);
        else if constexpr (std::same_as<Part, Reply>)
          return true; // The quoted author's identity is not the new sender.
        else if constexpr (std::same_as<Part, AuthoredNode> ||
                           std::same_as<Part, ScopedReferenceNode> ||
                           std::same_as<Part, IdSelectedNode>)
          return false; // No outer-author inference from nodes or selectors.
        else
          static_assert(!sizeof(Part),
                        "classify standard segment author semantics");
      },
      segment);
}
auto ordinary_send_message(const MessageInput &message) -> bool {
  return std::visit(
      []<class Input>(const Input &input) -> bool {
        if constexpr (std::same_as<Input, LiteralText>)
          return true; // Validated auto_escape: CQ-looking text stays literal.
        else if constexpr (std::same_as<Input, Composition>) {
          // The validated rooted tree has bounded, uniquely owned sequences.
          // Inspect all sequences, not just root leaves (including CQ
          // children).
          for (const auto &sequence : input.sequences)
            for (const auto &segment : sequence.segments)
              if (!ordinary_send_segment(segment))
                return false;
          return true;
        } else
          static_assert(!sizeof(Input),
                        "classify standard message author semantics");
      },
      message);
}

// Standard segments do not establish common-body or reusable-media provenance.
// The opaque native outcome and this physical-effects projection stay separate.
template <class Request>
requires(std::same_as<Request, SendPrivateMsgRequest> ||
         std::same_as<Request, SendGroupMsgRequest> ||
         std::same_as<Request, SendMsgRequest>)
auto send_message_policy() -> obcx::core::StructuredMessagePolicy<Request> {
  namespace domain = obcx::bot;
  using Result = typename domain::OperationTraits<Request>::result_type;
  return {
      .target =
          [](const Request &request) -> std::optional<domain::ConversationRef> {
        return request.parameters.target;
      },
      .project = [](const Request &request, const Result &result)
          -> std::optional<domain::SendOutcome> {
        domain::OperationTraits<Request>::validate_result(request, result);
        if (const auto *completed =
                std::get_if<Completed<SendMessageData>>(&result.state)) {
          const auto &message = completed->value.message;
          return domain::SendOutcome{
              domain::SendReceipt{{message}, {{message, std::nullopt, {}}}}};
        }
        const bool unknown =
            result.evidence.submission != Submission::NotSubmitted;
        auto code = domain::BotOperationErrorCode::OutcomeUnknown;
        if (const auto *failure = std::get_if<ExecutionFailure>(&result.state))
          code = failure->error.code;
        return domain::SendOutcome{domain::SendFailure{
            {.code = code,
             .message = "standard send has no confirmed message result",
             .retryable = false,
             .submission_safety =
                 unknown ? domain::SubmissionSafety::PossiblySubmitted
                         : domain::SubmissionSafety::DefinitelyNotSubmitted},
            {},
            {},
            unknown}};
      },
      .collect_confirmed_authors =
          [](const Request &request, const Result &result,
             const domain::SendOutcome &recovered,
             obcx::core::ConfirmedAuthorEvidenceCollector &authors) {
            if (!std::holds_alternative<Completed<SendMessageData>>(
                    result.state) ||
                !ordinary_send_message(request.parameters.message))
              return;
            for (const auto &message :
                 obcx::core::confirmed_author_detail::messages(recovered))
              authors.initiating_self(message);
          }};
}
} // namespace

auto send_private_msg_definition() -> const SendPrivateMsgDefinition & {
  static const SendPrivateMsgDefinition definition{
      {"onebot11.protocol", "onebot11.transport"},
      send_message_policy<SendPrivateMsgRequest>()};
  return definition;
}

auto bind_send_private_msg(SendPrivateMsgDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "send_private_msg requires an executable handler");
  return send_private_msg_definition().bind(
      [handler = std::move(handler)](const SendPrivateMsgRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard send_private_msg dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto send_group_msg_definition() -> const SendGroupMsgDefinition & {
  static const SendGroupMsgDefinition definition{
      {"onebot11.protocol", "onebot11.transport"},
      send_message_policy<SendGroupMsgRequest>()};
  return definition;
}

auto bind_send_group_msg(SendGroupMsgDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "send_group_msg requires an executable handler");
  return send_group_msg_definition().bind(
      [handler = std::move(handler)](const SendGroupMsgRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard send_group_msg dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto send_msg_definition() -> const SendMsgDefinition & {
  static const SendMsgDefinition definition{
      {"onebot11.protocol", "onebot11.transport"},
      send_message_policy<SendMsgRequest>()};
  return definition;
}

auto bind_send_msg(SendMsgDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument("send_msg requires an executable handler");
  return send_msg_definition().bind(
      [handler = std::move(handler)](const SendMsgRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard send_msg dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto send_like_definition() -> const SendLikeDefinition & {
  static const SendLikeDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_send_like(SendLikeDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument("send_like requires an executable handler");
  return send_like_definition().bind(
      [handler = std::move(handler)](const SendLikeRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard send_like dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto delete_msg_definition() -> const DeleteMsgDefinition & {
  static const DeleteMsgDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_delete_msg(DeleteMsgDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument("delete_msg requires an executable handler");
  return delete_msg_definition().bind(
      [handler = std::move(handler)](const DeleteMsgRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard delete_msg dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto set_group_kick_definition() -> const SetGroupKickDefinition & {
  static const SetGroupKickDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_set_group_kick(SetGroupKickDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "set_group_kick requires an executable handler");
  return set_group_kick_definition().bind(
      [handler = std::move(handler)](const SetGroupKickRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard set_group_kick dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto set_group_ban_definition() -> const SetGroupBanDefinition & {
  static const SetGroupBanDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_set_group_ban(SetGroupBanDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument("set_group_ban requires an executable handler");
  return set_group_ban_definition().bind(
      [handler = std::move(handler)](const SetGroupBanRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard set_group_ban dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto set_group_whole_ban_definition() -> const SetGroupWholeBanDefinition & {
  static const SetGroupWholeBanDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_set_group_whole_ban(SetGroupWholeBanDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "set_group_whole_ban requires an executable handler");
  return set_group_whole_ban_definition().bind(
      [handler = std::move(handler)](const SetGroupWholeBanRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard set_group_whole_ban dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto set_group_admin_definition() -> const SetGroupAdminDefinition & {
  static const SetGroupAdminDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_set_group_admin(SetGroupAdminDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "set_group_admin requires an executable handler");
  return set_group_admin_definition().bind(
      [handler = std::move(handler)](const SetGroupAdminRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard set_group_admin dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto set_group_anonymous_definition() -> const SetGroupAnonymousDefinition & {
  static const SetGroupAnonymousDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_set_group_anonymous(SetGroupAnonymousDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "set_group_anonymous requires an executable handler");
  return set_group_anonymous_definition().bind(
      [handler = std::move(handler)](const SetGroupAnonymousRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard set_group_anonymous dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto set_group_card_definition() -> const SetGroupCardDefinition & {
  static const SetGroupCardDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_set_group_card(SetGroupCardDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "set_group_card requires an executable handler");
  return set_group_card_definition().bind(
      [handler = std::move(handler)](const SetGroupCardRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard set_group_card dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto set_group_name_definition() -> const SetGroupNameDefinition & {
  static const SetGroupNameDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_set_group_name(SetGroupNameDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "set_group_name requires an executable handler");
  return set_group_name_definition().bind(
      [handler = std::move(handler)](const SetGroupNameRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard set_group_name dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto set_group_leave_definition() -> const SetGroupLeaveDefinition & {
  static const SetGroupLeaveDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_set_group_leave(SetGroupLeaveDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "set_group_leave requires an executable handler");
  return set_group_leave_definition().bind(
      [handler = std::move(handler)](const SetGroupLeaveRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard set_group_leave dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto set_group_special_title_definition()
    -> const SetGroupSpecialTitleDefinition & {
  static const SetGroupSpecialTitleDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_set_group_special_title(
    SetGroupSpecialTitleDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "set_group_special_title requires an executable handler");
  return set_group_special_title_definition().bind(
      [handler = std::move(handler)](const SetGroupSpecialTitleRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message =
                       "standard set_group_special_title dispatch deadline "
                       "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_stranger_info_definition() -> const GetStrangerInfoDefinition & {
  static const GetStrangerInfoDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_stranger_info(GetStrangerInfoDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "get_stranger_info requires an executable handler");
  return get_stranger_info_definition().bind(
      [handler = std::move(handler)](const GetStrangerInfoRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard get_stranger_info dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_login_info_definition() -> const GetLoginInfoDefinition & {
  static const GetLoginInfoDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_login_info(GetLoginInfoDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "get_login_info requires an executable handler");
  return get_login_info_definition().bind(
      [handler = std::move(handler)](const GetLoginInfoRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard get_login_info dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_friend_list_definition() -> const GetFriendListDefinition & {
  static const GetFriendListDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_friend_list(GetFriendListDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "get_friend_list requires an executable handler");
  return get_friend_list_definition().bind(
      [handler = std::move(handler)](const GetFriendListRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard get_friend_list dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_group_info_definition() -> const GetGroupInfoDefinition & {
  static const GetGroupInfoDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_group_info(GetGroupInfoDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "get_group_info requires an executable handler");
  return get_group_info_definition().bind(
      [handler = std::move(handler)](const GetGroupInfoRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard get_group_info dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_group_list_definition() -> const GetGroupListDefinition & {
  static const GetGroupListDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_group_list(GetGroupListDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "get_group_list requires an executable handler");
  return get_group_list_definition().bind(
      [handler = std::move(handler)](const GetGroupListRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard get_group_list dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_group_member_info_definition()
    -> const GetGroupMemberInfoDefinition & {
  static const GetGroupMemberInfoDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_group_member_info(GetGroupMemberInfoDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "get_group_member_info requires an executable handler");
  return get_group_member_info_definition().bind(
      [handler = std::move(handler)](const GetGroupMemberInfoRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message =
                       "standard get_group_member_info dispatch deadline "
                       "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_group_member_list_definition()
    -> const GetGroupMemberListDefinition & {
  static const GetGroupMemberListDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_group_member_list(GetGroupMemberListDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "get_group_member_list requires an executable handler");
  return get_group_member_list_definition().bind(
      [handler = std::move(handler)](const GetGroupMemberListRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message =
                       "standard get_group_member_list dispatch deadline "
                       "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_group_honor_info_definition() -> const GetGroupHonorInfoDefinition & {
  static const GetGroupHonorInfoDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_group_honor_info(GetGroupHonorInfoDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "get_group_honor_info requires an executable handler");
  return get_group_honor_info_definition().bind(
      [handler = std::move(handler)](const GetGroupHonorInfoRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard get_group_honor_info dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_cookies_definition() -> const GetCookiesDefinition & {
  static const GetCookiesDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_cookies(GetCookiesDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument("get_cookies requires an executable handler");
  return get_cookies_definition().bind(
      [handler = std::move(handler)](const GetCookiesRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard get_cookies dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_csrf_token_definition() -> const GetCsrfTokenDefinition & {
  static const GetCsrfTokenDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_csrf_token(GetCsrfTokenDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "get_csrf_token requires an executable handler");
  return get_csrf_token_definition().bind(
      [handler = std::move(handler)](const GetCsrfTokenRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard get_csrf_token dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_credentials_definition() -> const GetCredentialsDefinition & {
  static const GetCredentialsDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_credentials(GetCredentialsDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "get_credentials requires an executable handler");
  return get_credentials_definition().bind(
      [handler = std::move(handler)](const GetCredentialsRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard get_credentials dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_status_definition() -> const GetStatusDefinition & {
  static const GetStatusDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_status(GetStatusDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument("get_status requires an executable handler");
  return get_status_definition().bind(
      [handler = std::move(handler)](const GetStatusRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard get_status dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_version_info_definition() -> const GetVersionInfoDefinition & {
  static const GetVersionInfoDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_version_info(GetVersionInfoDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "get_version_info requires an executable handler");
  return get_version_info_definition().bind(
      [handler = std::move(handler)](const GetVersionInfoRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard get_version_info dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto can_send_image_definition() -> const CanSendImageDefinition & {
  static const CanSendImageDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_can_send_image(CanSendImageDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "can_send_image requires an executable handler");
  return can_send_image_definition().bind(
      [handler = std::move(handler)](const CanSendImageRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard can_send_image dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto can_send_record_definition() -> const CanSendRecordDefinition & {
  static const CanSendRecordDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_can_send_record(CanSendRecordDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "can_send_record requires an executable handler");
  return can_send_record_definition().bind(
      [handler = std::move(handler)](const CanSendRecordRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard can_send_record dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto set_restart_definition() -> const SetRestartDefinition & {
  static const SetRestartDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_set_restart(SetRestartDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument("set_restart requires an executable handler");
  return set_restart_definition().bind(
      [handler = std::move(handler)](const SetRestartRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard set_restart dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto clean_cache_definition() -> const CleanCacheDefinition & {
  static const CleanCacheDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_clean_cache(CleanCacheDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument("clean_cache requires an executable handler");
  return clean_cache_definition().bind(
      [handler = std::move(handler)](const CleanCacheRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard clean_cache dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_msg_definition() -> const GetMsgDefinition & {
  static const GetMsgDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_msg(GetMsgDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument("get_msg requires an executable handler");
  return get_msg_definition().bind(
      [handler = std::move(handler)](const GetMsgRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard get_msg dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_forward_msg_definition() -> const GetForwardMsgDefinition & {
  static const GetForwardMsgDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_forward_msg(GetForwardMsgDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument(
        "get_forward_msg requires an executable handler");
  return get_forward_msg_definition().bind(
      [handler = std::move(handler)](const GetForwardMsgRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard get_forward_msg dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}
auto get_image_definition() -> const GetImageDefinition & {
  static const GetImageDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_image(GetImageDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument("get_image requires an executable handler");
  return get_image_definition().bind(
      [handler = std::move(handler)](const GetImageRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard get_image dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}

auto get_record_definition() -> const GetRecordDefinition & {
  static const GetRecordDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}

auto bind_get_record(GetRecordDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument("get_record requires an executable handler");
  return get_record_definition().bind(
      [handler = std::move(handler)](const GetRecordRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "standard get_record dispatch deadline "
                              "unavailable or elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        // Trusted process handler must still validate native account/origin,
        // epoch, deadline and write ordering and checkpoint its own evidence.
        // After entry, exceptions keep the core binding's conservative policy.
        co_return co_await handler(request);
      });
}

} // namespace obcx::onebot11::bot::standard::detail

namespace obcx::onebot11::bot::standard::detail {
auto set_group_anonymous_ban_definition()
    -> const SetGroupAnonymousBanDefinition & {
  static const SetGroupAnonymousBanDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}
auto bind_set_group_anonymous_ban(
    SetGroupAnonymousBanDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument("event action requires executable handler");
  return set_group_anonymous_ban_definition().bind(
      [handler = std::move(handler)](const SetGroupAnonymousBanRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "event action deadline elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        co_return co_await handler(request);
      });
}
auto set_friend_add_request_definition()
    -> const SetFriendAddRequestDefinition & {
  static const SetFriendAddRequestDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}
auto bind_set_friend_add_request(SetFriendAddRequestDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument("event action requires executable handler");
  return set_friend_add_request_definition().bind(
      [handler = std::move(handler)](const SetFriendAddRequestRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "event action deadline elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        co_return co_await handler(request);
      });
}
auto set_group_add_request_definition()
    -> const SetGroupAddRequestDefinition & {
  static const SetGroupAddRequestDefinition definition{
      {"onebot11.protocol", "onebot11.transport"}};
  return definition;
}
auto bind_set_group_add_request(SetGroupAddRequestDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument("event action requires executable handler");
  return set_group_add_request_definition().bind(
      [handler = std::move(handler)](const SetGroupAddRequestRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "event action deadline elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        co_return co_await handler(request);
      });
}
auto handle_quick_operation_definition()
    -> const HandleQuickOperationDefinition & {
  static const HandleQuickOperationDefinition definition{
      {"onebot11.protocol", "onebot11.transport"},
      obcx::core::StructuredMessagePolicy<HandleQuickOperationRequest>{
          quick_operation_target, quick_operation_effect}};
  return definition;
}
auto bind_handle_quick_operation(
    HandleQuickOperationDefinition::Handler handler)
    -> obcx::core::OperationBinding {
  if (!handler)
    throw std::invalid_argument("event action requires executable handler");
  return handle_quick_operation_definition().bind(
      [handler = std::move(handler)](const HandleQuickOperationRequest &request)
          -> boost::asio::awaitable<obcx::core::StructuredOperationCompletion> {
        const auto &deadline = StandardRequestAccess::deadline(request);
        if (!deadline || std::chrono::steady_clock::now() >= *deadline)
          co_return obcx::core::StructuredOperationCompletion{
              obcx::bot::StructuredOperationReply::failure(
                  {.code = obcx::bot::BotOperationErrorCode::Cancelled,
                   .message = "event action deadline elapsed",
                   .retryable = false,
                   .submission_safety =
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}),
              obcx::core::StructuredCompletionDisposition::Failed};
        co_return co_await handler(request);
      });
}
} // namespace obcx::onebot11::bot::standard::detail
