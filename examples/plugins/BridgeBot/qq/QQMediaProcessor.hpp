#pragma once

#include "common/MessageType.hpp"
#include "interface/IBot.hpp"

#include <boost/asio.hpp>
#include <optional>
#include <string>
#include <vector>

namespace bridge::qq {

/**
 * @brief QQ媒体文件处理器
 *
 * 处理QQ到Telegram的媒体文件转换和处理
 */
class QQMediaProcessor {
public:
  /**
   * @brief 处理QQ消息段并转换为Telegram格式
   * @param qq_bot QQ机器人实例
   * @param telegram_bot Telegram机器人实例
   * @param segment QQ消息段
   * @param temp_files_to_cleanup 临时文件清理列表
   * @return 转换后的Telegram消息段
   */
  static auto convert_qq_segment_to_telegram(
      obcx::core::IBot &qq_bot, obcx::core::IBot &telegram_bot,
      const obcx::common::MessageSegment &segment,
      std::vector<std::string> &temp_files_to_cleanup)
      -> boost::asio::awaitable<std::optional<obcx::common::MessageSegment>>;

  /**
   * @brief 处理图片段
   */
  static auto process_image_segment(
      const obcx::common::MessageSegment &segment,
      std::vector<std::string> &temp_files_to_cleanup)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理语音段
   */
  static auto process_record_segment(
      const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理视频段
   */
  static auto process_video_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理文件段
   */
  static auto process_file_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理QQ表情段
   */
  static auto process_face_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理@消息段
   */
  static auto process_at_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理戳一戳段
   */
  static auto process_shake_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理音乐分享段
   */
  static auto process_music_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理链接分享段
   */
  static auto process_share_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理JSON小程序段
   */
  static auto process_json_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理应用分享段
   */
  static auto process_app_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理ARK卡片段
   */
  static auto process_ark_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

private:
  /**
   * @brief 检测是否为GIF格式文件
   */
  static auto is_gif_file(const std::string &file_path) -> bool;

  /**
   * @brief 解析小程序JSON数据
   */
  static auto parse_miniapp_json(const std::string &json_data) -> std::string;
};

} // namespace bridge::qq