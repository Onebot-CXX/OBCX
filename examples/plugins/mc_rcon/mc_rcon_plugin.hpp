#pragma once

#include <interfaces/plugin.hpp>
#include <string>

namespace mc_rcon {

/**
 * @brief Minecraft RCON Plugin
 *
 * Allows QQ users to execute Minecraft server commands via RCON.
 * Features:
 * - Multi-server support
 * - Permission control (admin users can run any command)
 * - Command whitelist for regular users
 * - Group-based access control
 */
class McRconPlugin : public obcx::interface::IPlugin {
public:
  McRconPlugin();
  ~McRconPlugin() override;

  // IPlugin interface
  [[nodiscard]] auto get_name() const -> std::string override;
  [[nodiscard]] auto get_version() const -> std::string override;
  [[nodiscard]] auto get_description() const -> std::string override;

  auto initialize() -> bool override;
  void deinitialize() override;
  void shutdown() override;

private:
  auto load_configuration() -> bool;

  /**
   * @brief Handle incoming QQ group message
   */
  auto handle_qq_message(obcx::core::IBot &bot,
                         const obcx::common::MessageEvent &event)
      -> boost::asio::awaitable<void>;

  /**
   * @brief Parse /mc command and extract server name and command
   * @param raw_message Raw message starting with /mc
   * @return Pair of (server_name, command). If server not specified, returns
   * default server.
   */
  std::pair<std::string, std::string> parse_mc_command(
      const std::string &raw_message);

  /**
   * @brief Execute RCON command and get response
   * @param server_name Server name from config
   * @param command Command to execute
   * @return Response string or error message
   */
  std::string execute_command(const std::string &server_name,
                              const std::string &command);

  /**
   * @brief Send response message to QQ group
   */
  auto send_response(obcx::core::IBot &bot, const std::string &group_id,
                     const std::string &response)
      -> boost::asio::awaitable<void>;
};

} // namespace mc_rcon
