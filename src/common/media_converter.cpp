#include "common/media_converter.hpp"
#include "common/logger.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

namespace obcx::common {

auto MediaConverter::convert_webm_to_gif(const std::string &webm_url,
                                         const std::string &output_path,
                                         int max_duration, int max_width)
    -> bool {
  try {
    OBCX_INFO("开始转换WebM到GIF: {} -> {}", webm_url, output_path);

    // 构建FFmpeg命令 - 完全无损转换
    // -t: 最大时长
    // -vf: 视频滤镜，完全保持原始分辨率、帧率和颜色
    // -y: 覆盖输出文件
    // 完全无损：不缩放分辨率、不限制帧率、不压缩调色板
    std::ostringstream cmd;

    // 如果max_width参数大于0，则进行缩放；否则保持原始分辨率
    if (max_width > 0) {
      cmd << "ffmpeg -i \"" << webm_url << "\" "
          << "-t " << max_duration << " "
          << "-vf \"scale=" << max_width
          << ":-1:flags=lanczos:force_original_aspect_ratio=decrease,split[s0]["
             "s1];[s0]palettegen=reserve_transparent=on:max_colors=256:stats_"
             "mode=full[p];[s1][p]paletteuse=dither=bayer:bayer_scale=5:diff_"
             "mode=rectangle\" "
          << "-loop 0 "
          << "-y \"" << output_path << "\" "
          << "2>/dev/null";
    } else {
      // 完全无损模式：保持原始分辨率、帧率、颜色
      cmd << "ffmpeg -i \"" << webm_url << "\" "
          << "-t " << max_duration << " "
          << "-vf "
             "\"split[s0][s1];[s0]palettegen=reserve_transparent=on:max_colors="
             "256:stats_mode=full[p];[s1][p]paletteuse=dither=bayer:bayer_"
             "scale=5:diff_mode=rectangle\" "
          << "-loop 0 "
          << "-y \"" << output_path << "\" "
          << "2>/dev/null";
    }

    OBCX_DEBUG("执行FFmpeg命令: {}", cmd.str());

    bool success = execute_command(cmd.str());

    if (success && is_valid_file(output_path)) {
      auto file_size = std::filesystem::file_size(output_path);
      OBCX_INFO("WebM到GIF转换成功，输出文件大小: {} bytes", file_size);
      return true;
    } else {
      OBCX_ERROR("WebM到GIF转换失败或输出文件无效");
      return false;
    }
  } catch (const std::exception &e) {
    OBCX_ERROR("WebM到GIF转换异常: {}", e.what());
    return false;
  }
}

auto MediaConverter::convert_webm_to_gif_async(const std::string &webm_url,
                                               const std::string &output_path,
                                               int max_duration, int max_width)
    -> std::future<bool> {
  return std::async(std::launch::async, [=]() {
    return convert_webm_to_gif(webm_url, output_path, max_duration, max_width);
  });
}

auto MediaConverter::convert_webm_to_gif_with_fallback(
    const std::string &webm_url, const std::string &output_path,
    int max_duration) -> bool {
  try {
    OBCX_INFO("开始带回退机制的WebM到GIF转换: {} -> {}", webm_url, output_path);

    // 第一次尝试：完全无损转换（保持原始分辨率、帧率、颜色）
    OBCX_DEBUG("尝试无损转换...");
    bool lossless_success =
        convert_webm_to_gif(webm_url, output_path, max_duration, 0);

    if (lossless_success && is_valid_file(output_path)) {
      OBCX_INFO("无损WebM到GIF转换成功");
      return true;
    }

    // 清理失败的文件
    cleanup_temp_file(output_path);

    // 第二次尝试：压缩转换（320px宽度，保持帧率和颜色优化）
    OBCX_WARN("无损转换失败，尝试压缩转换...");
    bool compressed_success =
        convert_webm_to_gif(webm_url, output_path, max_duration, 320);

    if (compressed_success && is_valid_file(output_path)) {
      OBCX_INFO("压缩WebM到GIF转换成功（320px）");
      return true;
    }

    // 清理失败的文件
    cleanup_temp_file(output_path);

    OBCX_ERROR("WebM到GIF转换完全失败，需要使用文本表情回退");
    return false;

  } catch (const std::exception &e) {
    OBCX_ERROR("带回退机制的WebM到GIF转换异常: {}", e.what());
    cleanup_temp_file(output_path);
    return false;
  }
}

auto MediaConverter::convert_tgs_to_gif(const std::string &tgs_url,
                                        const std::string &output_path,
                                        int max_width) -> bool {
  try {
    OBCX_INFO("开始转换TGS到GIF: {} -> {}", tgs_url, output_path);

    // TGS格式是基于Lottie的JSON动画，需要特殊处理
    // 这里我们尝试使用lottie-convert工具，如果不可用则返回false
    std::ostringstream cmd;
    cmd << "lottie_convert.py \"" << tgs_url << "\" \"" << output_path << "\" "
        << "--width " << max_width << " --height " << max_width << " "
        << "2>/dev/null";

    OBCX_DEBUG("执行TGS转换命令: {}", cmd.str());

    bool success = execute_command(cmd.str());

    if (success && is_valid_file(output_path)) {
      auto file_size = std::filesystem::file_size(output_path);
      OBCX_INFO("TGS到GIF转换成功，输出文件大小: {} bytes", file_size);
      return true;
    } else {
      OBCX_WARN("TGS到GIF转换失败，可能缺少lottie-convert工具");
      return false;
    }
  } catch (const std::exception &e) {
    OBCX_ERROR("TGS到GIF转换异常: {}", e.what());
    return false;
  }
}

auto MediaConverter::generate_temp_path(const std::string &extension)
    -> std::string {
  try {
    // 优先使用Docker共享目录
    std::filesystem::path shared_dir =
        "/home/lambillda/Codes/OBCX/tests/llonebot/bridge_files";

    // 检查共享目录是否存在，如果存在则使用它
    if (std::filesystem::exists(shared_dir)) {
      std::filesystem::create_directories(shared_dir);

      // 生成随机文件名
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(100000, 999999);

      std::string filename =
          "convert_" + std::to_string(dis(gen)) + "." + extension;
      std::filesystem::path temp_file = shared_dir / filename;

      OBCX_DEBUG("生成Docker共享目录文件路径: {}", temp_file.string());
      return temp_file.string();
    }

    // 回退：创建临时目录
    std::filesystem::path temp_dir =
        std::filesystem::temp_directory_path() / "obcx_bridge";
    std::filesystem::create_directories(temp_dir);

    // 生成随机文件名
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);

    std::string filename =
        "convert_" + std::to_string(dis(gen)) + "." + extension;
    std::filesystem::path temp_file = temp_dir / filename;

    OBCX_DEBUG("生成临时文件路径: {}", temp_file.string());
    return temp_file.string();
  } catch (const std::exception &e) {
    OBCX_ERROR("生成临时文件路径失败: {}", e.what());
    // 回退到简单路径
    return "/tmp/obcx_convert_" + std::to_string(std::time(nullptr)) + "." +
           extension;
  }
}

