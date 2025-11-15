/**
 * @file advanced_bot.cpp
 * @brief OneBot 11 协议高级功能示例
 *
 * 本示例展示了如何正确使用OBCX框架的同步API来获取信息，
 * 以及如何处理各种OneBot 11协议事件。
 */

#include "core/qq_bot.hpp"
#include "common/json_utils.hpp"
#include "common/logger.hpp"
#include "common/message_type.hpp"
#include "interfaces/bot.hpp"

#include <boost/asio.hpp>
#include <iostream>
#include <memory>
#include <spdlog/common.h>

using namespace obcx;
using namespace std::chrono_literals;
namespace asio = boost::asio;

constexpr std::string_view TEST_GROUP_ID = "114514";

namespace {
void print_api_response(std::string_view response, std::string_view api_name) {
  try {

    nlohmann::json j = nlohmann::json::parse(response);

    if (j.contains("retcode") && j["retcode"] == 0) {
      OBCX_INFO("{} 成功返回:", api_name);
      if (j.contains("data")) {
        // 限制数据输出大小
        std::string data_str = j["data"].dump(2);
        std::cout << "响应数据: " << data_str << '\n';
      }
    } else {
      // API调用失败
      int retcode = j.value("retcode", -1);
      std::string message = j.value("message", "未知错误");
      OBCX_ERROR("{} 失败: retcode={}, message={}", api_name, retcode, message);
    }
  } catch (const nlohmann::json::exception &e) {
    OBCX_ERROR("解析{}响应失败: {}", api_name, e.what());
  }
}

auto private_message_handler(core::IBot &bot, common::MessageEvent event)
    -> asio::awaitable<void> {
  if (event.message_type != "private") {
    co_return;
  }
  OBCX_INFO("收到消息: 来自用户 {} 的 {} 消息: {}", event.user_id,
            event.message_type, event.raw_message);
  if (event.raw_message == "/info") {
    try {
      OBCX_INFO("正在获取登录信息...");
      std::string response = co_await bot.get_login_info();
      print_api_response(response, "获取登录信息");
    } catch (const std::exception &e) {
      OBCX_ERROR("获取登录信息失败: {}", e.what());
    }
  } else if (event.raw_message == "/friends") {
    try {
      OBCX_INFO("正在获取好友列表...");
      std::string response = co_await bot.get_friend_list();
      print_api_response(response, "获取好友列表");
    } catch (const std::exception &e) {
      OBCX_ERROR("获取好友列表失败: {}", e.what());
    }
  } else if (event.raw_message == "/groups") {
    // 获取群列表
    try {
      OBCX_INFO("正在获取群列表...");
      std::string response = co_await bot.get_group_list();
      print_api_response(response, "获取群列表");
    } catch (const std::exception &e) {
      OBCX_ERROR("获取群列表失败: {}", e.what());
    }
  } else if (event.raw_message.starts_with("/userinfo ")) {
    std::string user_id_param = event.raw_message.substr(10);
    if (!user_id_param.empty()) {
      try {
        OBCX_INFO("正在获取用户 {} 的信息...", user_id_param);
        std::string response = co_await bot.get_stranger_info(user_id_param);
        print_api_response(response, "获取用户信息");
      } catch (const std::exception &e) {
        OBCX_ERROR("获取用户信息失败: {}", e.what());
      }
    }
  } else if (event.raw_message.find("/help") != std::string::npos) {
    common::Message help_msg = {
        {.type = {"text"},
         .data = {{"text", "可用命令:\n"
                           "/help - 显示帮助\n"
                           "/info - 获取登录信息\n"
                           "/friends - 获取好友列表\n"
                           "/groups - 获取群列表\n"
                           "/userinfo <用户ID> - 获取用户信息"}}}};
    co_await bot.send_private_message(event.user_id, help_msg);
  } else {
    std::string image_url = "";
    for (const auto &seg : event.message) {
      if (seg.type == "image") {
        image_url += static_cast<std::string>(seg.data["url"]) + "\n";
      }
    }
    common::Message reply_msg = {
        {.type = {"text"},
         .data = {
             {"text",
              "你发送了: " + event.raw_message +
                  (image_url.empty() ? "" : "\n图片链接: " + image_url)}}}};
    co_await bot.send_private_message(event.user_id, reply_msg);
  }
}

auto group_message_handler(core::IBot &bot, common::MessageEvent event)
    -> asio::awaitable<void> {
  if (event.message_type != "group" || !event.group_id.has_value() ||
      event.group_id.value() != TEST_GROUP_ID) {
    co_return;
  }
  auto group_id = event.group_id.value();
  OBCX_DEBUG("收到群消息: 来自群 {} 的消息: {}", group_id, event.raw_message);

  if (event.raw_message == "/groupinfo") {
    // 获取群信息
    try {
      OBCX_INFO("正在获取群信息...");
      std::string response = co_await bot.get_group_info(group_id);

      try {
        nlohmann::json j = nlohmann::json::parse(response);
        if (j.contains("data")) {
          auto data = j["data"];
          std::string group_name =
              data.value("group_name", std::string("未知"));
          int64_t group_id_val = data.value("group_id", (int64_t)0);
          int member_count = data.value("member_count", 0);
          int max_member_count = data.value("max_member_count", 0);

          std::string reply_text =
              "群聊信息:\n"
              "群名: " +
              group_name + "\n" + "群号: " + std::to_string(group_id_val) +
              "\n" + "成员: " + std::to_string(member_count) + "/" +
              std::to_string(max_member_count);

          common::Message message_to_send = {
              {.type = {"text"}, .data = {{"text", reply_text}}}};
          co_await bot.send_group_message(group_id, message_to_send);
        } else {
          OBCX_ERROR("群信息响应中缺少 'data' 字段");
          co_await bot.send_group_message(
              group_id,
              common::Message{
                  {.type = {"text"},
                   .data = {{"text", "无法解析群信息，响应格式不正确。"}}}});
        }
      } catch (const std::exception &e) {
        OBCX_ERROR("处理群信息时发生内部错误: {}", e.what());
        bot.error_notify(group_id,
                         fmt::format("抱歉，处理群信息时出错啦: {}", e.what()),
                         true);
      }
    } catch (const std::exception &e) {
      OBCX_ERROR("获取群信息失败: {}", e.what());
    }
  } else if (event.raw_message == "/members") {
    try {
      OBCX_INFO("正在获取群成员列表...");
      std::string response = co_await bot.get_group_member_list(group_id);
      print_api_response(response, "获取群成员列表");

      nlohmann::json j = nlohmann::json::parse(response);
      if (j.contains("retcode") && j["retcode"] == 0 && j.contains("data")) {
        auto data = j["data"];
        int member_count = data.size();

        common::Message count_msg = {
            {.type = {"text"},
             .data = {
                 {"text", "群成员总数: " + std::to_string(member_count)}}}};
        co_await bot.send_group_message(group_id, count_msg);
      }
    } catch (const std::exception &e) {
      bot.error_notify(group_id,
                       fmt::format("抱歉，处理群信息时出错啦。{}", e.what()),
                       true);
    }
  } else if (event.raw_message.starts_with("/group-rename ")) {
    OBCX_DEBUG("Try to rename group");
    std::string group_name = event.raw_message.substr(14);
    if (group_name.empty()) {
      co_return;
    }
    try {
      std::string response = co_await bot.set_group_name(group_id, group_name);

    } catch (std::exception &e) {
      OBCX_ERROR("{}", e.what());
      bot.error_notify(group_id,
                       fmt::format("Failed to rename group: {}", e.what()));
    }
  } else if (event.raw_message.starts_with("/memberinfo ")) {
    OBCX_DEBUG("尝试获取群成员信息");
    std::string target_user_id = event.raw_message.substr(12);
    if (!target_user_id.empty()) {
      try {
        OBCX_INFO("正在获取群成员 {} 的信息...", target_user_id);
        std::string response =
            co_await bot.get_group_member_info(group_id, target_user_id);
        print_api_response(response, "获取群成员信息");

        nlohmann::json j = nlohmann::json::parse(response);
        if (j.contains("retcode") && j["retcode"] == 0 && j.contains("data")) {
          auto data = j["data"];
          std::string nickname = data.value("nickname", std::string("未知"));
          std::string card = data.value("card", std::string(""));
          std::string role = data.value("role", std::string("member"));

          std::string info_text = "用户信息:\n昵称: " + nickname;
          if (!card.empty()) {
            info_text += "\n群名片: " + card;
          }
          info_text += "\n角色: " + role;

          common::Message info_msg = {
              {.type = {"text"}, .data = {{"text", info_text}}}};
          co_await bot.send_group_message(group_id, info_msg);
        } else {
          throw std::runtime_error(
              "获取群成员信息失败: 响应中缺少 'data' 字段");
        }
      } catch (const std::exception &e) {
        OBCX_ERROR("获取群成员信息失败: {}", e.what());
        bot.error_notify(group_id,
                         fmt::format("获取群成员信息失败: {}", e.what()), true);
      }
    }
  } else if (event.raw_message.starts_with("/kick ")) {
    OBCX_DEBUG("尝试踢出群成员");
    std::string user_id = event.raw_message.substr(6);
    if (!user_id.empty()) {
      auto res = co_await bot.set_group_kick(group_id, user_id, false);
      common::Message kick_msg = {
          {.type = {"text"},
           .data = {{"text", "已执行踢出操作: " + user_id + " " + res}}}};
      co_await bot.send_group_message(group_id, kick_msg);
    }
  } else if (event.raw_message.starts_with("/ban ")) {
    OBCX_DEBUG("尝试禁言群成员");
    std::string params = event.raw_message.substr(5);
    size_t space_pos = params.find(' ');

    if (space_pos != std::string::npos) {
      std::string user_id = params.substr(0, space_pos);
      int32_t duration = std::stoi(params.substr(space_pos + 1));

      auto response = co_await bot.set_group_ban(group_id, user_id, duration);
      common::json j = common::json::parse(response);

      auto ban_msg =
          (j["status"] == "failed")
              ? common::Message{{.type = {"text"},
                                 .data = {{"text", "failed to ban"}}}}
              : common::Message{
                    {.type = {"text"},
                     .data = {{"text", "已禁言用户 " + user_id + " " +
                                           std::to_string(duration) + " 秒"}}}};
      co_await bot.send_group_message(group_id, ban_msg);
    }
  } else if (event.raw_message == "/help") {
    OBCX_DEBUG("尝试发送群聊帮助");
    common::Message help_msg = {
        {.type = {"text"},
         .data = {{"text", "群聊可用命令:\n"
                           "/groupinfo - 获取群信息\n"
                           "/members - 获取群成员数量\n"
                           "/memberinfo <用户ID> - 获取群成员信息\n"
                           "/kick <用户ID> - 踢出群成员(需要权限)\n"
                           "/ban <用户ID> <时长秒数> - 禁言用户(需要权限)"}}}};
    co_await bot.send_group_message(group_id, help_msg);
  }
}
} // namespace

