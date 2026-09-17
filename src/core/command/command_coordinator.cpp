#include "core/command/command_coordinator.hpp"

#include "common/logger.hpp"
#include "core/actor/actor_messages.hpp"
#include "core/actor/reflected_actor.hpp"
#include "core/runtime/process_configuration.hpp"

#include <algorithm>
#include <boost/asio/async_result.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <cctype>
#include <functional>
#include <mutex>
#include <ranges>
#include <set>
#include <tuple>
#include <utility>

namespace obcx::core {
namespace {

auto command_failure(std::string code, std::string message)
    -> CommandRoutingBuildResult {
  return {
      .table = nullptr,
      .failure =
          CommandRoutingBuildFailure{
              .code = std::move(code),
              .message = std::move(message),
          },
  };
}

auto find_registration(const ActorInputContract &contract,
                       const std::string_view command)
    -> const ActorCommandRegistration * {
  const auto registration = std::ranges::find(contract.commands, command,
                                              &ActorCommandRegistration::name);
  return registration == contract.commands.end() ? nullptr : &*registration;
}

auto command_timeout(const common::CommandRuntimeConfig &runtime,
                     const common::CommandRouteConfig &route)
    -> std::chrono::milliseconds {
  return std::chrono::milliseconds{route.timeout_ms == 0 ? runtime.timeout_ms
                                                         : route.timeout_ms};
}

auto compile_group_policy(const common::CommandGroupPolicyConfig &configured)
    -> std::optional<ActiveCommandAccessPolicy> {
  if (!configured.mode || !configured.entries_present) {
    return std::nullopt;
  }
  ActiveCommandAccessPolicy policy{.mode = *configured.mode};
  for (const auto &entry : configured.entries) {
    if (!policy.entries
             .emplace(CommandAccessIdentity{.platform = entry.platform,
                                            .bot = entry.bot,
                                            .native_id = entry.native_group_id})
             .second) {
      return std::nullopt;
    }
  }
  if (policy.mode == common::CommandAccessMode::Unrestricted &&
      !policy.entries.empty()) {
    return std::nullopt;
  }
  return policy;
}

auto compile_user_policy(const common::CommandUserPolicyConfig &configured)
    -> std::optional<ActiveCommandAccessPolicy> {
  if (!configured.mode || !configured.entries_present) {
    return std::nullopt;
  }
  ActiveCommandAccessPolicy policy{.mode = *configured.mode};
  for (const auto &entry : configured.entries) {
    if (!policy.entries
             .emplace(CommandAccessIdentity{.platform = entry.platform,
                                            .bot = entry.bot,
                                            .native_id = entry.native_user_id})
             .second) {
      return std::nullopt;
    }
  }
  if (policy.mode == common::CommandAccessMode::Unrestricted &&
      !policy.entries.empty()) {
    return std::nullopt;
  }
  return policy;
}

auto compile_command_policy(const common::CommandGroupPolicyConfig &groups,
                            const common::CommandUserPolicyConfig &users)
    -> std::optional<ActiveCommandPolicy> {
  auto group_policy = compile_group_policy(groups);
  auto user_policy = compile_user_policy(users);
  if (!group_policy || !user_policy) {
    return std::nullopt;
  }
  return ActiveCommandPolicy{.groups = std::move(*group_policy),
                             .users = std::move(*user_policy)};
}

auto valid_utf8(const std::string_view value) -> bool {
  std::size_t index = 0;
  while (index < value.size()) {
    const auto lead = static_cast<unsigned char>(value[index]);
    std::size_t trailing = 0;
    std::uint32_t codepoint = 0;
    if (lead <= 0x7FU) {
      ++index;
      continue;
    }
    if ((lead & 0xE0U) == 0xC0U) {
      trailing = 1;
      codepoint = lead & 0x1FU;
      if (codepoint < 2U) {
        return false;
      }
    } else if ((lead & 0xF0U) == 0xE0U) {
      trailing = 2;
      codepoint = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
      trailing = 3;
      codepoint = lead & 0x07U;
    } else {
      return false;
    }
    if (index + trailing >= value.size()) {
      return false;
    }
    for (std::size_t offset = 1; offset <= trailing; ++offset) {
      const auto byte = static_cast<unsigned char>(value[index + offset]);
      if ((byte & 0xC0U) != 0x80U) {
        return false;
      }
      codepoint = (codepoint << 6U) | (byte & 0x3FU);
    }
    if ((trailing == 2 && codepoint < 0x800U) ||
        (trailing == 3 && codepoint < 0x10000U) ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU) ||
        codepoint > 0x10FFFFU) {
      return false;
    }
    index += trailing + 1;
  }
  return true;
}

auto valid_help_field(const std::string_view value) -> bool {
  return valid_utf8(value) &&
         std::ranges::all_of(value, [](const unsigned char byte) {
           return byte >= 0x20U && byte != 0x7FU;
         });
}

auto render_help_entries(const std::vector<CommandCatalogEntry> &catalog,
                         const std::size_t page_bytes,
                         const std::size_t maximum_pages)
    -> CommandHelpRenderResult {
  if (page_bytes == 0 || maximum_pages == 0) {
    return {.code = "command_help_bounds_invalid",
            .message = "command help bounds are invalid"};
  }
  CommandHelpRenderResult result;
  std::string page;
  for (const auto &entry : catalog) {
    if (!valid_help_field(entry.name) || !valid_help_field(entry.description)) {
      return {.code = "command_help_entry_invalid",
              .message = "command help entry is not valid plain UTF-8"};
    }
    const auto formatted = "/" + entry.name + " - " + entry.description + "\n";
    if (formatted.size() > page_bytes) {
      return {.code = "command_help_entry_too_large",
              .message = "command help entry exceeds the page bound"};
    }
    if (!page.empty() && page.size() + formatted.size() > page_bytes) {
      result.pages.push_back(std::move(page));
      page.clear();
    }
    page += formatted;
  }
  if (!page.empty()) {
    result.pages.push_back(std::move(page));
  }
  if (result.pages.size() > maximum_pages) {
    return {.code = "command_help_page_limit_exceeded",
            .message = "command help catalog exceeds the page-count bound"};
  }
  return result;
}

auto json_string(const common::json &document, const std::string_view key)
    -> std::string {
  if (!document.is_object() || !document.contains(key) ||
      !document.at(key).is_string()) {
    return {};
  }
  return document.at(key).get<std::string>();
}

auto valid_policy_value(const std::string_view value) -> bool {
  return value.size() <= 1024U &&
         std::ranges::all_of(value, [](const unsigned char byte) {
           return byte >= 0x20U && byte != 0x7FU;
         });
}

auto command_policy_subject(const MessageEnvelope &message,
                            const ActiveCommandBot &bot)
    -> std::optional<CommandPolicySubject> {
  if (message.source_platform != bot.key.platform ||
      message.source_bot != bot.key.bot || !message.payload.is_object()) {
    return std::nullopt;
  }
  const auto configured_bot = message.payload.find("source_bot_configured");
  if (configured_bot == message.payload.end() ||
      !configured_bot->is_boolean() || !configured_bot->get<bool>()) {
    return std::nullopt;
  }
  const auto field =
      [&message](const std::string_view key) -> std::optional<std::string> {
    const auto value = message.payload.find(key);
    if (value == message.payload.end() || !value->is_string()) {
      return std::nullopt;
    }
    auto result = value->get<std::string>();
    return valid_policy_value(result) ? std::optional{std::move(result)}
                                      : std::nullopt;
  };
  const auto kind = field("message_type");
  const auto sender = field("sender");
  const auto group = field("group_id");
  if (!kind || !sender || !group || sender->empty()) {
    return std::nullopt;
  }
  const auto chat = field("chat_id");
  if (message.payload.contains("chat_id") && !chat) {
    return std::nullopt;
  }
  const auto chat_id = chat.value_or(std::string{});

  std::optional<std::int64_t> topic;
  if (message.payload.contains("topic_id")) {
    const auto &value = message.payload.at("topic_id");
    if (bot.key.platform != "telegram" || !value.is_number_integer()) {
      return std::nullopt;
    }
    topic = value.get<std::int64_t>();
    if (*topic <= 0) {
      return std::nullopt;
    }
  }

  if (*kind == "group") {
    if (group->empty() || (!chat_id.empty() && chat_id != *group) ||
        (!group->empty() && message.conversation_id != "group:" + *group &&
         !(bot.key.platform == "telegram" &&
           message.conversation_id == "chat:" + *group))) {
      return std::nullopt;
    }
    return CommandPolicySubject{.platform = bot.key.platform,
                                .bot = bot.key.bot,
                                .conversation = CommandConversationKind::Group,
                                .group_id = *group,
                                .user_id = *sender,
                                .topic_id = topic};
  }
  if (*kind == "private") {
    if (!group->empty() || topic.has_value() ||
        (!chat_id.empty() && chat_id != *sender) ||
        (!sender->empty() && message.conversation_id != "private:" + *sender &&
         !(bot.key.platform == "telegram" &&
           message.conversation_id == "chat:" + *sender))) {
      return std::nullopt;
    }
    return CommandPolicySubject{
        .platform = bot.key.platform,
        .bot = bot.key.bot,
        .conversation = CommandConversationKind::Private,
        .group_id = {},
        .user_id = *sender,
    };
  }
  return std::nullopt;
}

auto message_field_value(const MessageEnvelope &message,
                         const std::string &field) -> std::string {
  if (field == "id") {
    return message.id;
  }
  if (field == "type") {
    return message.type;
  }
  if (field == "source_platform") {
    return message.source_platform;
  }
  if (field == "source_bot") {
    return message.source_bot;
  }
  if (field == "conversation_id") {
    return message.conversation_id;
  }
  if (field == "correlation_id") {
    return message.correlation_id;
  }
  if (field == "causation_id") {
    return message.causation_id;
  }
  if (const auto header = message.headers.find(field);
      header != message.headers.end()) {
    return header->second;
  }
  if (message.payload.is_object() && message.payload.contains(field)) {
    const auto &value = message.payload.at(field);
    if (value.is_string()) {
      return value.get<std::string>();
    }
    if (value.is_number_integer()) {
      return std::to_string(value.get<std::int64_t>());
    }
    if (value.is_number_unsigned()) {
      return std::to_string(value.get<std::uint64_t>());
    }
  }
  if (message.raw.is_object() && message.raw.contains(field)) {
    const auto &value = message.raw.at(field);
    if (value.is_string()) {
      return value.get<std::string>();
    }
    if (value.is_number_integer()) {
      return std::to_string(value.get<std::int64_t>());
    }
    if (value.is_number_unsigned()) {
      return std::to_string(value.get<std::uint64_t>());
    }
  }
  return {};
}

auto resolve_partition_key(const std::string &expression,
                           const MessageEnvelope &message) -> std::string {
  if (expression.empty() || expression == "global") {
    return "global";
  }
  std::string result;
  std::size_t start = 0;
  while (start <= expression.size()) {
    const auto separator = expression.find(':', start);
    const auto field = expression.substr(start, separator == std::string::npos
                                                    ? std::string::npos
                                                    : separator - start);
    if (!result.empty()) {
      result += ':';
    }
    result += message_field_value(message, field);
    if (separator == std::string::npos) {
      break;
    }
    start = separator + 1;
  }
  return result.empty() ? "global" : result;
}

enum class CommandCallStatus {
  Completed,
  TimedOut,
};

struct CommandCallResult {
  CommandCallStatus status = CommandCallStatus::Completed;
  ActorResult result;
};

template <typename Handler>
class TimedActorOperation
    : public std::enable_shared_from_this<TimedActorOperation<Handler>> {
public:
  TimedActorOperation(Handler handler, boost::asio::any_io_executor executor,
                      std::shared_ptr<NativeActorScheduler> scheduler,
                      ActorInvocation invocation,
                      const std::chrono::milliseconds timeout)
      : handler_(std::move(handler)), executor_(std::move(executor)),
        work_(boost::asio::make_work_guard(executor_)),
        scheduler_(std::move(scheduler)), invocation_(std::move(invocation)),
        timer_(executor_, timeout) {}

  void start() {
    auto self = this->shared_from_this();
    timer_.async_wait([self](const boost::system::error_code &error) {
      if (!error) {
        self->on_timeout();
      }
    });
    scheduler_->enqueue(
        invocation_, [self = std::move(self)](ActorResult result) mutable {
          auto executor = self->executor_;
          boost::asio::post(
              std::move(executor),
              [self = std::move(self), result = std::move(result)]() mutable {
                self->finish(CommandCallResult{
                    .status = CommandCallStatus::Completed,
                    .result = std::move(result),
                });
              });
        });
  }

private:
  void on_timeout() {
    scheduler_->cancel(invocation_.actor_id, invocation_.partition_key,
                       invocation_.message.id);
    finish(CommandCallResult{
        .status = CommandCallStatus::TimedOut,
        .result = ActorResult::failed(
            "command_timeout", "command actor invocation timed out", true),
    });
  }

  void finish(CommandCallResult result) {
    if (completed_) {
      return;
    }
    completed_ = true;
    try {
      timer_.cancel();
    } catch (...) {
    }
    auto handler = std::move(handler_);
    work_.reset();
    handler(std::move(result));
  }

  Handler handler_;
  boost::asio::any_io_executor executor_;
  boost::asio::executor_work_guard<boost::asio::any_io_executor> work_;
  std::shared_ptr<NativeActorScheduler> scheduler_;
  ActorInvocation invocation_;
  boost::asio::steady_timer timer_;
  bool completed_ = false;
};

template <typename CompletionToken>
auto async_invoke_with_timeout(std::shared_ptr<NativeActorScheduler> scheduler,
                               ActorInvocation invocation,
                               const std::chrono::milliseconds timeout,
                               CompletionToken &&token) {
  return boost::asio::async_initiate<CompletionToken, void(CommandCallResult)>(
      [scheduler = std::move(scheduler), invocation = std::move(invocation),
       timeout](auto &&handler) mutable {
        using handler_type = std::decay_t<decltype(handler)>;
        auto executor = boost::asio::any_io_executor{
            boost::asio::get_associated_executor(handler)};
        auto operation = std::make_shared<TimedActorOperation<handler_type>>(
            std::forward<decltype(handler)>(handler), std::move(executor),
            std::move(scheduler), std::move(invocation), timeout);
        operation->start();
      },
      token);
}

void merge_result(OrchestratorResult &target, OrchestratorResult source) {
  std::ranges::move(source.stages, std::back_inserter(target.stages));
  std::ranges::move(source.emitted, std::back_inserter(target.emitted));
  std::ranges::move(source.failures, std::back_inserter(target.failures));
}

void add_command_failure(OrchestratorResult &result,
                         const ActiveCommandRoute &route, std::string code,
                         std::string message, const bool retryable = false) {
  result.failures.push_back(OrchestratorFailure{
      .pipeline = "$command",
      .stage = route.key.command,
      .actor = route.actor,
      .failure =
          ActorFailure{
              .code = std::move(code),
              .message = std::move(message),
              .retryable = retryable,
          },
  });
}

void add_command_match_failure(OrchestratorResult &result, std::string code,
                               std::string message) {
  result.failures.push_back(OrchestratorFailure{
      .pipeline = "$command",
      .stage = "$match",
      .actor = {},
      .failure =
          ActorFailure{
              .code = std::move(code),
              .message = std::move(message),
              .retryable = false,
          },
  });
}

void add_core_command_failure(OrchestratorResult &result,
                              const std::string_view command, std::string code,
                              std::string message) {
  result.failures.push_back(OrchestratorFailure{
      .pipeline = "$command",
      .stage = std::string{command},
      .actor = {},
      .failure = ActorFailure{.code = std::move(code),
                              .message = std::move(message),
                              .retryable = false},
  });
}

struct CommandRouteMatch {
  const ActiveCommandRoute *route = nullptr;
  bool ambiguous = false;
};

auto match_command_route(const CommandRoutingTable &table,
                         const ActiveCommandBot &bot,
                         const std::string_view candidate)
    -> CommandRouteMatch {
  const auto *exact = table.find_route(CommandRouteKey{
      .platform = bot.key.platform,
      .bot = bot.key.bot,
      .command = std::string{candidate},
  });
  if (exact != nullptr) {
    return {.route = exact};
  }

  const ActiveCommandPattern *matched = nullptr;
  for (const auto &pattern : bot.patterns) {
    if (!command_re2_full_match(*pattern.compiled, candidate)) {
      continue;
    }
    if (matched != nullptr) {
      return {.ambiguous = true};
    }
    matched = &pattern;
  }
  if (matched == nullptr) {
    return {};
  }
  return {
      .route = table.find_route(CommandRouteKey{
          .platform = bot.key.platform,
          .bot = bot.key.bot,
          .command = matched->command,
      }),
  };
}

auto header_is(const MessageEnvelope &message, const std::string_view key,
               const std::string_view expected) -> bool {
  const auto value = message.headers.find(std::string{key});
  return value != message.headers.end() && value->second == expected;
}

void mark_processed(MessageEnvelope &message, const ActiveCommandRoute &route,
                    const std::string &transaction,
                    const std::uint64_t generation_id,
                    const std::string_view outcome) {
  message.headers.insert_or_assign(std::string{command_processed_header},
                                   "true");
  message.headers.insert_or_assign(std::string{command_name_header},
                                   route.key.command);
  message.headers.insert_or_assign(std::string{command_actor_header},
                                   route.actor);
  message.headers.insert_or_assign(std::string{command_transaction_header},
                                   transaction);
  message.headers.insert_or_assign(std::string{command_generation_header},
                                   std::to_string(generation_id));
  message.headers.insert_or_assign(std::string{command_outcome_header},
                                   std::string{outcome});
}

} // namespace

