#include "rcon_client.hpp"
#include "mcrcon.h"

#include <cstring>

namespace mc_rcon {

RconClient::~RconClient() { disconnect(); }

RconClient::RconClient(RconClient &&other) noexcept : socket_(other.socket_) {
  other.socket_ = -1;
}

RconClient &RconClient::operator=(RconClient &&other) noexcept {
  if (this != &other) {
    disconnect();
    socket_ = other.socket_;
    other.socket_ = -1;
  }
  return *this;
}

bool RconClient::connect(const std::string &host, uint16_t port) {
  disconnect();

  std::string port_str = std::to_string(port);
  socket_ = mcrcon_connect(host.c_str(), port_str.c_str());

  return socket_ >= 0;
}

bool RconClient::authenticate(const std::string &password) {
  if (socket_ < 0) {
    return false;
  }

  return mcrcon_auth(socket_, password.c_str()) == 1;
}

std::optional<std::string> RconClient::execute(const std::string &command) {
  if (socket_ < 0) {
    return std::nullopt;
  }

  char response[4096];
  if (mcrcon_command(socket_, command.c_str(), response, sizeof(response)) !=
      1) {
    return std::nullopt;
  }

  return std::string(response);
}

void RconClient::disconnect() {
  if (socket_ >= 0) {
    mcrcon_close(socket_);
    socket_ = -1;
  }
}

std::optional<std::string> execute_rcon_command(const std::string &host,
                                                uint16_t port,
                                                const std::string &password,
                                                const std::string &command) {
  RconClient client;

  if (!client.connect(host, port)) {
    return std::nullopt;
  }

  if (!client.authenticate(password)) {
    return std::nullopt;
  }

  return client.execute(command);
}

} // namespace mc_rcon