auto main() -> int {
  common::Logger::initialize(spdlog::level::err);

  try {
    // 创建OneBot11协议适配器
    adapter::onebot11::ProtocolAdapter onebotAdapter;
    std::unique_ptr<core::IBot> bot =
        std::make_unique<core::QQBot>(std::move(onebotAdapter));

    bot->on_event<common::MessageEvent>(private_message_handler);
    bot->on_event<common::MessageEvent>(group_message_handler);

    // 注册异常事件处理器
    bot->on_event<common::ErrorEvent>(
        [](core::IBot &bot, common::ErrorEvent event) -> asio::awaitable<void> {
          OBCX_WARN("收到异常事件: 类型={}, 消息={}, 目标={}, 是否为群组={}",
                    event.error_type, event.error_message, event.target_id,
                    event.is_group);

          // 根据异常类型和目标进行相应处理
          if (event.error_type == "message_error") {
            try {
              // 创建错误提示消息
              common::Message error_msg = {
                  {.type = {"text"},
                   .data = {{"text", "抱歉，处理您的消息时出现了问题: " +
                                         event.error_message}}}};

              // 发送错误提示消息
              if (event.is_group) {
                co_await bot.send_group_message(event.target_id, error_msg);
              } else {
                co_await bot.send_private_message(event.target_id, error_msg);
              }

              OBCX_INFO("已向目标 {} 发送错误提示消息", event.target_id);
            } catch (const std::exception &e) {
              OBCX_ERROR("发送错误提示消息失败: {}", e.what());
            }
          }

          co_return;
        });

    // bot->on_event<common::RequestEvent>(
    //[](core::Bot &bot,
    // common::RequestEvent event) -> asio::awaitable<void> {
    // OBCX_INFO("收到请求事件: 类型 {}, 用户 {}, 消息: {}",
    // event.request_type, event.user_id, event.comment);

    // if (event.request_type == "friend") {
    //// 自动同意好友请求
    // co_await bot.set_friend_add_request(event.flag, true,
    //"通过机器人自动添加");
    // OBCX_INFO("已自动同意来自用户 {} 的好友申请", event.user_id);
    //} else if (event.request_type == "group") {
    //// 自动同意加群请求
    // std::string sub_type =
    // event.comment.find("邀请") != std::string::npos ? "invite"
    //: "add";
    // co_await bot.set_group_add_request(event.flag, sub_type, true);
    // OBCX_INFO("已自动同意来自用户 {} 的{}请求", event.user_id,
    // sub_type == "invite" ? "邀请" : "加群");
    //}

    // co_return;
    //});

    // bot->on_event<common::NoticeEvent>([](core::Bot &bot,
    // common::NoticeEvent event)
    //-> asio::awaitable<void> {
    // if (!event.group_id.has_value() ||
    // event.group_id.value() != TEST_GROUP_ID) {
    // co_return;
    //}
    // OBCX_INFO("收到通知事件: 类型 {}, 用户 {}", event.notice_type,
    // event.user_id);

    // if (event.notice_type == "group_increase") {
    //// 有新成员加入群聊
    // std::string group_id = event.group_id.value();
    // common::Message welcome_msg = {
    //{.type = {"at"}, .data = {{"qq", event.user_id}}},
    //{.type = {"text"},
    //.data = {{"text", " 欢迎加入群聊！发送 /help 查看可用命令。"}}}};
    // co_await bot.send_group_message(group_id, welcome_msg);
    //} else if (event.notice_type == "group_decrease") {
    // common::Message leave_msg = {
    //{.type = {"text"}, .data = {{"text", "一位成员离开了群聊"}}}};
    // co_await bot.send_group_message(event.group_id.value(), leave_msg);
    //}
    // co_return;
    //});

    // 连接到 OneBot 实现
    std::string host = "127.0.0.1";
    uint16_t port = 3001;
    std::string access_token; // 如果需要认证，请填写 access_token

    OBCX_INFO("正在连接到 OneBot 实现: {}:{}", host, port);

    // 使用新的连接接口 - 通过工厂创建WebSocket连接管理器
    obcx::common::ConnectionConfig config;
    config.host = host;
    config.port = port;
    config.access_token = access_token;
    config.timeout = std::chrono::milliseconds(30000); // 30秒超时

    bot->connect(
        network::ConnectionManagerFactory::ConnectionType::Onebot11WebSocket,
        config);

    static bool connection_confirmed = true;

    bot->on_event<common::HeartbeatEvent>(
        [](core::IBot &bot,
           common::HeartbeatEvent event) -> asio::awaitable<void> {
          if (!connection_confirmed) {
            connection_confirmed = true;
            OBCX_INFO("收到第一次心跳，连接已建立！");

            try {
              OBCX_INFO("测试获取登录信息...");
              std::string login_response = co_await bot.get_login_info();
              print_api_response(login_response, "获取登录信息");

              OBCX_INFO("测试获取好友列表...");
              std::string friends_response = co_await bot.get_friend_list();
              print_api_response(friends_response, "获取好友列表");

              OBCX_INFO("测试获取群列表...");
              std::string groups_response = co_await bot.get_group_list();
              print_api_response(groups_response, "获取群列表");

              OBCX_INFO("API测试完成，现在可以发送消息测试其他功能了！");
            } catch (const std::exception &e) {
              OBCX_ERROR("API测试失败: {}", e.what());
            }
          }

          co_return;
        });
    bot->run();

  } catch (const std::exception &e) {
    OBCX_ERROR("程序异常: {}", e.what());
    return 1;
  }

  OBCX_INFO("程序正常退出");
  return 0;
}
