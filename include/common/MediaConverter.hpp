#pragma once

#include <future>
#include <string>

namespace obcx::common {

/**
 * @brief 文件格式转换器
 *
 * 用于转换不同格式的媒体文件，特别是将Telegram特有的格式转换为QQ支持的格式
 */
class MediaConverter {
public:
  /**
   * @brief 将WebM格式转换为GIF格式（完全无损转换）
   * @param webm_url WebM文件的URL
   * @param output_path 输出GIF文件的路径
   * @param max_duration 最大转换时长（秒），默认5秒
   * @param max_width
   * 输出GIF的最大宽度，设为0则保持原始分辨率，默认0（完全无损）
   * @return 转换是否成功
   */
  static auto convert_webm_to_gif(const std::string &webm_url,
                                  const std::string &output_path,
                                  int max_duration = 5, int max_width = 0)
      -> bool;

  /**
   * @brief 异步将WebM格式转换为GIF格式（完全无损转换）
   * @param webm_url WebM文件的URL
   * @param output_path 输出GIF文件的路径
   * @param max_duration 最大转换时长（秒），默认5秒
   * @param max_width
   * 输出GIF的最大宽度，设为0则保持原始分辨率，默认0（完全无损）
   * @return 异步任务的future对象
   */
  static auto convert_webm_to_gif_async(const std::string &webm_url,
                                        const std::string &output_path,
                                        int max_duration = 5, int max_width = 0)
      -> std::future<bool>;

  /**
   * @brief 带回退机制的WebM到GIF转换（无损->压缩->失败）
   * @param webm_url WebM文件的URL
   * @param output_path 输出GIF文件的路径
   * @param max_duration 最大转换时长（秒），默认5秒
   * @return 转换是否成功
   */
  static auto convert_webm_to_gif_with_fallback(const std::string &webm_url,
                                                const std::string &output_path,
                                                int max_duration = 5) -> bool;

  /**
   * @brief 将TGS格式（Telegram动画贴纸）转换为GIF格式
   * @param tgs_url TGS文件的URL
   * @param output_path 输出GIF文件的路径
   * @param max_width 输出GIF的最大宽度，默认512px
   * @return 转换是否成功
   */
  static auto convert_tgs_to_gif(const std::string &tgs_url,
                                 const std::string &output_path,
                                 int max_width = 512) -> bool;

  /**
   * @brief 生成临时文件路径
   * @param extension 文件扩展名（不包含点）
   * @return 临时文件的完整路径
   */
  static auto generate_temp_path(const std::string &extension) -> std::string;

  /**
   * @brief 清理临时文件
   * @param file_path 要删除的文件路径
   */
  static auto cleanup_temp_file(const std::string &file_path) -> void;

private:
  /**
   * @brief 执行系统命令并返回结果
   * @param command 要执行的命令
   * @return 命令执行成功返回true
   */
  static auto execute_command(const std::string &command) -> bool;

  /**
   * @brief 检查文件是否存在且大小大于0
   * @param file_path 文件路径
   * @return 文件有效返回true
   */
  static auto is_valid_file(const std::string &file_path) -> bool;
};

} // namespace obcx::common