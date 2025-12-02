#include "rclone_client.hpp"
#include "common/logger.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace plugins {

RcloneClient::RcloneClient(const std::string &remote,
                           const std::string &remote_path,
                           const std::string &proxy)
    : remote_(remote), remote_path_(remote_path), proxy_(proxy) {}

bool RcloneClient::set_proxy_env(std::string &old_http,
                                 std::string &old_https) {
  if (proxy_.empty()) {
    return false;
  }

  // Save old values
  const char *old_http_ptr = std::getenv("http_proxy");
  const char *old_https_ptr = std::getenv("https_proxy");
  if (old_http_ptr)
    old_http = old_http_ptr;
  if (old_https_ptr)
    old_https = old_https_ptr;

  // Set new proxy
  setenv("http_proxy", proxy_.c_str(), 1);
  setenv("https_proxy", proxy_.c_str(), 1);

  OBCX_DEBUG("Set proxy environment variables to {}", proxy_);
  return true;
}

void RcloneClient::restore_proxy_env(bool proxy_was_set,
                                     const std::string &old_http,
                                     const std::string &old_https) {
  if (!proxy_was_set) {
    return;
  }

  if (!old_http.empty()) {
    setenv("http_proxy", old_http.c_str(), 1);
  } else {
    unsetenv("http_proxy");
  }

  if (!old_https.empty()) {
    setenv("https_proxy", old_https.c_str(), 1);
  } else {
    unsetenv("https_proxy");
  }
}

boost::asio::awaitable<std::string> RcloneClient::upload(
    const std::string &local_path) {

  std::string full_remote = remote_ + remote_path_;

  OBCX_INFO("Uploading {} to {} (proxy: {})", local_path, full_remote,
            proxy_.empty() ? "none" : proxy_);

  // Set proxy
  std::string old_http_proxy, old_https_proxy;
  bool proxy_set = set_proxy_env(old_http_proxy, old_https_proxy);

  // Use fork+exec to avoid shell interpretation
  pid_t pid = fork();

  if (pid == -1) {
    throw std::runtime_error("Failed to fork process for rclone copy");
  }

  if (pid == 0) {
    // Child process - exec rclone
    execl("/usr/bin/rclone", "rclone", "copy", "--exclude=.torrent_tasks.json",
          local_path.c_str(), full_remote.c_str(), nullptr);

    // If execl returns, it failed
    OBCX_ERROR("Failed to exec rclone: {}", strerror(errno));
    _exit(127);
  }

  // Parent process - wait for child
  int status;
  waitpid(pid, &status, 0);
  int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

  // Restore proxy
  restore_proxy_env(proxy_set, old_http_proxy, old_https_proxy);

  if (exit_code != 0) {
    throw std::runtime_error(
        fmt::format("rclone copy failed with exit code {}", exit_code));
  }

  // Return full remote path
  std::string filename = fs::path(local_path).filename().string();
  std::string remote_full_path = full_remote; // + "/" + filename;

  OBCX_INFO("Upload successful: {}", remote_full_path);
  co_return remote_full_path;
}

boost::asio::awaitable<std::string> RcloneClient::get_share_link(
    const std::string &remote_full_path) {

  OBCX_INFO("Generating share link for {}", remote_full_path);

  // Set proxy
  std::string old_http_proxy, old_https_proxy;
  bool proxy_set = set_proxy_env(old_http_proxy, old_https_proxy);

  // Create pipe for reading rclone output
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    restore_proxy_env(proxy_set, old_http_proxy, old_https_proxy);
    throw std::runtime_error("Failed to create pipe");
  }

  pid_t pid = fork();

  if (pid == -1) {
    close(pipefd[0]);
    close(pipefd[1]);
    restore_proxy_env(proxy_set, old_http_proxy, old_https_proxy);
    throw std::runtime_error("Failed to fork process for rclone link");
  }

  if (pid == 0) {
    // Child process
    close(pipefd[0]);               // Close read end
    dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
    close(pipefd[1]);

    // Execute rclone link
    execl("/usr/bin/rclone", "rclone", "link", remote_full_path.c_str(),
          nullptr);

    _exit(127);
  }

  // Parent process
  close(pipefd[1]); // Close write end

  // Read output from pipe
  std::string output;
  char buffer[256];
  ssize_t bytes_read;
  while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
    buffer[bytes_read] = '\0';
    output += buffer;
  }
  close(pipefd[0]);

  // Wait for child
  int status;
  waitpid(pid, &status, 0);
  int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

  // Restore proxy
  restore_proxy_env(proxy_set, old_http_proxy, old_https_proxy);

  if (exit_code == 0 && !output.empty()) {
    // Trim whitespace
    output.erase(0, output.find_first_not_of(" \t\r\n"));
    output.erase(output.find_last_not_of(" \t\r\n") + 1);

    if (!output.empty()) {
      OBCX_INFO("Generated share link: {}", output);
      co_return output;
    }
  }

  OBCX_WARN("Could not generate share link for {}, exit_code: {}",
            remote_full_path, exit_code);
  co_return fmt::format("文件已上传到: {}", remote_full_path);
}

} // namespace plugins