auto MediaConverter::cleanup_temp_file(const std::string &file_path) -> void {
  try {
    if (std::filesystem::exists(file_path)) {
      std::filesystem::remove(file_path);
      OBCX_DEBUG("清理临时文件: {}", file_path);
    }
  } catch (const std::exception &e) {
    OBCX_WARN("清理临时文件失败: {} - {}", file_path, e.what());
  }
}

auto MediaConverter::execute_command(const std::string &command) -> bool {
  try {
    OBCX_DEBUG("执行系统命令: {}", command);
    int result = std::system(command.c_str());
    bool success = (result == 0);

    if (success) {
      OBCX_DEBUG("命令执行成功");
    } else {
      OBCX_DEBUG("命令执行失败，返回码: {}", result);
    }

    return success;
  } catch (const std::exception &e) {
    OBCX_ERROR("执行系统命令异常: {}", e.what());
    return false;
  }
}

auto MediaConverter::is_valid_file(const std::string &file_path) -> bool {
  try {
    if (!std::filesystem::exists(file_path)) {
      OBCX_DEBUG("文件不存在: {}", file_path);
      return false;
    }

    auto file_size = std::filesystem::file_size(file_path);
    if (file_size == 0) {
      OBCX_DEBUG("文件大小为0: {}", file_path);
      return false;
    }

    OBCX_DEBUG("文件有效: {} ({}bytes)", file_path, file_size);
    return true;
  } catch (const std::exception &e) {
    OBCX_ERROR("检查文件有效性异常: {} - {}", file_path, e.what());
    return false;
  }
}

} // namespace obcx::common