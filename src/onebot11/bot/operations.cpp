#include "../../core/bot/operation_response_helpers.hpp"
#include "core/bot/operation_handler.hpp"
#include "diagnostics.hpp"
#include "onebot11/bot/capability_ids.hpp"
#include "onebot11/bot/operation_component.hpp"
#include "onebot11/bot/operation_definitions.hpp"
#include "onebot11/bot/response_parser.hpp"
#include "onebot11/bot/transport.hpp"

#include <atomic>
#include <type_traits>

namespace obcx::core {
namespace {
using obcx::onebot11::bot::parse_onebot11_operation_response;
using operation_detail::malformed_read;
using operation_detail::malformed_side_effect;
using operation_detail::optional_string;
using operation_detail::parsed_value;
using operation_detail::provider_failure;
using operation_detail::provider_id;
} // namespace

class OneBot11OperationsComponent::Endpoint final {
public:
  explicit Endpoint(std::string installation_id)
      : installation_{.installation_id = std::move(installation_id),
                      .surface = bot::SurfaceId{"onebot11.qq"}} {
    installation_.validate();
  }

  void configure(std::shared_ptr<adapter::onebot11::ProtocolAdapter> protocol,
                 std::shared_ptr<OneBot11Transport> transport) {
    if (protocol == nullptr || transport == nullptr || protocol_ != nullptr ||
        transport_ != nullptr) {
      throw BotComponentRuntimeError(
          "OneBot operations require one protocol and transport");
    }
    protocol_ = std::move(protocol);
    transport_ = std::move(transport);
  }

  [[nodiscard]] auto installation() const -> bot::BotInstallationRef {
    return installation_;
  }