auto normalize_command_platform(std::string platform) -> std::string {
  std::ranges::transform(platform, platform.begin(),
                         [](const unsigned char character) {
                           return static_cast<char>(std::tolower(character));
                         });
  return platform;
}

auto CommandRoutingTable::empty() const noexcept -> bool {
  return routes_.empty();
}

auto CommandRoutingTable::find_route(const CommandRouteKey &key) const noexcept
    -> const ActiveCommandRoute * {
  const auto route = routes_.find(key);
  return route == routes_.end() ? nullptr : &route->second;
}

auto CommandRoutingTable::find_bot(const CommandBotKey &key) const noexcept
    -> const ActiveCommandBot * {
  const auto bot = bots_.find(key);
  return bot == bots_.end() ? nullptr : &bot->second;
}

auto CommandRoutingTable::routes() const noexcept
    -> const std::map<CommandRouteKey, ActiveCommandRoute> & {
  return routes_;
}

auto CommandRoutingTable::bots() const noexcept
    -> const std::map<CommandBotKey, ActiveCommandBot> & {
  return bots_;
}

auto CommandRoutingTable::message_observers(const CommandBotKey &key)
    const noexcept -> const std::vector<ActiveCommandMessageObserver> & {
  static const std::vector<ActiveCommandMessageObserver> empty;
  const auto observers = message_observers_.find(key);
  return observers == message_observers_.end() ? empty : observers->second;
}

