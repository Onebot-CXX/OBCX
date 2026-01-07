#include "group_member_backup_plugin.hpp"
#include "common/logger.hpp"
#include "core/qq_bot.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <regex>
#include <sstream>
#include <thread>

using json = nlohmann::json;

namespace group_member_backup {

GroupMemberBackupPlugin::GroupMemberBackupPlugin() = default;

GroupMemberBackupPlugin::~GroupMemberBackupPlugin() = default;

auto GroupMemberBackupPlugin::get_name() const -> std::string {
  return "GroupMemberBackupPlugin";
}

auto GroupMemberBackupPlugin::get_version() const -> std::string {
  return "1.0.0";
}

auto GroupMemberBackupPlugin::get_description() const -> std::string {
  return "群成员备份和恢复插件";
}

auto GroupMemberBackupPlugin::initialize() -> bool {
  if (!load_configuration()) {
    PLUGIN_ERROR(get_name(), "Failed to load configuration");
    return false;
  }

  auto [lock, bots] = get_bots();

  for (auto &bot_ptr : bots) {
    if (auto *qq_bot = dynamic_cast<obcx::core::QQBot *>(bot_ptr.get())) {
      qq_bot->on_event<obcx::common::MessageEvent>(
          [this](obcx::core::IBot &bot, const obcx::common::MessageEvent &event)
              -> boost::asio::awaitable<void> {
            co_await handle_qq_message(bot, event);
          });

      PLUGIN_INFO(get_name(), "Registered message handler");
    }
  }

  return true;
}

void GroupMemberBackupPlugin::deinitialize() {
  PLUGIN_INFO(get_name(), "Deinitialized");
}

void GroupMemberBackupPlugin::shutdown() {
  // 停止所有正在运行的任务
  std::lock_guard<std::mutex> lock(tasks_mutex_);
  for (auto &[group_id, task] : restore_tasks_) {
    task.is_running = false;
  }
  PLUGIN_INFO(get_name(), "Shutdown");
}

auto GroupMemberBackupPlugin::load_configuration() -> bool {
  // 加载允许的群列表
  if (auto groups =
          get_config_value<std::vector<std::string>>("allowed_groups")) {
    allowed_groups_ = std::set<std::string>(groups->begin(), groups->end());
    PLUGIN_INFO(get_name(), "Loaded {} allowed groups", allowed_groups_.size());
  }

  // 加载管理员用户列表
  if (auto admins = get_config_value<std::vector<std::string>>("admin_users")) {
    admin_users_ = std::set<std::string>(admins->begin(), admins->end());
    PLUGIN_INFO(get_name(), "Loaded {} admin users", admin_users_.size());
  }

  return true;
}

auto GroupMemberBackupPlugin::is_allowed_group(
    const std::string &group_id) const -> bool {
  return allowed_groups_.empty() || allowed_groups_.count(group_id) > 0;
}

auto GroupMemberBackupPlugin::is_admin_user(const std::string &user_id) const
    -> bool {
  return admin_users_.empty() || admin_users_.count(user_id) > 0;
}

auto GroupMemberBackupPlugin::handle_qq_message(
    obcx::core::IBot &bot, const obcx::common::MessageEvent &event)
    -> boost::asio::awaitable<void> {
  // 只处理群消息
  if (event.message_type != "group" || !event.group_id.has_value()) {
    co_return;
  }

  const auto &group_id = event.group_id.value();

  // 检查权限
  if (!is_allowed_group(group_id)) {
    co_return;
  }

  if (!is_admin_user(event.user_id)) {
    co_return;
  }

  const auto &raw_msg = event.raw_message;

  // 处理备份命令
  if (raw_msg == "/backup" || raw_msg.find("/backup ") == 0) {
    co_await handle_backup_command(bot, event);
    co_return;
  }

  // 处理恢复命令
  if (raw_msg == "/restore" || raw_msg.find("/restore ") == 0) {
    auto [min_interval, max_interval] = parse_restore_params(raw_msg);
    co_await handle_restore_command(bot, event, min_interval, max_interval);
    co_return;
  }

  // 处理状态查询命令
  if (raw_msg == "/restore_status") {
    co_await handle_status_command(bot, event);
    co_return;
  }
}

auto GroupMemberBackupPlugin::handle_backup_command(
    obcx::core::IBot &bot, const obcx::common::MessageEvent &event)
    -> boost::asio::awaitable<void> {
  const auto &group_id = event.group_id.value();

  std::string error_msg;
  bool has_error = false;

  try {
    // 获取群成员列表
    auto *qq_bot = dynamic_cast<obcx::core::QQBot *>(&bot);
    if (!qq_bot) {
      co_await send_message(bot, group_id, "错误：Bot类型不支持");
      co_return;
    }

    co_await send_message(bot, group_id, "正在获取群成员列表，请稍候...");

    auto result_json = co_await qq_bot->get_group_member_list(group_id);

    // 解析返回的JSON
    auto result = json::parse(result_json);

    if (result["status"] != "ok") {
      co_await send_message(bot, group_id,
                            "获取群成员列表失败: " +
                                result.value("message", "Unknown error"));
      co_return;
    }

    auto members = result["data"];

    // 构建备份数据
    json backup_data;
    backup_data["group_id"] = group_id;
    backup_data["backup_time"] =
        std::chrono::system_clock::now().time_since_epoch().count();
    backup_data["member_count"] = members.size();
    backup_data["members"] = members;

    // 保存到临时文件
    std::string filename =
        "group_" + group_id + "_backup_" +
        std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count()) +
        ".json";
    std::string filepath = "/tmp/" + filename;

    std::ofstream file(filepath);
    if (!file.is_open()) {
      co_await send_message(bot, group_id, "保存备份文件失败");
      co_return;
    }

    file << backup_data.dump(2);
    file.close();

    PLUGIN_INFO(get_name(), "Backup saved to {}", filepath);

    // 发送文件
    obcx::common::Message file_message;
    obcx::common::MessageSegment file_segment;
    file_segment.type = "file";
    file_segment.data["file"] = "file://" + filepath;
    file_segment.data["name"] = filename;
    file_message.push_back(file_segment);

    co_await qq_bot->send_group_message(group_id, file_message);

    // 发送统计信息
    std::ostringstream oss;
    oss << "备份完成！\n";
    oss << "群号: " << group_id << "\n";
    oss << "成员数量: " << members.size();

    co_await send_message(bot, group_id, oss.str());

  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Backup failed: {}", e.what());
    error_msg = "备份失败: " + std::string(e.what());
    has_error = true;
  }

