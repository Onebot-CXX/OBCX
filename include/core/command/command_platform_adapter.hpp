#ifndef OBCX_INCLUDE_CORE_COMMAND_PLATFORM_ADAPTER_HPP_
#define OBCX_INCLUDE_CORE_COMMAND_PLATFORM_ADAPTER_HPP_

#include "core/bot/operation_gateway.hpp"

#include <boost/asio/awaitable.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace obcx::core {

struct MessageEnvelope;

struct DetectedCommand {
  std::string name;
  std::string arguments;
};

struct CommandCatalogEntry {
  std::string name;
  std::string description;

  auto operator==(const CommandCatalogEntry &) const -> bool = default;
};

struct CommandCatalogPublishResult {
  bool supported = false;
  bool succeeded = false;
  std::string code;
  std::string message;
};

struct CommandReplyBuildResult {
  std::optional<bot::OperationEnvelope> operation;
  std::string code;
  std::string message;

  explicit operator bool() const noexcept { return operation.has_value(); }
};

class CommandCatalogPublisher {
public:
  CommandCatalogPublisher() = default;
  CommandCatalogPublisher(const CommandCatalogPublisher &) = delete;
  auto operator=(const CommandCatalogPublisher &)
      -> CommandCatalogPublisher & = delete;
  CommandCatalogPublisher(CommandCatalogPublisher &&) = delete;
  auto operator=(CommandCatalogPublisher &&)
      -> CommandCatalogPublisher & = delete;
  virtual ~CommandCatalogPublisher() = default;

  virtual auto publish(const std::vector<CommandCatalogEntry> &catalog)
      -> boost::asio::awaitable<CommandCatalogPublishResult> = 0;
};

class ICommandPlatformAdapter {
public:
  ICommandPlatformAdapter() = default;
  ICommandPlatformAdapter(const ICommandPlatformAdapter &) = delete;
  auto operator=(const ICommandPlatformAdapter &)
      -> ICommandPlatformAdapter & = delete;
  ICommandPlatformAdapter(ICommandPlatformAdapter &&) = delete;
  auto operator=(ICommandPlatformAdapter &&)
      -> ICommandPlatformAdapter & = delete;
  virtual ~ICommandPlatformAdapter() = default;

  [[nodiscard]] virtual auto platform() const noexcept -> std::string_view = 0;
  [[nodiscard]] virtual auto detect(const MessageEnvelope &event) const
      -> std::optional<DetectedCommand> = 0;
  [[nodiscard]] virtual auto validate_catalog(
      const std::vector<CommandCatalogEntry> &catalog) const
      -> std::optional<std::string> = 0;
  [[nodiscard]] virtual auto supports_catalog_publication() const noexcept
      -> bool = 0;
  [[nodiscard]] virtual auto build_text_reply(const MessageEnvelope &event,
                                              std::string text) const
      -> CommandReplyBuildResult = 0;
  virtual auto publish_catalog(CommandCatalogPublisher *catalog,
                               const std::vector<CommandCatalogEntry> &entries)
      -> boost::asio::awaitable<CommandCatalogPublishResult> = 0;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_COMMAND_PLATFORM_ADAPTER_HPP_
