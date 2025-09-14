#include "QQMediaProcessor.hpp"
#include "common/Logger.hpp"

#include <fmt/format.h>

namespace bridge::qq {

auto QQMediaProcessor::convert_qq_segment_to_telegram(
    obcx::core::IBot &qq_bot, obcx::core::IBot &telegram_bot,
    const obcx::common::MessageSegment &segment,
    std::vector<std::string> &temp_files_to_cleanup)
    -> boost::asio::awaitable<std::optional<obcx::common::MessageSegment>> {

  try {
    if (segment.type == "image") {
      co_return co_await process_image_segment(segment, temp_files_to_cleanup);
    } else if (segment.type == "record") {
      co_return co_await process_record_segment(segment);
    } else if (segment.type == "video") {
      co_return co_await process_video_segment(segment);
    } else if (segment.type == "file") {
      co_return co_await process_file_segment(segment);
    } else if (segment.type == "face") {
      co_return co_await process_face_segment(segment);
    } else if (segment.type == "at") {
      co_return co_await process_at_segment(segment);
    } else if (segment.type == "shake") {
      co_return co_await process_shake_segment(segment);
    } else if (segment.type == "music") {
      co_return co_await process_music_segment(segment);
    } else if (segment.type == "share") {
      co_return co_await process_share_segment(segment);
    } else if (segment.type == "json") {
      co_return co_await process_json_segment(segment);
    } else if (segment.type == "app") {
      co_return co_await process_app_segment(segment);
    } else if (segment.type == "ark") {
      co_return co_await process_ark_segment(segment);
    } else {
      // 保持原样
      co_return segment;
    }
  } catch (const std::exception &e) {
    OBCX_ERROR("转换QQ消息段失败: {}", e.what());
    co_return std::nullopt;
  }
}

auto QQMediaProcessor::process_image_segment(
    const obcx::common::MessageSegment &segment,
    std::vector<std::string> &temp_files_to_cleanup)
    -> boost::asio::awaitable<obcx::common::MessageSegment> {

  obcx::common::MessageSegment converted = segment;

  // 简化处理 - 保持图片格式不变
  std::string file_name = segment.data.value("file", "");
  std::string url = segment.data.value("url", "");

  OBCX_DEBUG("处理QQ图片: file={}, url={}", file_name, url);

  co_return converted;
}

auto QQMediaProcessor::process_record_segment(
    const obcx::common::MessageSegment &segment)
    -> boost::asio::awaitable<obcx::common::MessageSegment> {

  obcx::common::MessageSegment converted = segment;
  OBCX_DEBUG("转发QQ语音文件: {}", segment.data.value("file", "unknown"));
  co_return converted;
}

auto QQMediaProcessor::process_video_segment(
    const obcx::common::MessageSegment &segment)
    -> boost::asio::awaitable<obcx::common::MessageSegment> {

  obcx::common::MessageSegment converted = segment;
  OBCX_DEBUG("转发QQ视频文件: {}", segment.data.value("file", "unknown"));
  co_return converted;
}

auto QQMediaProcessor::process_file_segment(
    const obcx::common::MessageSegment &segment)
    -> boost::asio::awaitable<obcx::common::MessageSegment> {

  obcx::common::MessageSegment converted = segment;
  converted.type = "document";
  OBCX_DEBUG("转发QQ文件: {}", segment.data.value("file", "unknown"));
  co_return converted;
}

auto QQMediaProcessor::process_face_segment(
    const obcx::common::MessageSegment &segment)
    -> boost::asio::awaitable<obcx::common::MessageSegment> {

  obcx::common::MessageSegment converted;
  converted.type = "text";
  std::string face_id = segment.data.value("id", "0");
  converted.data["text"] = fmt::format("[QQ表情:{}]", face_id);
  OBCX_DEBUG("转换QQ表情为文本提示: face_id={}", face_id);
  co_return converted;
}

auto QQMediaProcessor::process_at_segment(
    const obcx::common::MessageSegment &segment)
    -> boost::asio::awaitable<obcx::common::MessageSegment> {

  obcx::common::MessageSegment converted;
  converted.type = "text";
  std::string qq_user_id = segment.data.value("qq", "unknown");
  converted.data["text"] = fmt::format("[@{}] ", qq_user_id);
  OBCX_DEBUG("转换QQ@消息: {}", qq_user_id);
  co_return converted;
}

auto QQMediaProcessor::process_shake_segment(
    const obcx::common::MessageSegment &segment)
    -> boost::asio::awaitable<obcx::common::MessageSegment> {

  obcx::common::MessageSegment converted;
  converted.type = "text";
  converted.data["text"] = "[戳一戳]";
  OBCX_DEBUG("转换QQ戳一戳为文本提示");
  co_return converted;
}

auto QQMediaProcessor::process_music_segment(
    const obcx::common::MessageSegment &segment)
    -> boost::asio::awaitable<obcx::common::MessageSegment> {

  obcx::common::MessageSegment converted;
  converted.type = "text";
  std::string title = segment.data.value("title", "未知音乐");
  converted.data["text"] = fmt::format("[音乐分享: {}]", title);
  OBCX_DEBUG("转换QQ音乐分享为文本: title={}", title);
  co_return converted;
}

auto QQMediaProcessor::process_share_segment(
    const obcx::common::MessageSegment &segment)
    -> boost::asio::awaitable<obcx::common::MessageSegment> {

  obcx::common::MessageSegment converted;
  converted.type = "text";
  std::string url = segment.data.value("url", "");
  std::string title = segment.data.value("title", "链接分享");
  converted.data["text"] = fmt::format("[{}]\t{}", title, url);
  OBCX_DEBUG("转换QQ链接分享为文本: title={}, url={}", title, url);
  co_return converted;
}

auto QQMediaProcessor::process_json_segment(
    const obcx::common::MessageSegment &segment)
    -> boost::asio::awaitable<obcx::common::MessageSegment> {

  obcx::common::MessageSegment converted;
  converted.type = "text";

  try {
    std::string json_data = segment.data.value("data", "");
    if (!json_data.empty()) {
      std::string parsed_info = parse_miniapp_json(json_data);
      converted.data["text"] = parsed_info;
      OBCX_DEBUG("转换QQ小程序JSON: {}", parsed_info);
    } else {
      converted.data["text"] = "📱 [小程序-无数据]";
      OBCX_DEBUG("QQ小程序JSON消息无数据");
    }
  } catch (const std::exception &e) {
    converted.data["text"] = "📱 [小程序解析错误]";
    OBCX_ERROR("处理QQ小程序JSON时出错: {}", e.what());
  }

  co_return converted;
}

auto QQMediaProcessor::process_app_segment(
    const obcx::common::MessageSegment &segment)
    -> boost::asio::awaitable<obcx::common::MessageSegment> {

  obcx::common::MessageSegment converted;
  converted.type = "text";

  try {
    std::string title = segment.data.value("title", "应用分享");
    std::string url = segment.data.value("url", "");
    converted.data["text"] = fmt::format("📱 [{}]\t{}", title, url);
    OBCX_DEBUG("转换QQ应用分享: title={}", title);
  } catch (const std::exception &e) {
    converted.data["text"] = "📱 [应用分享解析错误]";
    OBCX_ERROR("处理QQ应用分享时出错: {}", e.what());
  }

  co_return converted;
}

auto QQMediaProcessor::process_ark_segment(
    const obcx::common::MessageSegment &segment)
    -> boost::asio::awaitable<obcx::common::MessageSegment> {

  obcx::common::MessageSegment converted;
  converted.type = "text";
  converted.data["text"] = "📋 [ARK卡片消息]";
  OBCX_DEBUG("转换QQ ARK卡片为文本提示");
  co_return converted;
}

auto QQMediaProcessor::parse_miniapp_json(const std::string &json_data)
    -> std::string {
  // 简化版小程序解析
  try {
    // 这里应该包含小程序JSON解析逻辑，但为了简化先返回基本信息
    return "📱 [小程序]";
  } catch (const std::exception &e) {
    OBCX_ERROR("解析小程序JSON失败: {}", e.what());
    return "📱 [小程序解析失败]";
  }
}

auto QQMediaProcessor::is_gif_file(const std::string &file_path) -> bool {
  // 简化的GIF检测
  return file_path.find(".gif") != std::string::npos ||
         file_path.find(".GIF") != std::string::npos ||
         file_path.find("gif") != std::string::npos;
}

} // namespace bridge::qq