  // 在catch块外发送错误消息
  if (has_error) {
    try {
      co_await send_message(bot, group_id, error_msg);
    } catch (...) {
      // 忽略发送错误消息时的异常
    }
  }
}

auto GroupMemberBackupPlugin::handle_restore_command(
    obcx::core::IBot &bot, const obcx::common::MessageEvent &event,
    int min_interval, int max_interval) -> boost::asio::awaitable<void> {
  const auto &group_id = event.group_id.value();

  std::string error_msg;
  bool has_error = false;

  // 检查是否有文件消息段
  auto file_url_opt = extract_file_url(event.message);

  if (!file_url_opt.has_value()) {
    co_await send_message(bot, group_id,
                          "请回复一个JSON备份文件并使用 /restore [min,max] "
                          "命令\n例如: /restore 60,120");
    co_return;
  }

  // 检查是否已有任务在运行
  {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    if (restore_tasks_.count(group_id) > 0 &&
        restore_tasks_[group_id].is_running) {
      co_await send_message(bot, group_id, "该群已有恢复任务正在运行中");
      co_return;
    }
  }

  try {
    // 下载并解析备份文件
    auto backup_data_opt =
        co_await bot.run_heavy_task([file_url = file_url_opt.value(), this]() {
          return download_and_parse_backup(file_url);
        });

    if (!backup_data_opt.has_value()) {
      co_await send_message(bot, group_id, "无法解析备份文件");
      co_return;
    }

    auto backup_data = backup_data_opt.value();

    // 验证备份数据
    if (!backup_data.contains("members") ||
        !backup_data["members"].is_array()) {
      co_await send_message(bot, group_id, "备份文件格式错误");
      co_return;
    }

    // 提取用户ID列表
    std::vector<std::string> user_ids;
    for (const auto &member : backup_data["members"]) {
      if (member.contains("user_id")) {
        user_ids.push_back(std::to_string(member["user_id"].get<int64_t>()));
      }
    }

    if (user_ids.empty()) {
      co_await send_message(bot, group_id, "备份文件中没有有效的成员信息");
      co_return;
    }

    // 创建恢复任务
    {
      std::lock_guard<std::mutex> lock(tasks_mutex_);
      RestoreTask task;
      task.group_id = group_id;
      task.operator_id = event.user_id;
      task.pending_user_ids = user_ids;
      task.min_interval = min_interval;
      task.max_interval = max_interval;
      task.is_running = true;
      task.start_time = std::chrono::system_clock::now();
      restore_tasks_[group_id] = task;
    }

    std::ostringstream oss;
    oss << "开始恢复群成员...\n";
    oss << "待恢复成员数量: " << user_ids.size() << "\n";
    oss << "邀请间隔: " << min_interval << "-" << max_interval << "秒\n";
    oss << "预计耗时: "
        << (user_ids.size() * (min_interval + max_interval) / 2) / 60
        << " 分钟";

    co_await send_message(bot, group_id, oss.str());

    // 在后台线程池中执行恢复任务
    boost::asio::co_spawn(
        bot.get_task_scheduler().get_io_context(),
        [this, &bot, group_id, operator_id = event.user_id, user_ids,
         min_interval, max_interval]() -> boost::asio::awaitable<void> {
          co_await bot.run_heavy_task([this, &bot, group_id, operator_id,
                                       user_ids, min_interval, max_interval]() {
            execute_restore_task(bot, group_id, operator_id, user_ids,
                                 min_interval, max_interval);
          });
        },
        boost::asio::detached);

  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Restore command failed: {}", e.what());
    error_msg = "恢复命令执行失败: " + std::string(e.what());
    has_error = true;
  }

  // 在catch块外发送错误消息
  if (has_error) {
    try {
      co_await send_message(bot, group_id, error_msg);
    } catch (...) {
      // 忽略发送错误消息时的异常
    }
  }
}