auto CommandRoutingTable::policy_for(const std::string_view canonical_command)
    const -> const ActiveCommandPolicy & {
  const auto override_policy =
      policy_overrides_.find(std::string{canonical_command});
  if (override_policy != policy_overrides_.end()) {
    return override_policy->second;
  }
  if (!global_policy_) {
    throw std::logic_error("command routing table has no access policy");
  }
  return *global_policy_;
}

auto CommandRoutingTable::permits(const std::string_view canonical_command,
                                  const CommandPolicySubject &subject) const
    -> bool {
  const auto &policy = policy_for(canonical_command);
  const auto evaluate = [&subject](const ActiveCommandAccessPolicy &dimension,
                                   const std::string &native_id) {
    if (dimension.mode == common::CommandAccessMode::Unrestricted) {
      return true;
    }
    if (native_id.empty()) {
      return false;
    }
    const auto present = dimension.entries.contains(CommandAccessIdentity{
        .platform = subject.platform,
        .bot = subject.bot,
        .native_id = native_id,
    });
    return dimension.mode == common::CommandAccessMode::Allowlist ? present
                                                                  : !present;
  };
  const auto user_allowed = evaluate(policy.users, subject.user_id);
  if (subject.conversation == CommandConversationKind::Private) {
    return user_allowed;
  }
  return evaluate(policy.groups, subject.group_id) && user_allowed;
}

