#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace mc_rcon {

/**
 * @brief C++ wrapper for RCON client
 *
 * Provides a simple interface for connecting to and executing
 * commands on a Minecraft RCON server.
 */
class RconClient {
public:
  RconClient() = default;
  ~RconClient();

  // Non-copyable
  RconClient(const RconClient &) = delete;
  RconClient &operator=(const RconClient &) = delete;

  // Movable
  RconClient(RconClient &&other) noexcept;
  RconClient &operator=(RconClient &&other) noexcept;

  /**
   * @brief Connect to an RCON server
   * @param host Server hostname or IP
   * @param port Server port
   * @return true on success
   */
  bool connect(const std::string &host, uint16_t port);

  /**
   * @brief Authenticate with the RCON server
   * @param password RCON password
   * @return true on success
   */
  bool authenticate(const std::string &password);

  /**
   * @brief Execute a command and get the response
   * @param command Command to execute
   * @return Response string on success, nullopt on failure
   */
  std::optional<std::string> execute(const std::string &command);

  /**
   * @brief Close the connection
   */
  void disconnect();

  /**
   * @brief Check if connected
   * @return true if connected
   */
  bool is_connected() const { return socket_ >= 0; }

private:
  int socket_ = -1;
};

/**
 * @brief Execute a single RCON command (connect, auth, execute, disconnect)
 *
 * Convenience function for one-off commands. Opens a new connection,
 * authenticates, executes the command, and closes the connection.
 *
 * @param host Server hostname or IP
 * @param port Server port
 * @param password RCON password
 * @param command Command to execute
 * @return Response string on success, nullopt on failure
 */
std::optional<std::string> execute_rcon_command(const std::string &host,
                                                uint16_t port,
                                                const std::string &password,
                                                const std::string &command);

} // namespace mc_rcon