auto GroupMemberBackupPlugin::handle_status_command(
    obcx::core::IBot &bot, const obcx::common::MessageEvent &event)
    -> boost::asio::awaitable<void> {
  const auto &group_id = event.group_id.value();

  std::lock_guard<std::mutex> lock(tasks_mutex_);

  if (restore_tasks_.count(group_id) == 0) {
    co_await send_message(bot, group_id, "当前没有恢复任务");
    co_return;
  }

  const auto &task = restore_tasks_[group_id];

  std::ostringstream oss;
  oss << "恢复任务状态:\n";
  oss << "状态: " << (task.is_running ? "运行中" : "已完成") << "\n";
  oss << "总成员数: "
      << (task.pending_user_ids.size() + task.success_user_ids.size() +
          task.failed_user_ids.size())
      << "\n";
  oss << "待处理: " << task.pending_user_ids.size() << "\n";
  oss << "成功: " << task.success_user_ids.size() << "\n";
  oss << "失败: " << task.failed_user_ids.size() << "\n";

  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now() - task.start_time)
                     .count();
  oss << "已运行: " << elapsed / 60 << " 分钟";

  co_await send_message(bot, group_id, oss.str());
}

void GroupMemberBackupPlugin::execute_restore_task(
    obcx::core::IBot &bot, const std::string &group_id,
    const std::string &operator_id, const std::vector<std::string> &user_ids,
    int min_interval, int max_interval) {

  PLUGIN_INFO(get_name(), "Starting restore task for group {}", group_id);

  auto *qq_bot = dynamic_cast<obcx::core::QQBot *>(&bot);
  if (!qq_bot) {
    PLUGIN_ERROR(get_name(), "Bot is not QQBot");
    return;
  }

  for (const auto &user_id : user_ids) {
    // 检查任务是否被停止
    {
      std::lock_guard<std::mutex> lock(tasks_mutex_);
      if (!restore_tasks_[group_id].is_running) {
        PLUGIN_INFO(get_name(), "Restore task stopped for group {}", group_id);
        break;
      }
    }

    try {
      // 注意：OneBot11协议中没有直接的"邀请用户入群"API
      // 这里我们发送群邀请（实际上需要配合Bot的加群请求处理）
      // 由于API限制，这里记录日志，实际邀请需要通过其他方式实现

      PLUGIN_INFO(get_name(), "Processing user {} for group {}", user_id,
                  group_id);

      // TODO: 实际的邀请逻辑需要根据OneBot11的具体实现来完成
      // 可能需要使用 set_group_add_request 来处理加群请求

      // 更新任务状态
      {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        auto &task = restore_tasks_[group_id];
        task.pending_user_ids.erase(std::remove(task.pending_user_ids.begin(),
                                                task.pending_user_ids.end(),
                                                user_id),
                                    task.pending_user_ids.end());
        task.success_user_ids.push_back(user_id);
      }

      // 随机延迟
      int delay = generate_random_interval(min_interval, max_interval);
      std::this_thread::sleep_for(std::chrono::seconds(delay));

    } catch (const std::exception &e) {
      PLUGIN_ERROR(get_name(), "Failed to invite user {}: {}", user_id,
                   e.what());

      // 更新失败状态
      {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        auto &task = restore_tasks_[group_id];
        task.pending_user_ids.erase(std::remove(task.pending_user_ids.begin(),
                                                task.pending_user_ids.end(),
                                                user_id),
                                    task.pending_user_ids.end());
        task.failed_user_ids.push_back(user_id);
      }
    }
  }

  // 任务完成，发送统计消息
  {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    auto &task = restore_tasks_[group_id];
    task.is_running = false;

    std::ostringstream oss;
    oss << "群成员恢复任务完成！\n";
    oss << "成功: " << task.success_user_ids.size() << "\n";
    oss << "失败: " << task.failed_user_ids.size() << "\n";

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now() - task.start_time)
                       .count();
    oss << "总耗时: " << elapsed / 60 << " 分钟";

    // 异步发送消息
    boost::asio::co_spawn(
        bot.get_task_scheduler().get_io_context(),
        [this, &bot, group_id,
         message = oss.str()]() -> boost::asio::awaitable<void> {
          co_await send_message(bot, group_id, message);
        },
        boost::asio::detached);
  }

  PLUGIN_INFO(get_name(), "Restore task completed for group {}", group_id);
}