auto CommandRoutingTable::render_help(const ActiveCommandBot &bot,
                                      const CommandPolicySubject &subject) const
    -> CommandHelpRenderResult {
  std::vector<CommandCatalogEntry> permitted;
  permitted.reserve(bot.catalog.size());
  for (const auto &entry : bot.catalog) {
    if (permits(entry.name, subject)) {
      permitted.push_back(entry);
    }
  }
  return render_help_entries(permitted, help_page_bytes_, help_maximum_pages_);
}

auto CommandRoutingTable::help_page_bytes() const noexcept -> std::size_t {
  return help_page_bytes_;
}

auto CommandRoutingTable::help_maximum_pages() const noexcept -> std::size_t {
  return help_maximum_pages_;
}

auto build_command_routing_table(
    const common::RuntimeConfigSnapshot &snapshot,
    const std::unordered_map<std::string, ActorInputContract> &contracts)
    -> CommandRoutingBuildResult {
  auto table = std::make_shared<CommandRoutingTable>();
  const auto runtime = snapshot.get_command_runtime_config();
  if (!runtime.routes.empty()) {
    if (!runtime.help.page_bytes || !runtime.help.maximum_pages) {
      return command_failure(
          "command_help_configuration_missing",
          "active command routes require explicit help bounds");
    }
    table->help_page_bytes_ = *runtime.help.page_bytes;
    table->help_maximum_pages_ = *runtime.help.maximum_pages;
    table->global_policy_ =
        compile_command_policy(runtime.access.groups, runtime.access.users);
    if (!table->global_policy_) {
      return command_failure(
          "command_access_configuration_invalid",
          "active command routes require complete explicit access policies");
    }
    for (const auto &[actor, contract] : contracts) {
      (void)actor;
      if (std::ranges::any_of(contract.commands, [](const auto &registration) {
            return registration.name == command::help_name;
          })) {
        return command_failure(
            "command_reserved_name",
            "actor command contract uses reserved name help");
      }
    }
  }

  std::unordered_map<std::string, common::ActorConfig> actors;
  for (const auto &actor : snapshot.get_actor_configs()) {
    if (actor.enabled) {
      actors.emplace(actor.name, actor);
    }
  }
  std::unordered_map<std::string, common::BotInstallationMetadata> bots;
  std::unordered_map<std::string, std::shared_ptr<ICommandPlatformAdapter>>
      adapters;
  std::unordered_map<std::string, std::vector<bot::ActionId>> bot_actions;
  for (const auto &plan : ProcessConfigAccess::plans(snapshot)) {
    const auto &bot = plan->metadata();
    if (bot.enabled) {
      bots.emplace(bot.installation_id, bot);
      adapters.emplace(bot.installation_id, plan->command_adapter());
      bot_actions.emplace(bot.installation_id,
                          plan->recipe().advertised_actions);
    }
  }

  const auto raw_message_type =
      std::string{canonical_message_type_name<events::RawMessageEvent>()};
  for (const auto &configured_observer : runtime.message_observers) {
    if (configured_observer.actor.empty() ||
        configured_observer.platforms.empty() ||
        configured_observer.bots.empty()) {
      return command_failure(
          "command_message_observer_invalid",
          "command message observer requires explicit actor, platforms, and "
          "bots");
    }
    const auto actor = actors.find(configured_observer.actor);
    const auto contract = contracts.find(configured_observer.actor);
    if (actor == actors.end() || contract == contracts.end()) {
      return command_failure(
          "command_message_observer_actor_unavailable",
          "command message observer references a missing or disabled actor: " +
              configured_observer.actor);
    }
    if (!contract->second.accepted_input_set.contains(raw_message_type)) {
      return command_failure(
          "command_message_observer_input_unsupported",
          "command message observer actor does not accept RawMessageEvent: " +
              configured_observer.actor);
    }
    if (configured_observer.timeout_ms <
            common::CommandRuntimeConfig::min_timeout_ms ||
        configured_observer.timeout_ms >
            common::CommandRuntimeConfig::max_timeout_ms) {
      return command_failure(
          "command_message_observer_timeout_invalid",
          "command message observer timeout is outside the supported range");
    }

    std::set<std::string> platforms;
    for (const auto &configured_platform : configured_observer.platforms) {
      auto platform = normalize_command_platform(configured_platform);
      if (!platforms.emplace(platform).second) {
        return command_failure(
            "command_message_observer_scope_duplicate",
            "command message observer repeats a platform scope: " + platform);
      }
      if (!ProcessConfigAccess::catalog(snapshot)->supports_ingress(platform)) {
        return command_failure(
            "command_message_observer_platform_unavailable",
            "command message observer platform has no ingress adapter: " +
                platform);
      }
    }

    std::set<std::string> selected_bots;
    std::set<std::string> covered_platforms;
    for (const auto &bot_name : configured_observer.bots) {
      if (!selected_bots.emplace(bot_name).second) {
        return command_failure(
            "command_message_observer_scope_duplicate",
            "command message observer repeats a bot scope: " + bot_name);
      }
      const auto configured_bot = bots.find(bot_name);
      if (configured_bot == bots.end()) {
        return command_failure(
            "command_message_observer_bot_unavailable",
            "command message observer references a missing or disabled bot: " +
                bot_name);
      }
      const auto &platform = configured_bot->second.ingress_platform;
      if (!platforms.contains(platform)) {
        return command_failure(
            "command_message_observer_bot_platform_mismatch",
            "command message observer bot platform is outside the configured "
            "scope: " +
                bot_name);
      }
      covered_platforms.emplace(platform);
      const CommandBotKey key{.platform = platform, .bot = bot_name};
      auto &active = table->message_observers_[key];
      if (std::ranges::any_of(active, [&](const auto &observer) {
            return observer.actor == configured_observer.actor;
          })) {
        return command_failure(
            "command_message_observer_conflict",
            "command message observer actor is duplicated for a bot scope: " +
                configured_observer.actor + ":" + bot_name);
      }
      active.push_back(ActiveCommandMessageObserver{
          .key = key,
          .actor = configured_observer.actor,
          .partition_expression = actor->second.partition,
          .db_instance = actor->second.db,
          .db_namespace = actor->second.db_namespace,
          .timeout = std::chrono::milliseconds{configured_observer.timeout_ms},
      });
    }
    if (covered_platforms != platforms) {
      return command_failure(
          "command_message_observer_platform_scope_empty",
          "each command message observer platform must have a selected bot");
    }
  }

  for (const auto &configured_route : runtime.routes) {
    const auto actor = actors.find(configured_route.actor);
    const auto contract = contracts.find(configured_route.actor);
    if (actor == actors.end() || contract == contracts.end()) {
      return command_failure(
          "command_actor_unavailable",
          "command route references a missing or disabled actor: " +
              configured_route.actor);
    }

    std::set<std::string> platforms;
    for (const auto &configured_platform : configured_route.platforms) {
      auto platform = normalize_command_platform(configured_platform);
      if (!platforms.emplace(platform).second) {
        return command_failure("command_route_scope_duplicate",
                               "command route repeats a platform scope: " +
                                   platform);
      }
      if (!ProcessConfigAccess::catalog(snapshot)->supports_ingress(platform)) {
        return command_failure("command_platform_adapter_unavailable",
                               "command route platform has no adapter: " +
                                   platform);
      }
    }

    std::set<std::string> commands;
    for (const auto &command : configured_route.commands) {
      if (!commands.emplace(command).second) {
        return command_failure("command_route_scope_duplicate",
                               "command route repeats a command scope: " +
                                   command);
      }
      if (command == command::help_name) {
        return command_failure("command_reserved_name",
                               "command route uses reserved name help");
      }
      const auto *registration = find_registration(contract->second, command);
      if (registration == nullptr) {
        return command_failure(
            "command_not_declared",
            "command route references an undeclared actor command: " +
                configured_route.actor + ":" + command);
      }
      if (!contract->second.accepted_input_set.contains(
              registration->request_type)) {
        return command_failure(
            "command_request_unsupported",
            "command request type is absent from the actor input contract: " +
                registration->request_type);
      }
    }

    std::set<std::string> selected_bots;
    std::set<std::string> covered_platforms;
    for (const auto &bot_name : configured_route.bots) {
      if (!selected_bots.emplace(bot_name).second) {
        return command_failure("command_route_scope_duplicate",
                               "command route repeats a bot scope: " +
                                   bot_name);
      }
      const auto configured_bot = bots.find(bot_name);
      if (configured_bot == bots.end()) {
        return command_failure(
            "command_bot_unavailable",
            "command route references a missing or disabled bot: " + bot_name);
      }
      const auto &platform = configured_bot->second.ingress_platform;
      if (!platforms.contains(platform)) {
        return command_failure(
            "command_bot_platform_mismatch",
            "command route bot platform is outside the configured scope: " +
                bot_name);
      }
      auto adapter = adapters.at(bot_name);
      if (!adapter) {
        return command_failure(
            "command_platform_adapter_unavailable",
            "configured bot platform has no command adapter: " + platform);
      }
      const auto &actions = bot_actions.at(bot_name);
      const auto supports = [&actions](const std::string_view action) {
        return std::ranges::find(actions, bot::ActionId{std::string{action}}) !=
               actions.end();
      };
      if (!supports("message.send_group") ||
          !supports("message.send_private") ||
          (platform == "telegram" &&
           !supports("telegram.message.send_topic"))) {
        return command_failure(
            "command_reply_capability_unavailable",
            "configured bot cannot provide every typed command reply action");
      }
      covered_platforms.emplace(platform);

      const CommandBotKey bot_key{.platform = platform, .bot = bot_name};
      auto [active_bot, inserted] = table->bots_.try_emplace(
          bot_key,
          ActiveCommandBot{
              .key = bot_key,
              .installation = {bot_name, configured_bot->second.surface},
              .target = configured_bot->second.command_target,
              .adapter = std::move(adapter),
          });
      if (!inserted &&
          active_bot->second.target != configured_bot->second.command_target) {
        return command_failure("command_bot_metadata_conflict",
                               "command bot target metadata is inconsistent");
      }

      for (const auto &command : commands) {
        const auto *registration = find_registration(contract->second, command);
        const CommandRouteKey key{
            .platform = platform,
            .bot = bot_name,
            .command = command,
        };
        ActiveCommandRoute route{
            .key = key,
            .actor = configured_route.actor,
            .request_type = registration->request_type,
            .description = registration->description,
            .partition_expression = actor->second.partition,
            .db_instance = actor->second.db,
            .db_namespace = actor->second.db_namespace,
            .fallback = configured_route.fallback,
            .timeout = command_timeout(runtime, configured_route),
        };
        if (!table->routes_.emplace(key, std::move(route)).second) {
          return command_failure(
              "command_route_conflict",
              "multiple command routes own the same platform, bot, and "
              "command scope: " +
                  platform + ":" + bot_name + ":" + command);
        }
        if (registration->matcher) {
          if (registration->matcher->kind != "re2" ||
              registration->matcher->mode != "full") {
            return command_failure(
                "command_matcher_unsupported",
                "active command matcher kind or mode is unsupported");
          }
          const auto duplicate_pattern = std::ranges::find(
              active_bot->second.patterns, registration->matcher->pattern,
              &ActiveCommandPattern::expression);
          if (duplicate_pattern != active_bot->second.patterns.end()) {
            return command_failure(
                "command_pattern_conflict",
                "multiple command routes declare the same RE2 pattern in one "
                "bot scope");
          }
          auto compiled = compile_command_re2(registration->matcher->pattern);
          if (!compiled) {
            return command_failure(std::move(compiled.code),
                                   std::move(compiled.message));
          }
          active_bot->second.patterns.push_back(ActiveCommandPattern{
              .command = command,
              .expression = registration->matcher->pattern,
              .compiled = std::move(compiled.compiled),
          });
        }
        active_bot->second.catalog.push_back(CommandCatalogEntry{
            .name = command,
            .description = registration->description,
        });
      }
    }
    if (covered_platforms != platforms) {
      return command_failure(
          "command_platform_scope_empty",
          "each command route platform must have a selected bot");
    }
  }

  std::set<std::string> active_commands{std::string{command::help_name}};
  for (const auto &[key, route] : table->routes_) {
    (void)key;
    active_commands.insert(route.key.command);
  }
  for (const auto &configured : runtime.access.overrides) {
    if (!active_commands.contains(configured.command)) {
      return command_failure(
          "command_access_override_inactive",
          "command access override does not name an active canonical command");
    }
    auto policy = compile_command_policy(configured.groups, configured.users);
    if (!policy) {
      return command_failure("command_access_override_invalid",
                             "command access override is incomplete");
    }
    if (!table->policy_overrides_
             .emplace(configured.command, std::move(*policy))
             .second) {
      return command_failure("command_access_override_duplicate",
                             "command access override names are not unique");
    }
  }

  for (auto &[key, bot] : table->bots_) {
    (void)key;
    bot.catalog.push_back(CommandCatalogEntry{
        .name = std::string{command::help_name},
        .description = std::string{command::help_description},
    });
    std::ranges::sort(bot.catalog, {}, &CommandCatalogEntry::name);
    const auto duplicate =
        std::ranges::adjacent_find(bot.catalog, {}, &CommandCatalogEntry::name);
    if (duplicate != bot.catalog.end()) {
      return command_failure("command_route_conflict",
                             "aggregate command catalog contains duplicates");
    }
    if (const auto error = bot.adapter->validate_catalog(bot.catalog)) {
      return command_failure("command_catalog_invalid", *error);
    }
    const auto rendered = render_help_entries(
        bot.catalog, table->help_page_bytes_, table->help_maximum_pages_);
    if (!rendered) {
      return command_failure(rendered.code, rendered.message);
    }
    std::ranges::sort(bot.patterns, {}, &ActiveCommandPattern::command);
  }
  return {.table = std::move(table)};
}

