#pragma once

#include "common/message_type.hpp"
#include "interfaces/plugin.hpp"

#include <boost/asio/awaitable.hpp>
#include <chrono>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

namespace group_member_backup {

/**
 * @brief 群成员备份恢复插件
 *
 * 功能：
 * - /backup：备份当前群的所有成员信息到JSON文件
 * - /restore <min,max>：回复JSON文件来恢复群成员（发送加群邀请）
 *   - min,max：随机间隔时间范围（秒），默认5,10
 *
 * 配置示例:
 * [group_member_backup]
 * allowed_groups = ["123456789"]
 * admin_users = ["987654321"]  # 允许使用该功能的用户QQ号
 */
class GroupMemberBackupPlugin : public obcx::interface::IPlugin {
public:
  GroupMemberBackupPlugin();
  ~GroupMemberBackupPlugin() override;

  auto get_name() const -> std::string override;
  auto get_version() const -> std::string override;
  auto get_description() const -> std::string override;

  auto initialize() -> bool override;
  void deinitialize() override;
  void shutdown() override;

private:
  /// 恢复任务状态
  struct RestoreTask {
    std::string group_id;
    std::string operator_id;
    std::vector<std::string> pending_user_ids;
    std::vector<std::string> success_user_ids;
    std::vector<std::string> failed_user_ids;
    int min_interval;
    int max_interval;
    bool is_running;
    std::chrono::system_clock::time_point start_time;
  };

  /// 允许使用该命令的群列表
  std::set<std::string> allowed_groups_;
  /// 允许使用该命令的管理员用户列表
  std::set<std::string> admin_users_;
  /// 当前活动的恢复任务
  std::map<std::string, RestoreTask> restore_tasks_;
  /// 任务互斥锁
  mutable std::mutex tasks_mutex_;

  /**
   * @brief 加载配置
   */
  auto load_configuration() -> bool;

  /**
   * @brief 检查群是否在白名单中
   */
  auto is_allowed_group(const std::string &group_id) const -> bool;

  /**
   * @brief 检查用户是否是管理员
   */
  auto is_admin_user(const std::string &user_id) const -> bool;

  /**
   * @brief 处理QQ群消息
   */
  auto handle_qq_message(obcx::core::IBot &bot,
                         const obcx::common::MessageEvent &event)
      -> boost::asio::awaitable<void>;

  /**
   * @brief 处理备份命令
   */
  auto handle_backup_command(obcx::core::IBot &bot,
                             const obcx::common::MessageEvent &event)
      -> boost::asio::awaitable<void>;

  /**
   * @brief 处理恢复命令
   */
  auto handle_restore_command(obcx::core::IBot &bot,
                              const obcx::common::MessageEvent &event,
                              int min_interval, int max_interval)
      -> boost::asio::awaitable<void>;

  /**
   * @brief 处理状态查询命令
   */
  auto handle_status_command(obcx::core::IBot &bot,
                             const obcx::common::MessageEvent &event)
      -> boost::asio::awaitable<void>;

  /**
   * @brief 执行恢复任务（在后台线程池中运行）
   */
  void execute_restore_task(obcx::core::IBot &bot, const std::string &group_id,
                            const std::string &operator_id,
                            const std::vector<std::string> &user_ids,
                            int min_interval, int max_interval);

  /**
   * @brief 发送文本消息
   */
  auto send_message(obcx::core::IBot &bot, const std::string &group_id,
                    const std::string &text) -> boost::asio::awaitable<void>;

  /**
   * @brief 解析恢复命令的时间参数
   * @return {min_interval, max_interval}，默认{5, 10}
   */
  auto parse_restore_params(const std::string &raw_message)
      -> std::pair<int, int>;

  /**
   * @brief 从消息中提取文件URL
   */
  auto extract_file_url(const obcx::common::Message &message)
      -> std::optional<std::string>;

  /**
   * @brief 下载并解析备份文件
   */
  auto download_and_parse_backup(const std::string &file_url)
      -> std::optional<nlohmann::json>;

  /**
   * @brief 生成随机时间间隔（秒）
   */
  auto generate_random_interval(int min_sec, int max_sec) -> int;
};

} // namespace group_member_backup
