#pragma once

#include <boost/asio/awaitable.hpp>
#include <string>

namespace plugins {

/**
 * @brief Rclone客户端封装
 *
 * 封装所有rclone相关操作：上传、生成分享链接等
 */
class RcloneClient {
public:
  RcloneClient(const std::string &remote, const std::string &remote_path,
               const std::string &proxy = "");

  /**
   * @brief 上传文件或文件夹到远程
   * @param local_path 本地文件或文件夹路径
   * @return 远程完整路径
   */
  boost::asio::awaitable<std::string> upload(const std::string &local_path);

  /**
   * @brief 生成远程文件的分享链接
   * @param remote_full_path 远程完整路径
   * @return 分享链接或fallback字符串
   */
  boost::asio::awaitable<std::string> get_share_link(
      const std::string &remote_full_path);

private:
  std::string remote_;      // rclone remote名称 (e.g., "gdrive:")
  std::string remote_path_; // 远程目标路径 (e.g., "Torrents")
  std::string proxy_;       // 代理URL (可选)

  /**
   * @brief 设置代理环境变量
   * @param old_http 保存旧的http_proxy值
   * @param old_https 保存旧的https_proxy值
   * @return 是否设置了代理
   */
  bool set_proxy_env(std::string &old_http, std::string &old_https);

  /**
   * @brief 恢复代理环境变量
   */
  void restore_proxy_env(bool proxy_was_set, const std::string &old_http,
                         const std::string &old_https);
};

} // namespace plugins