CommandCoordinator::CommandCoordinator(
    const std::uint64_t generation_id,
    std::shared_ptr<const CommandRoutingTable> routing_table,
    std::shared_ptr<NativeActorScheduler> scheduler,
    std::shared_ptr<Orchestrator> orchestrator,
    std::shared_ptr<bot::BotOperationGateway> operation_gateway)
    : generation_id_(generation_id), routing_table_(std::move(routing_table)),
      scheduler_(std::move(scheduler)), orchestrator_(std::move(orchestrator)),
      operation_gateway_(std::move(operation_gateway)) {
  if (!routing_table_ || !scheduler_ || !orchestrator_ || !operation_gateway_) {
    throw std::invalid_argument(
        "CommandCoordinator requires routing, "
        "scheduler, orchestrator, and operation gateway");
  }
}

auto CommandCoordinator::observe_message(const MessageEnvelope &message)
    -> boost::asio::awaitable<OrchestratorResult> {
  OrchestratorResult result;
  const auto &observers = routing_table_->message_observers(CommandBotKey{
      .platform = normalize_command_platform(message.source_platform),
      .bot = message.source_bot,
  });
  for (const auto &observer : observers) {
    result.stages.push_back(OrchestratorStageExecution{
        .pipeline = "$message_observer",
        .name = "observe",
        .actor = observer.actor,
        .input = message.type,
        .mode = "await",
        .partition_key =
            resolve_partition_key(observer.partition_expression, message),
        .terminal_async = false,
    });
    auto call = co_await async_invoke_with_timeout(
        scheduler_,
        ActorInvocation{
            .actor_id = observer.actor,
            .partition_key = result.stages.back().partition_key,
            .db_instance = observer.db_instance,
            .db_namespace = observer.db_namespace,
            .message = message,
        },
        observer.timeout, boost::asio::use_awaitable);
    if (call.status == CommandCallStatus::TimedOut) {
      result.failures.push_back(OrchestratorFailure{
          .pipeline = "$message_observer",
          .stage = "observe",
          .actor = observer.actor,
          .failure =
              ActorFailure{
                  .code = "message_observer_timeout",
                  .message = "message observer actor invocation timed out",
                  .retryable = true,
              },
      });
      continue;
    }
    if (!call.result.ok()) {
      result.failures.push_back(OrchestratorFailure{
          .pipeline = "$message_observer",
          .stage = "observe",
          .actor = observer.actor,
          .failure = *call.result.failure,
      });
      continue;
    }
    if (!call.result.emitted.empty()) {
      result.failures.push_back(OrchestratorFailure{
          .pipeline = "$message_observer",
          .stage = "observe",
          .actor = observer.actor,
          .failure =
              ActorFailure{
                  .code = "message_observer_emission_unsupported",
                  .message = "message observer actor must not emit messages",
                  .retryable = false,
              },
      });
    }
  }
  co_return result;
}