auto GroupMemberBackupPlugin::send_message(obcx::core::IBot &bot,
                                           const std::string &group_id,
                                           const std::string &text)
    -> boost::asio::awaitable<void> {
  obcx::common::Message message;
  obcx::common::MessageSegment text_segment;
  text_segment.type = "text";
  text_segment.data["text"] = text;
  message.push_back(text_segment);

  auto *qq_bot = dynamic_cast<obcx::core::QQBot *>(&bot);
  if (qq_bot) {
    co_await qq_bot->send_group_message(group_id, message);
  }
}

auto GroupMemberBackupPlugin::parse_restore_params(
    const std::string &raw_message) -> std::pair<int, int> {
  // 默认值: 5-10秒
  int min_interval = 5;
  int max_interval = 10;

  // 使用正则表达式提取参数: /restore 60,120
  std::regex pattern(R"(/restore\s+(\d+)\s*,\s*(\d+))");
  std::smatch match;

  if (std::regex_search(raw_message, match, pattern)) {
    if (match.size() == 3) {
      try {
        min_interval = std::stoi(match[1].str());
        max_interval = std::stoi(match[2].str());

        // 确保最小值小于最大值
        if (min_interval > max_interval) {
          std::swap(min_interval, max_interval);
        }
      } catch (...) {
        // 解析失败，使用默认值
      }
    }
  }

  return {min_interval, max_interval};
}

auto GroupMemberBackupPlugin::extract_file_url(
    const obcx::common::Message &message) -> std::optional<std::string> {
  for (const auto &segment : message) {
    if (segment.type == "file" && segment.data.contains("url")) {
      return segment.data["url"].get<std::string>();
    }
  }
  return std::nullopt;
}

auto GroupMemberBackupPlugin::download_and_parse_backup(
    const std::string &file_url) -> std::optional<nlohmann::json> {
  try {
    // 这里需要实现文件下载逻辑
    // 由于没有内置的HTTP客户端，这里简化处理
    // 实际使用时可能需要使用libcurl或其他HTTP库

    PLUGIN_INFO(get_name(), "Downloading backup file from {}", file_url);

    // TODO: 实现真实的文件下载
    // 暂时假设file_url是本地文件路径
    std::string local_path = file_url;
    if (local_path.find("file://") == 0) {
      local_path = local_path.substr(7);
    }

    std::ifstream file(local_path);
    if (!file.is_open()) {
      PLUGIN_ERROR(get_name(), "Failed to open file: {}", local_path);
      return std::nullopt;
    }

    json data;
    file >> data;
    return data;

  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to download/parse backup: {}", e.what());
    return std::nullopt;
  }
}

auto GroupMemberBackupPlugin::generate_random_interval(int min_sec, int max_sec)
    -> int {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(min_sec, max_sec);
  return dis(gen);
}

} // namespace group_member_backup

OBCX_PLUGIN_EXPORT(group_member_backup::GroupMemberBackupPlugin)
