#pragma once

#include <boost/asio/awaitable.hpp>
#include <common/message_type.hpp>
#include <interfaces/plugin.hpp>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

namespace image_extract {

/**
 * @brief Image Extract Plugin - 提取合并转发消息中的图片URL
 *
 * 使用方法: 回复一条合并转发消息，并发送 /ie 或 /imageextract 命令
 * 插件会解析合并转发消息中的所有图片，并以合并转发的形式输出所有图片URL
 *
 * 配置示例:
 * [image_extract]
 * allowed_groups = ["123456789", "987654321"]
 */
class ImageExtractPlugin : public obcx::interface::IPlugin {
public:
  ImageExtractPlugin();
  ~ImageExtractPlugin() override;

  [[nodiscard]] auto get_name() const -> std::string override;
  [[nodiscard]] auto get_version() const -> std::string override;
  [[nodiscard]] auto get_description() const -> std::string override;

  auto initialize() -> bool override;
  void deinitialize() override;
  void shutdown() override;

private:
  /// 允许使用该命令的群列表
  std::set<std::string> allowed_groups_;

  /**
   * @brief 加载配置
   */
  auto load_configuration() -> bool;

  /**
   * @brief 检查群是否在白名单中
   */
  auto is_allowed_group(const std::string &group_id) const -> bool;
  /**
   * @brief 处理QQ群消息
   */
  auto handle_qq_message(obcx::core::IBot &bot,
                         const obcx::common::MessageEvent &event)
      -> boost::asio::awaitable<void>;

  /**
   * @brief 从合并转发消息中提取所有图片URL（包括通过reply引用的图片）
   * @param bot Bot引用用于获取reply引用的消息
   * @param forward_data 合并转发消息的JSON数据
   * @return 图片URL列表
   */
  auto extract_image_urls(obcx::core::IBot &bot,
                          const nlohmann::json &forward_data)
      -> boost::asio::awaitable<std::vector<std::string>>;

  /**
   * @brief 从消息内容中提取图片URL和reply ID
   * @param content 消息内容
   * @param urls 用于存储提取的URL
   * @param reply_ids 用于存储需要进一步获取的reply消息ID
   */
  void extract_images_and_replies(const nlohmann::json &content,
                                  std::vector<std::string> &urls,
                                  std::vector<std::string> &reply_ids);

  /**
   * @brief 发送包含图片URL的合并转发消息
   */
  auto send_forward_response(obcx::core::IBot &bot, const std::string &group_id,
                             const std::vector<std::string> &image_urls,
                             const std::string &self_id)
      -> boost::asio::awaitable<void>;

  /**
   * @brief 发送普通文本响应
   */
  auto send_text_response(obcx::core::IBot &bot, const std::string &group_id,
                          const std::string &text)
      -> boost::asio::awaitable<void>;
};

} // namespace image_extract