auto CommandCoordinator::process(MessageEnvelope message,
                                 std::shared_ptr<void> route_lifetime)
    -> boost::asio::awaitable<OrchestratorResult> {
  const auto raw_message_type =
      canonical_message_type_name<events::RawMessageEvent>();
  if (shutdown_.load(std::memory_order_acquire) ||
      message.type != raw_message_type ||
      message.headers.contains(std::string{command_processed_header})) {
    co_return co_await orchestrator_->process(std::move(message),
                                              std::move(route_lifetime));
  }

  auto result = co_await observe_message(message);
  const CommandBotKey bot_key{
      .platform = normalize_command_platform(message.source_platform),
      .bot = message.source_bot,
  };
  const auto *bot = routing_table_->find_bot(bot_key);
  if (bot == nullptr) {
    auto continued = co_await orchestrator_->process(std::move(message),
                                                     std::move(route_lifetime));
    merge_result(result, std::move(continued));
    co_return result;
  }
  const auto detected = bot->adapter->detect(message);
  if (!detected) {
    auto continued = co_await orchestrator_->process(std::move(message),
                                                     std::move(route_lifetime));
    merge_result(result, std::move(continued));
    co_return result;
  }
  const auto subject = command_policy_subject(message, *bot);
  if (detected->name == command::help_name) {
    if (!subject || !routing_table_->permits(command::help_name, *subject)) {
      add_core_command_failure(result, command::help_name,
                               "command_access_denied",
                               "command access was denied");
      co_return result;
    }
    if (!detected->arguments.empty()) {
      add_core_command_failure(result, command::help_name,
                               "invalid_help_arguments",
                               "help does not accept arguments");
      co_return result;
    }
    const auto rendered = routing_table_->render_help(*bot, *subject);
    if (!rendered) {
      add_core_command_failure(result, command::help_name, rendered.code,
                               rendered.message);
      co_return result;
    }
    for (const auto &page : rendered.pages) {
      if (shutdown_.load(std::memory_order_acquire)) {
        add_core_command_failure(result, command::help_name,
                                 "command_cancelled",
                                 "help delivery was cancelled");
        co_return result;
      }
      auto reply = bot->adapter->build_text_reply(message, page);
      if (!reply) {
        add_core_command_failure(result, command::help_name,
                                 std::move(reply.code),
                                 std::move(reply.message));
        co_return result;
      }
      try {
        auto delivered =
            co_await operation_gateway_->invoke(std::move(*reply.operation));
        delivered.validate();
        if (!delivered.ok()) {
          const auto uncertain = delivered.error->submission_safety ==
                                 obcx::bot::SubmissionSafety::PossiblySubmitted;
          add_core_command_failure(result, command::help_name,
                                   uncertain ? "command_help_delivery_uncertain"
                                             : "command_help_delivery_failed",
                                   uncertain
                                       ? "help delivery outcome is uncertain"
                                       : "help delivery failed");
          co_return result;
        }
      } catch (...) {
        add_core_command_failure(result, command::help_name,
                                 "command_help_delivery_uncertain",
                                 "help delivery outcome is uncertain");
        co_return result;
      }
    }
    co_return result;
  }

  const auto matched =
      match_command_route(*routing_table_, *bot, detected->name);
  if (matched.ambiguous) {
    add_command_match_failure(
        result, "command_match_ambiguous",
        "command candidate matched multiple active command patterns");
    auto continued = co_await orchestrator_->process(std::move(message),
                                                     std::move(route_lifetime));
    merge_result(result, std::move(continued));
    co_return result;
  }
  const auto *route = matched.route;
  if (route == nullptr) {
    auto continued = co_await orchestrator_->process(std::move(message),
                                                     std::move(route_lifetime));
    merge_result(result, std::move(continued));
    co_return result;
  }
  if (!subject || !routing_table_->permits(route->key.command, *subject)) {
    add_core_command_failure(result, route->key.command,
                             "command_access_denied",
                             "command access was denied");
    co_return result;
  }

  const auto transaction =
      "g" + std::to_string(generation_id_) + ":" +
      std::to_string(next_transaction_.fetch_add(1, std::memory_order_relaxed));
  command::CommandInvocation invocation{
      .transaction_id = transaction,
      .name = route->key.command,
      .arguments = detected->arguments,
      .source_message_id = message.id,
      .source_platform = message.source_platform,
      .source_bot = message.source_bot,
      .conversation_id = message.conversation_id,
      .sender = json_string(message.payload, "sender"),
      .source_event = message.payload,
  };
  MessageEnvelope request;
  request.id = message.id + ":command:" + transaction;
  request.type = route->request_type;
  request.source_platform = message.source_platform;
  request.source_bot = message.source_bot;
  request.conversation_id = message.conversation_id;
  request.correlation_id =
      message.correlation_id.empty() ? message.id : message.correlation_id;
  request.causation_id = message.id;
  request.timestamp = message.timestamp;
  request.payload = {{"invocation", std::move(invocation)}};
  request.raw = message.raw;
  request.headers = message.headers;
  request.headers.insert_or_assign(std::string{command_transaction_header},
                                   transaction);
  request.headers.insert_or_assign(std::string{command_name_header},
                                   route->key.command);
  request.headers.insert_or_assign(std::string{command_actor_header},
                                   route->actor);
  request.headers.insert_or_assign(std::string{command_generation_header},
                                   std::to_string(generation_id_));
  request.headers.insert_or_assign(std::string{command_reply_header},
                                   "coordinator");

  auto call = co_await async_invoke_with_timeout(
      scheduler_,
      ActorInvocation{
          .actor_id = route->actor,
          .partition_key =
              resolve_partition_key(route->partition_expression, message),
          .db_instance = route->db_instance,
          .db_namespace = route->db_namespace,
          .message = std::move(request),
      },
      route->timeout, boost::asio::use_awaitable);

  std::vector<MessageEnvelope> completions;
  for (auto &emitted : call.result.emitted) {
    if (emitted.type ==
        canonical_message_type_name<command::CommandCompleted>()) {
      completions.push_back(std::move(emitted));
      continue;
    }
    auto routed = co_await orchestrator_->process(emitted, route_lifetime);
    result.emitted.push_back(std::move(emitted));
    merge_result(result, std::move(routed));
  }

  std::optional<command::Propagation> propagation;
  std::string failure_code;
  std::string failure_message;
  bool failure_retryable = false;
  if (call.status == CommandCallStatus::TimedOut) {
    failure_code = "command_timeout";
    failure_message = "command actor invocation timed out";
    failure_retryable = true;
  } else if (!call.result.ok()) {
    failure_code = "command_actor_failure";
    failure_message = "command actor invocation failed";
    failure_retryable = call.result.failure->retryable;
  } else if (completions.empty()) {
    failure_code = "command_completion_missing";
    failure_message = "command actor returned without a completion";
  } else if (completions.size() != 1) {
    failure_code = "command_completion_duplicate";
    failure_message = "command actor returned multiple completions";
  } else {
    try {
      const auto completion =
          completions.front().payload.get<command::CommandCompleted>();
      const auto generation = std::to_string(generation_id_);
      if (completion.transaction_id != transaction ||
          !header_is(completions.front(), command_transaction_header,
                     transaction) ||
          !header_is(completions.front(), command_actor_header, route->actor) ||
          !header_is(completions.front(), command_generation_header,
                     generation) ||
          !header_is(completions.front(), command_reply_header,
                     "coordinator")) {
        failure_code = "command_completion_mismatch";
        failure_message =
            "command completion correlation or ownership is invalid";
      } else {
        propagation = completion.propagation;
      }
    } catch (...) {
      failure_code = "command_completion_malformed";
      failure_message = "command completion payload is malformed";
    }
  }

  if (!failure_code.empty()) {
    add_command_failure(result, *route, failure_code, failure_message,
                        failure_retryable);
    propagation = route->fallback == common::CommandFallback::Continue
                      ? command::Propagation::Continue
                      : command::Propagation::Consume;
  }

  if (*propagation == command::Propagation::Continue) {
    mark_processed(message, *route, transaction, generation_id_,
                   failure_code.empty() ? "continue" : "fallback_continue");
    auto continued = co_await orchestrator_->process(std::move(message),
                                                     std::move(route_lifetime));
    merge_result(result, std::move(continued));
  }
  co_return result;
}

void CommandCoordinator::shutdown() noexcept {
  shutdown_.store(true, std::memory_order_release);
}

} // namespace obcx::core