  auto execute(const bot::SendGroupMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendMessageResult>> {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_send_group_message_request(
            request.target.native_group_id, request.message, echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::SendMessageResult>(parsed);
    }
    const auto message_id = provider_id(parsed_value(parsed), "message_id");
    if (!message_id) {
      co_return malformed_side_effect<bot::SendMessageResult>(
          "OneBot send response is missing message_id");
    }
    co_return bot::BotOperationResult<bot::SendMessageResult>::success(
        {.messages = {
             {.group = request.target, .native_message_id = *message_id}}});
  }

  auto execute(const bot::SendPrivateMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::SendPrivateMessageResult>> {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_send_private_message_request(
            request.target.native_user_id, request.message, echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::SendPrivateMessageResult>(parsed);
    }
    const auto message_id = provider_id(parsed_value(parsed), "message_id");
    if (!message_id) {
      co_return malformed_side_effect<bot::SendPrivateMessageResult>(
          "OneBot private-send response is missing message_id");
    }
    co_return bot::BotOperationResult<bot::SendPrivateMessageResult>::success(
        {.messages = {
             {.target = request.target, .native_message_id = *message_id}}});
  }

  auto execute(const bot::DeleteMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<bot::DeleteMessageResult>> {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_delete_message_request(
            {}, request.message.native_message_id, echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<bot::DeleteMessageResult>(parsed);
    }
    co_return bot::BotOperationResult<bot::DeleteMessageResult>::success(
        {.message = request.message});
  }

  auto execute(const obcx::onebot11::bot::GetOneBotGroupMemberRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<obcx::onebot11::bot::OneBotGroupMember>> {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_get_chat_member_info_request(
            request.target.native_group_id, request.user_id, request.no_cache,
            echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok()) {
      co_return provider_failure<obcx::onebot11::bot::OneBotGroupMember>(
          parsed);
    }
    const auto user_id = provider_id(parsed_value(parsed), "user_id");
    if (!user_id) {
      co_return malformed_read<obcx::onebot11::bot::OneBotGroupMember>(
          "OneBot member response is missing user_id");
    }
    co_return bot::BotOperationResult<obcx::onebot11::bot::OneBotGroupMember>::
        success({.target = request.target,
                 .user_id = *user_id,
                 .nickname = optional_string(parsed_value(parsed), "nickname"),
                 .card = optional_string(parsed_value(parsed), "card"),
                 .title = optional_string(parsed_value(parsed), "title")});
  }

  auto execute(
      const obcx::onebot11::bot::GetOneBotForwardMessageRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<obcx::onebot11::bot::OneBotForwardMessage>> {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_get_forward_msg_request(request.forward_id, echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok()) {
      co_return provider_failure<obcx::onebot11::bot::OneBotForwardMessage>(
          parsed);
    }
    if (!parsed_value(parsed).is_object() ||
        !parsed_value(parsed).contains("messages") ||
        !parsed_value(parsed).at("messages").is_array()) {
      co_return malformed_read<obcx::onebot11::bot::OneBotForwardMessage>(
          "OneBot forward response is missing messages");
    }
    obcx::onebot11::bot::OneBotForwardMessage result{
        .installation = installation_,
        .forward_id = request.forward_id,
        .messages = parsed_value(parsed).at("messages")};
    try {
      result.validate();
    } catch (const std::exception &error) {
      co_return malformed_read<obcx::onebot11::bot::OneBotForwardMessage>(
          error.what());
    }
    co_return bot::BotOperationResult<
        obcx::onebot11::bot::OneBotForwardMessage>::success(std::move(result));
  }

  auto execute(
      const obcx::onebot11::bot::ResolveOneBotGroupFileRequest &request)
      -> boost::asio::awaitable<bot::BotOperationResult<
          obcx::onebot11::bot::ResolvedOneBotGroupFile>> {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_get_group_file_url_request(
            request.target.native_group_id, request.file_id, echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok()) {
      co_return provider_failure<obcx::onebot11::bot::ResolvedOneBotGroupFile>(
          parsed);
    }
    const auto url = optional_string(parsed_value(parsed), "url");
    if (url.empty()) {
      co_return malformed_read<obcx::onebot11::bot::ResolvedOneBotGroupFile>(
          "OneBot group-file response is missing url");
    }
    co_return bot::BotOperationResult<
        obcx::onebot11::bot::ResolvedOneBotGroupFile>::
        success(
            {.target = request.target, .file_id = request.file_id, .url = url});
  }

  auto execute(
      const obcx::onebot11::bot::ResolveOneBotPrivateFileRequest &request)
      -> boost::asio::awaitable<bot::BotOperationResult<
          obcx::onebot11::bot::ResolvedOneBotPrivateFile>> {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_get_private_file_url_request(
            request.user_id, request.file_id, echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, false);
    if (!parsed.ok()) {
      co_return provider_failure<
          obcx::onebot11::bot::ResolvedOneBotPrivateFile>(parsed);
    }
    const auto url = optional_string(parsed_value(parsed), "url");
    if (url.empty()) {
      co_return malformed_read<obcx::onebot11::bot::ResolvedOneBotPrivateFile>(
          "OneBot private-file response is missing url");
    }
    co_return bot::BotOperationResult<
        obcx::onebot11::bot::ResolvedOneBotPrivateFile>::
        success({.installation = installation_,
                 .user_id = request.user_id,
                 .file_id = request.file_id,
                 .url = url});
  }

  auto execute(const obcx::onebot11::bot::PokeOneBotGroupRequest &request)
      -> boost::asio::awaitable<
          bot::BotOperationResult<obcx::onebot11::bot::OneBotGroupPokeResult>> {
    const auto echo = next_echo();
    const auto response = co_await transport().send_action(
        protocol().serialize_group_poke_request(request.target.native_group_id,
                                                request.user_id, echo),
        echo);
    const auto parsed = parse_onebot11_operation_response(response, true);
    if (!parsed.ok()) {
      co_return provider_failure<obcx::onebot11::bot::OneBotGroupPokeResult>(
          parsed);
    }
    co_return bot::
        BotOperationResult<obcx::onebot11::bot::OneBotGroupPokeResult>::success(
            {.target = request.target, .user_id = request.user_id});
  }

private:
  [[nodiscard]] auto next_echo() noexcept -> std::uint64_t {
    return echo_.fetch_add(1, std::memory_order_relaxed);
  }
  [[nodiscard]] auto protocol() const -> adapter::onebot11::ProtocolAdapter & {
    if (protocol_ == nullptr) {
      throw BotComponentRuntimeError("OneBot operations are not prepared");
    }
    return *protocol_;
  }
  [[nodiscard]] auto transport() const -> OneBot11Transport & {
    if (transport_ == nullptr) {
      throw BotComponentRuntimeError("OneBot operations are not prepared");
    }
    return *transport_;
  }

  bot::BotInstallationRef installation_;
  std::shared_ptr<adapter::onebot11::ProtocolAdapter> protocol_;
  std::shared_ptr<OneBot11Transport> transport_;
  std::atomic_uint64_t echo_{};
};

OneBot11OperationsComponent::OneBot11OperationsComponent(
    std::string installation_id)
    : endpoint_(std::make_shared<Endpoint>(std::move(installation_id))),
      operations_(
          std::make_shared<OperationRegistry>(endpoint_->installation())) {}

auto OneBot11OperationsComponent::descriptor() const -> ComponentDescriptor {
  std::vector<CapabilityId> dependencies;
  for (const auto &id : obcx::onebot11::bot::operation_dependencies()) {
    dependencies.emplace_back(id);
  }
  return {.id = ComponentId{"onebot11.operations"},
          .provides = {CapabilityId{"bot.operations"}},
          .required = std::move(dependencies)};
}

void OneBot11OperationsComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<OperationRegistry>(ComponentId{"onebot11.operations"},
                                      CapabilityId{"bot.operations"},
                                      operations_);
}

void OneBot11OperationsComponent::prepare(const CapabilityRegistry &registry) {
  endpoint_->configure(
      registry.get<adapter::onebot11::ProtocolAdapter>(CapabilityId{
          std::string{obcx::onebot11::bot::capability_ids::protocol}}),
      registry.get<OneBot11Transport>(CapabilityId{
          std::string{obcx::onebot11::bot::capability_ids::transport}}));
  obcx::onebot11::bot::for_each_operation([&](const auto &definition) {
    using Request =
        typename std::remove_cvref_t<decltype(definition)>::request_type;
    operations_->install(definition.bind(bind_operation_handler<Request>(
        endpoint_, obcx::onebot11::bot::redact_diagnostic)));
  });
  std::vector<std::string> available;
  for (const auto &id : obcx::onebot11::bot::operation_dependencies()) {
    if (registry.contains(CapabilityId{id})) {
      available.push_back(id);
    }
  }
  operations_->seal(endpoint_->installation(), available);
}

void OneBot11OperationsComponent::start() {}
void OneBot11OperationsComponent::stop() { operations_->close(); }

} // namespace obcx::core
