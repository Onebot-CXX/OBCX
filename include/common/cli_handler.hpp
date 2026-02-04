#pragma once

#include "common/config_loader.hpp"
#include "common/plugin_manager.hpp"
#include "interfaces/bot.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace obcx::common {

/**
 * \if CHINESE
 * @brief CLI命令处理器，负责处理stdin输入的命令
 * \endif
 * \if ENGLISH
 * @brief CLI command handler for processing stdin commands
 * \endif
 */
class CliHandler {
public:
  /**
   * \if CHINESE
   * @brief 输出回调函数类型，用于将命令输出重定向到TUI
   * \endif
   * \if ENGLISH
   * @brief Output callback type for redirecting command output to TUI
   * \endif
   */
  using OutputCallback = std::function<void(const std::string &)>;

  /**
   * \if CHINESE
   * @brief CLI上下文，包含命令处理所需的各种引用
   * \endif
   * \if ENGLISH
   * @brief CLI context containing references needed for command processing
   * \endif
   */
  struct Context {
    PluginManager &plugin_manager;
    ConfigLoader &config_loader;
    std::vector<BotConfig> &bot_configs;
    std::vector<std::unique_ptr<core::IBot>> &bots;
    std::mutex &bots_mutex;
    std::atomic_bool &should_stop;
    std::condition_variable &stop_cv;
    OutputCallback output_cb = nullptr;
    std::function<void()> shutdown_cb = nullptr;
  };

  /**
   * \if CHINESE
   * @brief 命令处理函数类型
   * \endif
   * \if ENGLISH
   * @brief Command handler function type
   * \endif
   */
  using CommandHandler = std::function<bool(Context &, const std::string &)>;

  /**
   * \if CHINESE
   * @brief 构造CLI处理器
   * @param ctx CLI上下文
   * \endif
   * \if ENGLISH
   * @brief Construct CLI handler
   * @param ctx CLI context
   * \endif
   */
  explicit CliHandler(Context ctx);

  /**
   * \if CHINESE
   * @brief 处理一行输入命令
   * @param line 输入的命令行
   * @return 是否应该继续运行（false表示应该退出循环）
   * \endif
   * \if ENGLISH
   * @brief Process a line of input command
   * @param line The input command line
   * @return Whether to continue running (false means should exit loop)
   * \endif
   */
  auto process_command(const std::string &line) -> bool;

  /**
   * \if CHINESE
   * @brief 运行CLI输入循环（阻塞）
   * \endif
   * \if ENGLISH
   * @brief Run CLI input loop (blocking)
   * \endif
   */
  void run();

private:
  /**
   * \if CHINESE
   * @brief 注册默认命令处理器
   * \endif
   * \if ENGLISH
   * @brief Register default command handlers
   * \endif
   */
  void register_default_handlers();

  /**
   * \if CHINESE
   * @brief 向输出回调或stdout输出消息
   * \endif
   * \if ENGLISH
   * @brief Output message to callback or stdout
   * \endif
   */
  static void output(Context &ctx, const std::string &msg);

  /**
   * \if CHINESE
   * @brief 处理退出命令
   * \endif
   * \if ENGLISH
   * @brief Handle exit command
   * \endif
   */
  static auto handle_exit(Context &ctx, const std::string &args) -> bool;

  /**
   * \if CHINESE
   * @brief 处理reload命令
   * \endif
   * \if ENGLISH
   * @brief Handle reload command
   * \endif
   */
  static auto handle_reload(Context &ctx, const std::string &args) -> bool;

  /**
   * \if CHINESE
   * @brief 处理log_level命令
   * \endif
   * \if ENGLISH
   * @brief Handle log_level command
   * \endif
   */
  static auto handle_log_level(Context &ctx, const std::string &args) -> bool;

  Context ctx_;
  std::unordered_map<std::string, CommandHandler> handlers_;
};

} // namespace obcx::common
