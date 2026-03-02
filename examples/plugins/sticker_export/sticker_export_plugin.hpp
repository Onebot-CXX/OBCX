#pragma once

#include <boost/asio/awaitable.hpp>
#include <common/message_type.hpp>
#include <interfaces/plugin.hpp>
#include <nlohmann/json.hpp>

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sticker_export {

/**
 * @brief Sticker 下载导出插件
 *
 * 支持 QQ 和 Telegram 两端的表情包下载
 *
 * 命令:
 *   /dl_sticker           - 回复一条含表情包的消息，下载并返回文件
 *   /start_sticker_export - 开始批量导出模式
 *   /end_sticker_export   - 结束批量导出模式
 */
class StickerExportPlugin : public obcx::interface::IPlugin {
public:
  StickerExportPlugin();
  ~StickerExportPlugin() override;

  [[nodiscard]] auto get_name() const -> std::string override;
  [[nodiscard]] auto get_version() const -> std::string override;
  [[nodiscard]] auto get_description() const -> std::string override;

  auto initialize() -> bool override;
  void deinitialize() override;
  void shutdown() override;

private:
  struct Config {
    std::string download_dir = "/tmp/sticker_export";
  };

  Config config_;

  // Export session tracking: key = "platform:user_id"
  std::map<std::string, bool> export_sessions_;
  std::mutex session_mutex_;

  auto load_configuration() -> bool;

  // --- QQ ---
  auto handle_qq_message(obcx::core::IBot &bot,
                         const obcx::common::MessageEvent &event)
      -> boost::asio::awaitable<void>;

  auto download_and_send_qq_emoji(obcx::core::IBot &bot,
                                  const obcx::common::MessageSegment &segment,
                                  const std::string &target_id, bool is_group)
      -> boost::asio::awaitable<void>;

  // --- Telegram ---
  auto handle_tg_message(obcx::core::IBot &bot,
                         const obcx::common::MessageEvent &event)
      -> boost::asio::awaitable<void>;

  auto download_and_send_tg_sticker(obcx::core::IBot &bot,
                                    const obcx::common::MessageEvent &event)
      -> boost::asio::awaitable<void>;

  // --- Helpers ---
  auto send_file_qq(obcx::core::IBot &bot, const std::string &file_path,
                    const std::string &target_id, bool is_group)
      -> boost::asio::awaitable<void>;

  auto send_file_tg(obcx::core::IBot &bot, const std::string &file_path,
                    const std::string &chat_id,
                    std::optional<int64_t> topic_id = std::nullopt)
      -> boost::asio::awaitable<void>;

  auto send_text_qq(obcx::core::IBot &bot, const std::string &text,
                    const std::string &target_id, bool is_group)
      -> boost::asio::awaitable<void>;

  auto send_text_tg(obcx::core::IBot &bot, const std::string &text,
                    const std::string &chat_id,
                    std::optional<int64_t> topic_id = std::nullopt)
      -> boost::asio::awaitable<void>;

  static auto get_session_key(const std::string &platform,
                              const std::string &user_id) -> std::string;
  auto is_export_active(const std::string &session_key) -> bool;
  void set_export_active(const std::string &session_key, bool active);

  /// 检测 QQ 消息段是否为表情包/emoji
  static auto is_qq_emoji(const obcx::common::MessageSegment &segment) -> bool;

  /// 从 QQ 消息中提取所有 emoji 段
  static auto extract_qq_emoji_segments(const obcx::common::Message &message)
      -> std::vector<obcx::common::MessageSegment>;

  /// 检查 TG 消息是否包含贴纸/动画
  static auto has_tg_sticker(const obcx::common::MessageEvent &event) -> bool;
};

} // namespace sticker_export
