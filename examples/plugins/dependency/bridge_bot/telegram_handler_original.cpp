#include "config.hpp"
#include "media_processor.hpp"
#include "telegram_handler.hpp"

#include "common/logger.hpp"
#include "common/media_converter.hpp"
#include "core/tg_bot.hpp"
#include "path_manager.hpp"
#include "telegram/network/connection_manager.hpp"

#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <nlohmann/json.hpp>

namespace bridge {

TelegramHandler::TelegramHandler(
    std::shared_ptr<obcx::storage::DatabaseManager> db_manager)
    : db_manager_(db_manager) {}

auto TelegramHandler::forward_to_qq(obcx::core::IBot &telegram_bot,
                                    obcx::core::IBot &qq_bot,
                                    obcx::common::MessageEvent event)
    -> boost::asio::awaitable<void> {
  // 确保是群消息
  if (event.message_type != "group" || !event.group_id.has_value()) {
    co_return;
  }

  const std::string telegram_group_id = event.group_id.value();
  std::string qq_group_id;
  const GroupBridgeConfig *bridge_config = nullptr;

  // 查找对应的QQ群ID和桥接配置
  const auto it = GROUP_MAP.find(telegram_group_id);
  if (it == GROUP_MAP.end()) {
    OBCX_DEBUG("Telegram群 {} 没有对应的QQ群配置", telegram_group_id);
    co_return;
  }
  bridge_config = &it->second;

  // 根据桥接模式处理转发逻辑
  if (bridge_config->mode == BridgeMode::GROUP_TO_GROUP) {
    // 群组模式：整个TG群对应一个QQ群
    qq_group_id = bridge_config->qq_group_id;
    OBCX_DEBUG("群组模式：Telegram群 {} 转发到QQ群 {}", telegram_group_id,
               qq_group_id);
  } else {
    // Topic模式：根据topic ID查找对应的QQ群
    int64_t message_thread_id = -1;
    if (event.data.contains("message_thread_id")) {
      message_thread_id = event.data["message_thread_id"].get<int64_t>();
    }

    // 查找对应的topic配置
    const TopicBridgeConfig *topic_config =
        get_topic_config(telegram_group_id, message_thread_id);
    if (!topic_config) {
      OBCX_DEBUG("Telegram消息来自topic {}，没有对应的QQ群配置，跳过转发",
                 message_thread_id);
      co_return;
    }

    qq_group_id = topic_config->qq_group_id;
    OBCX_DEBUG("Topic模式：Telegram topic {} 转发到QQ群 {}", message_thread_id,
               qq_group_id);
  }

  // 检查是否是 /recall 命令
  if (event.raw_message.starts_with("/recall")) {
    OBCX_INFO("检测到 /recall 命令，处理撤回请求");
    co_await handle_recall_command(telegram_bot, qq_bot, event, qq_group_id);
    co_return;
  }

  // 检查是否是回环消息（从QQ转发过来的）
  if (event.raw_message.starts_with("[QQ] ")) {
    OBCX_DEBUG("检测到可能是回环的QQ消息，跳过转发");
    co_return;
  }

  // 检查消息是否已转发（避免重复）
  if (db_manager_->get_target_message_id("telegram", event.message_id, "qq")
          .has_value()) {
    OBCX_DEBUG("Telegram消息 {} 已转发到QQ，跳过重复处理", event.message_id);
    co_return;
  }

  OBCX_INFO("准备从Telegram群 {} 转发消息到QQ群 {}", telegram_group_id,
            qq_group_id);

  // 用于收集下载的临时文件路径，以便发送后清理
  std::vector<std::string> temp_files_to_cleanup;

  try {
    // 保存/更新用户信息
    db_manager_->save_user_from_event(event, "telegram");
    // 保存消息信息
    db_manager_->save_message_from_event(event, "telegram");

    // 构造转发消息
    std::string sender_display_name =
        db_manager_->get_user_display_name("telegram", event.user_id, "");

    // 创建转发消息，保留原始消息的所有段（包括图片）
    obcx::common::Message message_to_send;

    // 检查是否有引用消息，reply段必须放在消息开头
    if (event.data.contains("reply_to_message")) {
      auto reply_to_message = event.data["reply_to_message"];
      if (reply_to_message.is_object() &&
          reply_to_message.contains("message_id")) {

        // 在Topic中，需要区分Topic结构回复和用户主动回复
        bool is_genuine_reply = true;
        if (event.data.contains("message_thread_id")) {
          int64_t thread_id = event.data["message_thread_id"].get<int64_t>();
          int64_t reply_msg_id = reply_to_message["message_id"].get<int64_t>();

          // 如果回复的是Topic根消息（message_thread_id），这是Topic结构回复，不是用户主动回复
          if (reply_msg_id == thread_id) {
            is_genuine_reply = false;
            OBCX_DEBUG("检测到Topic结构回复 (回复Topic根消息 {})，跳过回复处理",
                       thread_id);
          }
        }

        if (is_genuine_reply) {
          std::string reply_message_id =
              std::to_string(reply_to_message["message_id"].get<int64_t>());
          OBCX_DEBUG("检测到Telegram用户主动回复消息，引用ID: {}",
                     reply_message_id);

          std::optional<std::string> target_qq_message_id;

          // 首先查找该Telegram消息是否已被转发到QQ（用户回复的是已转发的消息）
          target_qq_message_id = db_manager_->get_target_message_id(
              "telegram", reply_message_id, "qq");

          // 如果没找到，再查找该消息是否来源于QQ（用户回复的是从QQ转发过来的消息）
          if (!target_qq_message_id.has_value()) {
            target_qq_message_id = db_manager_->get_source_message_id(
                "telegram", reply_message_id, "qq");
          }

          OBCX_DEBUG(
              "查找结果 - Telegram消息 {} 对应的QQ消息ID: {}", reply_message_id,
              target_qq_message_id.has_value() ? target_qq_message_id.value()
                                               : "未找到");

          if (target_qq_message_id.has_value()) {
            // 创建QQ引用消息段，必须放在消息开头
            obcx::common::MessageSegment reply_segment;
            reply_segment.type = "reply";
            reply_segment.data["id"] = target_qq_message_id.value();
            message_to_send.push_back(reply_segment);
            OBCX_DEBUG("添加QQ引用消息段，引用ID: {}",
                       target_qq_message_id.value());
          } else {
            OBCX_DEBUG(
                "未找到Telegram引用消息对应的QQ消息ID，可能是原生Telegram消息");
          }
        }
      }
    }

    // 根据配置决定是否添加发送者信息
    bool show_sender = false;
    if (bridge_config->mode == BridgeMode::GROUP_TO_GROUP) {
      show_sender = bridge_config->show_tg_to_qq_sender;
    } else {
      // Topic模式：获取对应topic的配置
      int64_t message_thread_id = -1;
      if (event.data.contains("message_thread_id")) {
        message_thread_id = event.data["message_thread_id"].get<int64_t>();
      }
      const TopicBridgeConfig *topic_config =
          get_topic_config(telegram_group_id, message_thread_id);
      show_sender = topic_config ? topic_config->show_tg_to_qq_sender : false;
    }

    if (show_sender) {
      std::string sender_info = fmt::format("[{}]\t", sender_display_name);

      // 添加发送者信息作为文本段
      obcx::common::MessageSegment sender_segment;
      sender_segment.type = "text";

      // 如果没有找到对应的回复消息ID，在发送者信息中加上回复提示
      bool has_reply = false;
      bool has_genuine_reply = false;
      if (event.data.contains("reply_to_message")) {
        // 检查message_to_send中是否已经添加了reply段
        has_reply =
            !message_to_send.empty() && message_to_send[0].type == "reply";

        // 检查是否是用户主动回复（排除Topic结构回复）
        if (event.data.contains("message_thread_id")) {
          auto reply_to_message = event.data["reply_to_message"];
          int64_t thread_id = event.data["message_thread_id"].get<int64_t>();
          int64_t reply_msg_id = reply_to_message["message_id"].get<int64_t>();
          has_genuine_reply = (reply_msg_id != thread_id);
        } else {
          has_genuine_reply = true; // 非Topic消息的回复都是真回复
        }
      }
      sender_segment.data["text"] =
          has_reply ? sender_info
                    : (has_genuine_reply ? sender_info + "[回复一条消息] "
                                         : sender_info);
      message_to_send.push_back(sender_segment);
      OBCX_DEBUG("Telegram到QQ转发显示发送者：{}", sender_display_name);
    } else {
      // 不显示发送者，但如果有回复需要添加提示
      if (event.data.contains("reply_to_message")) {
        bool has_reply =
            !message_to_send.empty() && message_to_send[0].type == "reply";

        // 检查是否是用户主动回复（排除Topic结构回复）
        bool has_genuine_reply = false;
        if (event.data.contains("message_thread_id")) {
          auto reply_to_message = event.data["reply_to_message"];
          int64_t thread_id = event.data["message_thread_id"].get<int64_t>();
          int64_t reply_msg_id = reply_to_message["message_id"].get<int64_t>();
          has_genuine_reply = (reply_msg_id != thread_id);
        } else {
          has_genuine_reply = true; // 非Topic消息的回复都是真回复
        }

        if (!has_reply && has_genuine_reply) {
          obcx::common::MessageSegment reply_tip_segment;
          reply_tip_segment.type = "text";
          reply_tip_segment.data["text"] = "[回复一条消息] ";
          message_to_send.push_back(reply_tip_segment);
        }
      }
      OBCX_DEBUG("Telegram到QQ转发不显示发送者");
    }

    // 处理消息中的不同文件类型
    auto handle_telegram_media =
        [&](const std::string &file_type, const std::string &file_id,
            const nlohmann::json &media_data) -> boost::asio::awaitable<void> {
      try {
        if (!file_id.empty()) {
          // 使用TGBot的新接口获取文件URL
          obcx::core::MediaFileInfo media_info;
          media_info.file_id = file_id;
          media_info.file_type = file_type;

          auto download_url_opt =
              co_await static_cast<obcx::core::TGBot &>(telegram_bot)
                  .get_media_download_url(media_info);
          if (!download_url_opt) {
            throw std::runtime_error("无法获取文件下载链接");
          }

          const std::string &file_url = download_url_opt.value();
          auto [final_url, filename] =
              MediaProcessor::get_qq_file_info(file_url, file_type);

          obcx::common::MessageSegment file_segment;
          bool cancel_push = false;
          std::string local_file_path; // 用于记录下载的本地文件路径

          // 根据文件类型创建相应的消息段
          if (file_type == "photo" || file_type == "image") {
            // 下载图片到本地
            try {
              // 获取TelegramConnectionManager实例
              auto *tg_bot = &static_cast<obcx::core::TGBot &>(telegram_bot);
              auto *conn_manager =
                  dynamic_cast<obcx::network::TelegramConnectionManager *>(
                      tg_bot->get_connection_manager());
              if (!conn_manager) {
                throw std::runtime_error(
                    "无法获取TelegramConnectionManager实例");
              }

              auto local_path_opt =
                  co_await MediaProcessor::download_media_file(
                      conn_manager, file_url, file_type, filename);

              if (local_path_opt.has_value()) {
                local_file_path = local_path_opt.value();
                temp_files_to_cleanup.push_back(
                    local_file_path); // 记录临时文件用于清理

                // 转换为容器内路径
                const auto &path_manager = MediaProcessor::get_path_manager();
                std::string container_path =
                    path_manager.host_to_container_absolute(local_file_path);

                file_segment.type = "image";
                file_segment.data["file"] = "file:///" + container_path;
                file_segment.data["proxy"] = 1;
                OBCX_INFO("成功下载图片到本地: {} -> 容器路径: {}",
                          local_file_path, container_path);
              } else {
                throw std::runtime_error("下载图片失败");
              }
            } catch (const std::exception &e) {
              // 下载失败，回退到URL方式
              OBCX_WARN("下载图片失败，回退到URL方式: {}", e.what());
              file_segment.type = "image";
              file_segment.data["file"] = final_url;
              file_segment.data["proxy"] = 1;
            }
          } else if (file_type == "video") {
            // 下载视频到本地
            try {
              auto *tg_bot = &static_cast<obcx::core::TGBot &>(telegram_bot);
              auto *conn_manager =
                  dynamic_cast<obcx::network::TelegramConnectionManager *>(
                      tg_bot->get_connection_manager());
              if (!conn_manager) {
                throw std::runtime_error(
                    "无法获取TelegramConnectionManager实例");
              }

              auto local_path_opt =
                  co_await MediaProcessor::download_media_file(
                      conn_manager, file_url, file_type, filename);

              if (local_path_opt.has_value()) {
                local_file_path = local_path_opt.value();
                temp_files_to_cleanup.push_back(
                    local_file_path); // 记录临时文件用于清理

                // 转换为容器内路径
                const auto &path_manager = MediaProcessor::get_path_manager();
                std::string container_path =
                    path_manager.host_to_container_absolute(local_file_path);

                file_segment.type = "video";
                file_segment.data["file"] = "file:///" + container_path;
                OBCX_INFO("成功下载视频到本地: {} -> 容器路径: {}",
                          local_file_path, container_path);
              } else {
                throw std::runtime_error("下载视频失败");
              }
            } catch (const std::exception &e) {
              OBCX_WARN("下载视频失败，回退到URL方式: {}", e.what());
              file_segment.type = "video";
              file_segment.data["file"] = final_url;
              file_segment.data["proxy"] = 1;
            }
          } else if (file_type == "audio" || file_type == "voice") {
            // 下载音频到本地
            try {
              auto *tg_bot = &static_cast<obcx::core::TGBot &>(telegram_bot);
              auto *conn_manager =
                  dynamic_cast<obcx::network::TelegramConnectionManager *>(
                      tg_bot->get_connection_manager());
              if (!conn_manager) {
                throw std::runtime_error(
                    "无法获取TelegramConnectionManager实例");
              }

              auto local_path_opt =
                  co_await MediaProcessor::download_media_file(
                      conn_manager, file_url, file_type, filename);

              if (local_path_opt.has_value()) {
                local_file_path = local_path_opt.value();
                temp_files_to_cleanup.push_back(
                    local_file_path); // 记录临时文件用于清理

                // 转换为容器内路径
                const auto &path_manager = MediaProcessor::get_path_manager();
                std::string container_path =
                    path_manager.host_to_container_absolute(local_file_path);

                file_segment.type = "record";
                file_segment.data["file"] = "file:///" + container_path;
                OBCX_INFO("成功下载音频到本地: {} -> 容器路径: {}",
                          local_file_path, container_path);
              } else {
                throw std::runtime_error("下载音频失败");
              }
            } catch (const std::exception &e) {
              OBCX_WARN("下载音频失败，回退到URL方式: {}", e.what());
              file_segment.type = "record";
              file_segment.data["file"] = final_url;
              file_segment.data["proxy"] = 1;
            }
          } else if (file_type == "document") {
            // 下载文档到本地
            try {
              auto *tg_bot = &static_cast<obcx::core::TGBot &>(telegram_bot);
              auto *conn_manager =
                  dynamic_cast<obcx::network::TelegramConnectionManager *>(
                      tg_bot->get_connection_manager());
              if (!conn_manager) {
                throw std::runtime_error(
                    "无法获取TelegramConnectionManager实例");
              }

              auto local_path_opt =
                  co_await MediaProcessor::download_media_file(
                      conn_manager, file_url, file_type, filename);

              if (local_path_opt.has_value()) {
                local_file_path = local_path_opt.value();
                temp_files_to_cleanup.push_back(
                    local_file_path); // 记录临时文件用于清理

                // 转换为容器内路径
                const auto &path_manager = MediaProcessor::get_path_manager();
                std::string container_path =
                    path_manager.host_to_container_absolute(local_file_path);

                file_segment.type = "file";
                file_segment.data["file"] = "file:///" + container_path;
                file_segment.data["name"] = filename;
                OBCX_INFO("成功下载文档到本地: {} -> 容器路径: {}",
                          local_file_path, container_path);
              } else {
                throw std::runtime_error("下载文档失败");
              }
            } catch (const std::exception &e) {
              OBCX_WARN("下载文档失败，回退到URL方式: {}", e.what());
              file_segment.type = "file";
              file_segment.data["file"] = final_url;
              file_segment.data["name"] = filename;
              file_segment.data["proxy"] = 1;
            }
          } else if (file_type == "sticker") {
            // 使用新的智能缓存系统处理表情包
            try {
              // 补充MediaFileInfo中缺少的信息
              if (media_data.contains("sticker")) {
                auto sticker = media_data["sticker"];
                if (sticker.contains("file_size")) {
                  media_info.file_size = sticker["file_size"].get<int64_t>();
                }
                // 确保file_unique_id被正确设置
                if (sticker.contains("file_unique_id")) {
                  media_info.file_unique_id =
                      sticker["file_unique_id"].get<std::string>();
                }
                // 根据sticker属性确定MIME类型
                if (sticker.contains("is_animated") &&
                    sticker["is_animated"].get<bool>()) {
                  media_info.mime_type = "application/tgs";
                } else if (sticker.contains("is_video") &&
                           sticker["is_video"].get<bool>()) {
                  media_info.mime_type = "video/webm";
                } else {
                  media_info.mime_type = "image/webp";
                }
              }

              // 使用缓存系统下载表情包到本地
              auto cached_path_opt = co_await download_sticker_with_cache(
                  telegram_bot, media_info, "/tmp/bridge_files");

              if (cached_path_opt.has_value()) {
                std::string container_file_path = cached_path_opt.value();

                // 使用容器内文件路径作为图片源
                file_segment.type = "image";
                file_segment.data.clear();
                file_segment.data["file"] = container_file_path;

                // 添加贴纸信息
                if (media_data.contains("sticker")) {
                  auto sticker = media_data["sticker"];
                  std::string sticker_info = "[贴纸";
                  if (sticker.contains("emoji")) {
                    sticker_info += " " + sticker["emoji"].get<std::string>();
                  }
                  if (sticker.contains("is_animated") &&
                      sticker["is_animated"].get<bool>()) {
                    sticker_info += " 动画";
                  } else if (sticker.contains("is_video") &&
                             sticker["is_video"].get<bool>()) {
                    sticker_info += " 视频";
                  }
                  sticker_info += "]";
                  file_segment.data["caption"] = sticker_info;
                }

                OBCX_INFO("成功缓存Telegram sticker到容器路径: {}",
                          container_file_path);
              } else {
                throw std::runtime_error("缓存下载失败");
              }
            } catch (const std::exception &e) {
              OBCX_WARN("缓存系统处理表情包失败: {}", e.what());

              // 回退为文本提示
              cancel_push = true;
              // file_segment.type = "text";
              // std::string emoji_info = "";
              // if (media_data.contains("sticker") &&
              // media_data["sticker"].contains("emoji")) {
              // emoji_info =
              //" " + media_data["sticker"]["emoji"].get<std::string>();
              //}
              // file_segment.data["text"] = fmt::format("[贴纸{}]",
              // emoji_info);
            }
          } else if (file_type == "animation") {
            // 使用缓存系统下载动画到本地，并进行webm到gif的转换
            try {
              // 补充MediaFileInfo中缺少的file_unique_id信息
              if (media_data.contains("animation")) {
                auto animation = media_data["animation"];
                if (animation.contains("file_unique_id")) {
                  media_info.file_unique_id =
                      animation["file_unique_id"].get<std::string>();
                }
              }

              auto cached_path_opt = co_await download_animation_with_cache(
                  telegram_bot, media_info, "/tmp/bridge_files");

              if (cached_path_opt.has_value()) {
                std::string container_file_path = cached_path_opt.value();

                // 使用容器内文件路径作为文件源
                file_segment.type = "file";
                file_segment.data.clear();
                file_segment.data["file"] = "file:///" + container_file_path;
                file_segment.data["name"] = filename;

                OBCX_INFO("成功缓存Telegram animation到容器路径: {}",
                          container_file_path);
              } else {
                throw std::runtime_error("缓存下载失败");
              }
            } catch (const std::exception &e) {
              OBCX_WARN("缓存系统处理动画失败: {}, 回退到URL方式", e.what());
              file_segment.type = "file";
              file_segment.data["file"] = final_url;
              file_segment.data["name"] = filename;
              file_segment.data["proxy"] = 1;
            }
          } else if (file_type == "video_note") {
            // 下载视频消息到本地
            try {
              auto *tg_bot = &static_cast<obcx::core::TGBot &>(telegram_bot);
              auto *conn_manager =
                  dynamic_cast<obcx::network::TelegramConnectionManager *>(
                      tg_bot->get_connection_manager());
              if (!conn_manager) {
                throw std::runtime_error(
                    "无法获取TelegramConnectionManager实例");
              }

              auto local_path_opt =
                  co_await MediaProcessor::download_media_file(
                      conn_manager, file_url, file_type, filename);

              if (local_path_opt.has_value()) {
                local_file_path = local_path_opt.value();
                temp_files_to_cleanup.push_back(
                    local_file_path); // 记录临时文件用于清理

                // 转换为容器内路径
                const auto &path_manager = MediaProcessor::get_path_manager();
                std::string container_path =
                    path_manager.host_to_container_absolute(local_file_path);

                file_segment.type = "video";
                file_segment.data["file"] = "file:///" + container_path;
                OBCX_INFO("成功下载视频消息到本地: {} -> 容器路径: {}",
                          local_file_path, container_path);
              } else {
                throw std::runtime_error("下载视频消息失败");
              }
            } catch (const std::exception &e) {
              OBCX_WARN("下载视频消息失败，回退到URL方式: {}", e.what());
              file_segment.type = "video";
              file_segment.data["file"] = final_url;
              file_segment.data["proxy"] = 1;
            }
          } else {
            // 下载其他类型文件到本地
            try {
              auto *tg_bot = &static_cast<obcx::core::TGBot &>(telegram_bot);
              auto *conn_manager =
                  dynamic_cast<obcx::network::TelegramConnectionManager *>(
                      tg_bot->get_connection_manager());
              if (!conn_manager) {
                throw std::runtime_error(
                    "无法获取TelegramConnectionManager实例");
              }

              auto local_path_opt =
                  co_await MediaProcessor::download_media_file(
                      conn_manager, file_url, file_type, filename);

              if (local_path_opt.has_value()) {
                local_file_path = local_path_opt.value();
                temp_files_to_cleanup.push_back(
                    local_file_path); // 记录临时文件用于清理

                // 转换为容器内路径
                const auto &path_manager = MediaProcessor::get_path_manager();
                std::string container_path =
                    path_manager.host_to_container_absolute(local_file_path);

                file_segment.type = "file";
                file_segment.data["file"] = "file:///" + container_path;
                file_segment.data["name"] = filename;
                OBCX_INFO("成功下载其他类型文件到本地: {} -> 容器路径: {}",
                          local_file_path, container_path);
              } else {
                throw std::runtime_error("下载其他类型文件失败");
              }
            } catch (const std::exception &e) {
              OBCX_WARN("下载其他类型文件失败，回退到URL方式: {}", e.what());
              file_segment.type = "file";
              file_segment.data["file"] = final_url;
              file_segment.data["name"] = filename;
              file_segment.data["proxy"] = 1;
            }
          }

          // 添加caption（如果有）
          if (media_data.contains("caption") &&
              !media_data["caption"].get<std::string>().empty()) {
            file_segment.data["caption"] = media_data["caption"];
          }

          if (cancel_push) {
            OBCX_INFO("处理失败，取消推送Telegram {}文件: {}", file_type,
                      filename);
          } else {
            message_to_send.push_back(file_segment);
            OBCX_INFO("成功处理Telegram {}文件: {}", file_type, filename);
          }
        } else {
          // 发送文件类型提示
          obcx::common::MessageSegment text_segment;
          text_segment.type = "text";
          std::string type_name = file_type == "photo"        ? "图片"
                                  : file_type == "video"      ? "视频"
                                  : file_type == "audio"      ? "音频"
                                  : file_type == "voice"      ? "语音"
                                  : file_type == "sticker"    ? "贴纸"
                                  : file_type == "animation"  ? "GIF动画"
                                  : file_type == "video_note" ? "视频消息"
                                  : file_type == "document"   ? "文档"
                                                              : "文件";
          text_segment.data["text"] = fmt::format("[{}]", type_name);
          message_to_send.push_back(text_segment);
        }
      } catch (const std::exception &e) {
        OBCX_ERROR("处理Telegram {}文件时出错: {}", file_type, e.what());
        // 发送错误提示
        obcx::common::MessageSegment text_segment;
        text_segment.type = "text";
        std::string type_name = file_type == "photo"        ? "图片"
                                : file_type == "video"      ? "视频"
                                : file_type == "audio"      ? "音频"
                                : file_type == "voice"      ? "语音"
                                : file_type == "sticker"    ? "贴纸"
                                : file_type == "animation"  ? "GIF动画"
                                : file_type == "video_note" ? "视频消息"
                                : file_type == "document"   ? "文档"
                                                            : "文件";

        // 检查是否是文件过大错误
        std::string error_msg = e.what();
        if (error_msg.find("文件过大") != std::string::npos ||
            error_msg.find("file is too big") != std::string::npos) {
          text_segment.data["text"] =
              fmt::format("[{}，文件过大无法转发(>20MB)]", type_name);
        } else {
          text_segment.data["text"] = fmt::format("[{}转发失败]", type_name);
        }
        message_to_send.push_back(text_segment);
      }
    };

    // 使用新的TGBot媒体接口处理媒体文件
    auto media_files = obcx::core::TGBot::extract_media_files(event.data);

    for (const auto &media_info : media_files) {
      co_await handle_telegram_media(media_info.file_type, media_info.file_id,
                                     event.data);
    }

    // 添加原始消息的文本内容
    for (const auto &segment : event.message) {
      // 跳过媒体类型，因为我们已经在上面处理了
      if (segment.type != "image" && segment.type != "video" &&
          segment.type != "audio" && segment.type != "voice" &&
          segment.type != "document" && segment.type != "sticker" &&
          segment.type != "animation" && segment.type != "video_note") {
        message_to_send.push_back(segment);
      }
    }

    // 发送到QQ群
    std::string response =
        co_await qq_bot.send_group_message(qq_group_id, message_to_send);

    // 解析响应获取QQ消息ID，用于后续映射
    try {
      nlohmann::json response_json = nlohmann::json::parse(response);
      if (response_json.contains("data") && response_json["data"].is_object() &&
          response_json["data"].contains("message_id")) {
        std::string qq_message_id =
            std::to_string(response_json["data"]["message_id"].get<int64_t>());

        // 记录消息ID映射
        obcx::storage::MessageMapping mapping;
        mapping.source_platform = "telegram";
        mapping.source_message_id = event.message_id;
        mapping.target_platform = "qq";
        mapping.target_message_id = qq_message_id;
        mapping.created_at = std::chrono::system_clock::now();
        db_manager_->add_message_mapping(mapping);
        OBCX_INFO("Telegram消息 {} 成功转发到QQ，QQ消息ID: {}",
                  event.message_id, qq_message_id);

        // 消息发送成功，清理下载的临时文件
        for (const auto &temp_file : temp_files_to_cleanup) {
          MediaProcessor::cleanup_media_file(temp_file);
        }
        OBCX_DEBUG("清理了 {} 个临时媒体文件", temp_files_to_cleanup.size());
      } else {
        OBCX_WARN("转发Telegram消息后，无法解析QQ消息ID。响应: {}", response);
      }
    } catch (const std::exception &e) {
      OBCX_ERROR("解析QQ响应时出错: {}", e.what());
    }
  } catch (const std::exception &e) {
    OBCX_ERROR("转发Telegram消息到QQ时出错: {}", e.what());

    // 发送失败也要清理临时文件，避免文件堆积
    for (const auto &temp_file : temp_files_to_cleanup) {
      MediaProcessor::cleanup_media_file(temp_file);
    }
    OBCX_DEBUG("发送失败，清理了 {} 个临时媒体文件",
               temp_files_to_cleanup.size());

    telegram_bot.error_notify(
        telegram_group_id, fmt::format("转发消息到QQ失败: {}", e.what()), true);
  }
}

auto TelegramHandler::handle_message_deleted(obcx::core::IBot &telegram_bot,
                                             obcx::core::IBot &qq_bot,
                                             obcx::common::Event event)
    -> boost::asio::awaitable<void> {
  try {
    // Telegram的删除事件通常也是MessageEvent，但需要特殊的标识
    // 这里假设删除事件会在context中包含"deleted": true标识
    // 具体实现需要根据Telegram adapter的事件格式调整

    // 暂时跳过，因为需要先了解Telegram删除事件的具体格式
    OBCX_DEBUG("Telegram消息删除事件处理尚未完全实现");
    co_return;

  } catch (const std::exception &e) {
    OBCX_ERROR("处理Telegram删除事件时出错: {}", e.what());
  }
}

auto TelegramHandler::handle_message_edited(obcx::core::IBot &telegram_bot,
                                            obcx::core::IBot &qq_bot,
                                            obcx::common::MessageEvent event)
    -> boost::asio::awaitable<void> {
  try {
    // 确保是群消息编辑
    if (event.message_type != "group" || !event.group_id.has_value()) {
      co_return;
    }

    const std::string telegram_group_id = event.group_id.value();
    OBCX_INFO("处理Telegram群 {} 中消息 {} 的编辑事件", telegram_group_id,
              event.message_id);

    // 查找对应的QQ消息ID
    auto target_message_id =
        db_manager_->get_target_message_id("telegram", event.message_id, "qq");

    if (!target_message_id.has_value()) {
      OBCX_DEBUG("未找到Telegram消息 {} 对应的QQ消息映射", event.message_id);
      co_return;
    }

    bool recall_success = false;

    try {
      // 先尝试撤回QQ上的原消息
      auto recall_response =
          co_await qq_bot.delete_message(target_message_id.value());

      // 解析撤回响应
      nlohmann::json recall_json = nlohmann::json::parse(recall_response);

      if (recall_json.contains("status") && recall_json["status"] == "ok") {
        OBCX_INFO("成功在QQ撤回消息: {}", target_message_id.value());
        recall_success = true;
      } else {
        OBCX_WARN("QQ撤回消息失败: {}, 响应: {}", target_message_id.value(),
                  recall_response);
      }

    } catch (const std::exception &e) {
      OBCX_WARN("尝试在QQ撤回消息时出错: {}", e.what());
    }

    // 只有撤回成功时才重发新消息
    if (recall_success) {
      OBCX_INFO("撤回成功，开始重发编辑后的消息到QQ");

      try {
        // 查找对应的QQ群ID（这里需要根据实际配置映射逻辑调整）
        // 暂时使用相同ID，实际应该查询配置

        // 创建新的消息事件来转发编辑后的内容
        // 这里复用现有的forward_to_qq方法
        co_await forward_to_qq(telegram_bot, qq_bot, event);

        OBCX_INFO("成功重发编辑后的消息");

      } catch (const std::exception &e) {
        OBCX_ERROR("重发编辑后的消息时出错: {}", e.what());

        // 重发失败时不更新映射，保持原状态
        co_return;
      }
    } else {
      OBCX_WARN("由于撤回失败，跳过重发编辑后的消息");

      // 撤回失败时删除映射，因为QQ侧可能已经超时无法撤回
      db_manager_->delete_message_mapping("telegram", event.message_id, "qq");
    }

  } catch (const std::exception &e) {
    OBCX_ERROR("处理Telegram编辑事件时出错: {}", e.what());
  }
}

auto TelegramHandler::handle_recall_command(obcx::core::IBot &telegram_bot,
                                            obcx::core::IBot &qq_bot,
                                            obcx::common::MessageEvent event,
                                            std::string_view qq_group_id)
    -> boost::asio::awaitable<void> {
  try {
    const std::string telegram_group_id = event.group_id.value();

    // 检查是否回复了消息
    if (!event.data.contains("reply_to_message")) {
      // 没有回复消息，发送使用说明
      obcx::common::Message help_message;
      obcx::common::MessageSegment help_segment;
      help_segment.type = "text";
      help_segment.data["text"] =
          "⚠️ 请回复一条消息后使用 /recall 命令来撤回对应的QQ消息";
      help_message.push_back(help_segment);

      co_await telegram_bot.send_group_message(telegram_group_id, help_message);
      co_return;
    }

    // 获取被回复的消息ID
    auto reply_to_message = event.data["reply_to_message"];
    if (!reply_to_message.contains("message_id")) {
      OBCX_WARN("/recall 命令：无法获取被回复消息的ID");
      co_return;
    }

    std::string replied_message_id =
        std::to_string(reply_to_message["message_id"].get<int64_t>());
    OBCX_INFO("/recall 命令：尝试撤回回复的Telegram消息 {} 对应的QQ消息",
              replied_message_id);

    // 查找被回复消息对应的QQ消息ID
    std::optional<std::string> target_qq_message_id;

    // 首先查找该Telegram消息是否已被转发到QQ（回复的是已转发的消息）
    target_qq_message_id = db_manager_->get_target_message_id(
        "telegram", replied_message_id, "qq");

    // 如果没找到，再查找该消息是否来源于QQ（回复的是从QQ转发过来的消息）
    if (!target_qq_message_id.has_value()) {
      target_qq_message_id = db_manager_->get_source_message_id(
          "telegram", replied_message_id, "qq");
    }

    if (!target_qq_message_id.has_value()) {
      // 没有找到对应的QQ消息
      obcx::common::Message error_message;
      obcx::common::MessageSegment error_segment;
      error_segment.type = "text";
      error_segment.data["text"] =
          "❌ 未找到该消息对应的QQ消息，可能不是转发消息或已过期";
      error_message.push_back(error_segment);

      // 回复原命令消息
      obcx::common::MessageSegment reply_segment;
      reply_segment.type = "reply";
      reply_segment.data["message_id"] = event.message_id;
      error_message.insert(error_message.begin(), reply_segment);

      co_await telegram_bot.send_group_message(telegram_group_id,
                                               error_message);
      co_return;
    }

    OBCX_INFO("/recall 命令：找到对应的QQ消息ID: {}",
              target_qq_message_id.value());

    // 尝试在QQ撤回消息
    std::string result_message;
    try {
      std::string recall_response =
          co_await qq_bot.delete_message(target_qq_message_id.value());

      // 解析QQ撤回响应
      nlohmann::json recall_json = nlohmann::json::parse(recall_response);

      if (recall_json.contains("status") && recall_json["status"] == "ok") {
        result_message = "✅ 撤回成功";
        OBCX_INFO("/recall 命令：成功在QQ撤回消息 {}",
                  target_qq_message_id.value());

        // 撤回成功，删除数据库映射
        db_manager_->delete_message_mapping("telegram", replied_message_id,
                                            "qq");
        OBCX_DEBUG("已删除消息映射: telegram:{} -> qq:{}", replied_message_id,
                   target_qq_message_id.value());

      } else {
        result_message = "❌ 撤回失败";
        if (recall_json.contains("message")) {
          std::string error_msg = recall_json["message"];
          if (error_msg.find("超时") != std::string::npos ||
              error_msg.find("timeout") != std::string::npos ||
              error_msg.find("时间") != std::string::npos) {
            result_message += "：消息发送时间过久，无法撤回";
          } else if (error_msg.find("权限") != std::string::npos ||
                     error_msg.find("permission") != std::string::npos) {
            result_message += "：没有撤回权限";
          } else {
            result_message += "：" + error_msg;
          }
        }
        OBCX_WARN("/recall 命令：QQ撤回消息失败: {}", recall_response);
      }

    } catch (const std::exception &e) {
      OBCX_ERROR("/recall 命令：QQ撤回操作异常: {}", e.what());
      result_message = "❌ 撤回操作发生异常，请稍后重试";
    }

    // 发送撤回结果消息
    try {
      obcx::common::Message result_msg;
      obcx::common::MessageSegment reply_segment;
      reply_segment.type = "reply";
      reply_segment.data["message_id"] = event.message_id;
      result_msg.push_back(reply_segment);

      obcx::common::MessageSegment result_segment;
      result_segment.type = "text";
      result_segment.data["text"] = result_message;
      result_msg.push_back(result_segment);

      co_await telegram_bot.send_group_message(telegram_group_id, result_msg);
    } catch (const std::exception &send_e) {
      OBCX_ERROR("/recall 命令：发送结果消息失败: {}", send_e.what());
    }

  } catch (const std::exception &e) {
    OBCX_ERROR("处理 /recall 命令时出错: {}", e.what());
  }
}

auto TelegramHandler::download_sticker_with_cache(
    obcx::core::IBot &telegram_bot, const obcx::core::MediaFileInfo &media_info,
    const std::string &bridge_files_dir)
    -> boost::asio::awaitable<std::optional<std::string>> {

  try {
    // 检查是否是表情包类型
    if (media_info.file_type != "sticker") {
      OBCX_ERROR("不支持的文件类型，仅支持sticker: {}", media_info.file_type);
      co_return std::nullopt;
    }

    // 严格使用file_unique_id作为唯一键，不使用任何hash
    if (media_info.file_unique_id.empty()) {
      OBCX_WARN("file_unique_id为空，跳过数据库缓存操作，直接下载: {}",
                media_info.file_id);
      // 不使用缓存，直接下载
    } else {
      std::string cache_key = media_info.file_unique_id;
      OBCX_DEBUG("表情包缓存查找，使用file_unique_id: {}", cache_key);

      // 查询缓存
      auto cache_info = db_manager_->get_sticker_cache("telegram", cache_key);
      if (cache_info.has_value()) {
        // 缓存命中，但需要验证文件是否真实存在
        bool file_exists = false;

        // 检查最终使用的文件是否存在
        if (cache_info->conversion_status == "success" &&
            cache_info->converted_file_path.has_value()) {
          // 优先使用转换后的文件
          std::string host_path = cache_info->converted_file_path.value();
          if (std::filesystem::exists(host_path)) {
            file_exists = true;
          }
        } else if (!cache_info->original_file_path.empty()) {
          // 使用原始文件
          std::string host_path = cache_info->original_file_path;
          if (std::filesystem::exists(host_path)) {
            file_exists = true;
          }
        }

        if (file_exists && !cache_info->container_path.empty()) {
          // 更新最后使用时间
          obcx::storage::StickerCacheInfo update_info = *cache_info;
          update_info.last_used_at = std::chrono::system_clock::now();
          db_manager_->save_sticker_cache(update_info);

          OBCX_DEBUG("表情包缓存命中: {} -> {}", cache_key,
                     cache_info->container_path);
          co_return cache_info->container_path;
        } else {
          OBCX_WARN("表情包缓存项存在但文件丢失，将重新下载: {}", cache_key);
        }
      }
    }

    // 缓存未命中或文件不存在，需要下载
    OBCX_INFO("表情包缓存未命中，开始下载: {}", media_info.file_id);

    // 获取下载URL
    auto download_urls = co_await static_cast<obcx::core::TGBot &>(telegram_bot)
                             .get_media_download_urls({media_info});
    if (download_urls.empty() || !download_urls[0].has_value()) {
      OBCX_ERROR("获取表情包下载URL失败: {}", media_info.file_id);
      co_return std::nullopt;
    }

    std::string download_url = download_urls[0].value();

    // 使用正确的挂载点路径
    std::string host_bridge_files_dir =
        "/home/lambillda/Codes/OBCX/tests/llonebot/bridge_files";
    std::string original_dir = host_bridge_files_dir + "/stickers/original";
    std::filesystem::create_directories(original_dir);

    // 检测文件类型和扩展名
    std::string file_extension = ".webp"; // 默认webp
    std::string mime_type = "image/webp";

    if (media_info.mime_type.has_value()) {
      mime_type = media_info.mime_type.value();
      if (mime_type == "image/webp") {
        file_extension = ".webp";
      } else if (mime_type == "video/webm") {
        file_extension = ".webm";
      } else if (mime_type == "application/tgs") {
        file_extension = ".tgs";
      }
    }

    // 生成原始文件路径
    std::string filename_prefix;
    if (!media_info.file_unique_id.empty()) {
      // 使用 file_unique_id 作为文件名前缀
      filename_prefix =
          fmt::format("sticker_{}_{}", media_info.file_unique_id.substr(0, 12),
                      media_info.file_id.substr(0, 8));
    } else {
      // 没有 file_unique_id，使用时间戳
      auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
      filename_prefix = fmt::format("sticker_{}_{}", timestamp,
                                    media_info.file_id.substr(0, 8));
    }
    std::string original_filename = filename_prefix + file_extension;
    std::string original_file_path = original_dir + "/" + original_filename;

    // 下载文件内容
    auto *tg_bot = dynamic_cast<obcx::core::TGBot *>(&telegram_bot);
    if (!tg_bot) {
      OBCX_ERROR("telegram_bot不是TGBot类型");
      co_return std::nullopt;
    }

    auto *conn_manager =
        dynamic_cast<obcx::network::TelegramConnectionManager *>(
            tg_bot->get_connection_manager());
    if (!conn_manager) {
      OBCX_ERROR("连接管理器不是TelegramConnectionManager类型");
      co_return std::nullopt;
    }

    auto file_content =
        co_await conn_manager->download_file_content(download_url);
    if (file_content.empty()) {
      OBCX_ERROR("下载文件内容为空: {}", download_url);
      co_return std::nullopt;
    }

    // 保存原始文件
    std::ofstream file(original_file_path, std::ios::binary);
    if (!file) {
      throw std::runtime_error("无法创建文件: " + original_file_path);
    }
    file.write(file_content.data(), file_content.size());
    file.close();

    OBCX_INFO("表情包原始文件已下载: {} -> {} ({}字节)", media_info.file_id,
              original_file_path, file_content.size());

    std::string final_file_path = original_file_path;
    std::string final_container_path;
    std::string conversion_status = "success";

    // 如果是webm格式，需要转换为gif
    if (mime_type == "video/webm" || file_extension == ".webm") {
      OBCX_INFO("检测到webm格式贴纸，开始转换为gif: {}", original_file_path);

      // 创建转换目录
      std::string converted_dir = host_bridge_files_dir + "/stickers/converted";
      std::filesystem::create_directories(converted_dir);

      // 生成转换后的gif文件路径
      std::string converted_filename = filename_prefix + ".gif";
      std::string converted_file_path =
          converted_dir + "/" + converted_filename;

      // 使用MediaConverter进行转换
      bool conversion_success =
          obcx::common::MediaConverter::convert_webm_to_gif_with_fallback(
              original_file_path, converted_file_path, 5);

      if (conversion_success && std::filesystem::exists(converted_file_path)) {
        OBCX_INFO("webm贴纸到gif转换成功: {} -> {}", original_file_path,
                  converted_file_path);
        final_file_path = converted_file_path;
        final_container_path =
            "/root/llonebot/bridge_files/stickers/converted/" +
            converted_filename;
        conversion_status = "success";
      } else {
        OBCX_WARN("webm贴纸到gif转换失败，使用原始webm文件: {}",
                  original_file_path);
        final_container_path =
            "/root/llonebot/bridge_files/stickers/original/" +
            original_filename;
        conversion_status = "failed";
      }
    } else {
      // 非webm格式，直接使用原始文件
      final_container_path =
          "/root/llonebot/bridge_files/stickers/original/" + original_filename;
    }

    // 只有在有 file_unique_id 时才保存到数据库
    if (!media_info.file_unique_id.empty()) {
      // 创建缓存信息
      obcx::storage::StickerCacheInfo new_cache_info;
      new_cache_info.platform = "telegram";
      new_cache_info.sticker_id = media_info.file_id; // 原始file_id
      new_cache_info.sticker_hash =
          media_info.file_unique_id; // 用于查询的唯一ID
      new_cache_info.original_file_path = original_file_path; // 主机路径
      new_cache_info.file_size = file_content.size();
      new_cache_info.mime_type = mime_type;
      new_cache_info.conversion_status = conversion_status;
      new_cache_info.created_at = std::chrono::system_clock::now();
      new_cache_info.last_used_at = std::chrono::system_clock::now();
      new_cache_info.container_path = final_container_path; // 容器内路径

      // 如果有转换后的文件，也保存转换后的路径
      if (final_file_path != original_file_path) {
        new_cache_info.converted_file_path = final_file_path;
      }

      // 保存到缓存数据库
      if (!db_manager_->save_sticker_cache(new_cache_info)) {
        OBCX_WARN("保存表情包缓存失败，但文件已下载: {}", final_file_path);
      }
    } else {
      OBCX_DEBUG("没有file_unique_id，跳过数据库保存");
    }

    OBCX_INFO("表情包缓存完成: {} -> {}", media_info.file_id,
              final_container_path);
    co_return final_container_path;

  } catch (const std::exception &e) {
    OBCX_ERROR("下载表情包失败 (file_id: {}): {}", media_info.file_id,
               e.what());
    co_return std::nullopt;
  }
}

auto TelegramHandler::download_animation_with_cache(
    obcx::core::IBot &telegram_bot, const obcx::core::MediaFileInfo &media_info,
    const std::string &bridge_files_dir)
    -> boost::asio::awaitable<std::optional<std::string>> {

  try {
    // 检查是否是动画类型
    if (media_info.file_type != "animation") {
      OBCX_ERROR("不支持的文件类型，仅支持animation: {}", media_info.file_type);
      co_return std::nullopt;
    }

    // 严格使用file_unique_id作为唯一键，不使用任何hash
    if (media_info.file_unique_id.empty()) {
      OBCX_WARN("file_unique_id为空，跳过数据库缓存操作，直接下载: {}",
                media_info.file_id);
      // 不使用缓存，直接下载
    } else {
      std::string cache_key = media_info.file_unique_id;
      OBCX_DEBUG("动画缓存查找，使用file_unique_id: {}", cache_key);

      // 查询缓存 - 使用专门的animation缓存表
      auto cache_info =
          db_manager_->get_sticker_cache("telegram_animation", cache_key);
      if (cache_info.has_value()) {
        // 缓存命中，但需要验证文件是否真实存在
        bool file_exists = false;

        // 检查最终使用的文件是否存在
        if (cache_info->conversion_status == "success" &&
            cache_info->converted_file_path.has_value()) {
          // 优先使用转换后的gif文件
          std::string host_path = cache_info->converted_file_path.value();
          if (std::filesystem::exists(host_path)) {
            file_exists = true;
          }
        } else if (!cache_info->original_file_path.empty()) {
          // 使用原始文件
          std::string host_path = cache_info->original_file_path;
          if (std::filesystem::exists(host_path)) {
            file_exists = true;
          }
        }

        if (file_exists && !cache_info->container_path.empty()) {
          // 更新最后使用时间
          obcx::storage::StickerCacheInfo update_info = *cache_info;
          update_info.last_used_at = std::chrono::system_clock::now();
          db_manager_->save_sticker_cache(update_info);

          OBCX_DEBUG("动画缓存命中: {} -> {}", cache_key,
                     cache_info->container_path);
          co_return cache_info->container_path;
        } else {
          OBCX_WARN("动画缓存项存在但文件丢失，将重新下载: {}", cache_key);
        }
      }
    }

    // 缓存未命中或文件不存在，需要下载
    OBCX_INFO("动画缓存未命中，开始下载: {}", media_info.file_id);

    // 获取下载URL
    auto download_urls = co_await static_cast<obcx::core::TGBot &>(telegram_bot)
                             .get_media_download_urls({media_info});
    if (download_urls.empty() || !download_urls[0].has_value()) {
      OBCX_ERROR("获取动画下载URL失败: {}", media_info.file_id);
      co_return std::nullopt;
    }

    std::string download_url = download_urls[0].value();

    // 使用正确的挂载点路径
    std::string host_bridge_files_dir =
        "/home/lambillda/Codes/OBCX/tests/llonebot/bridge_files";
    std::string original_dir = host_bridge_files_dir + "/animations/original";
    std::string converted_dir = host_bridge_files_dir + "/animations/converted";
    std::filesystem::create_directories(original_dir);
    std::filesystem::create_directories(converted_dir);

    // 检测文件类型和扩展名
    std::string file_extension = ".mp4"; // 默认mp4
    std::string mime_type = "video/mp4";

    if (media_info.mime_type.has_value()) {
      mime_type = media_info.mime_type.value();
      if (mime_type == "video/mp4") {
        file_extension = ".mp4";
      } else if (mime_type == "video/webm") {
        file_extension = ".webm";
      } else if (mime_type == "image/gif") {
        file_extension = ".gif";
      }
    }

    // 生成原始文件路径
    std::string filename_prefix;
    if (!media_info.file_unique_id.empty()) {
      // 使用 file_unique_id 作为文件名前缀
      filename_prefix = fmt::format("animation_{}_{}",
                                    media_info.file_unique_id.substr(0, 12),
                                    media_info.file_id.substr(0, 8));
    } else {
      // 没有 file_unique_id，使用时间戳
      auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
      filename_prefix = fmt::format("animation_{}_{}", timestamp,
                                    media_info.file_id.substr(0, 8));
    }
    std::string original_filename = filename_prefix + file_extension;
    std::string original_file_path = original_dir + "/" + original_filename;

    // 下载文件内容
    auto *tg_bot = dynamic_cast<obcx::core::TGBot *>(&telegram_bot);
    if (!tg_bot) {
      OBCX_ERROR("telegram_bot不是TGBot类型");
      co_return std::nullopt;
    }

    auto *conn_manager =
        dynamic_cast<obcx::network::TelegramConnectionManager *>(
            tg_bot->get_connection_manager());
    if (!conn_manager) {
      OBCX_ERROR("连接管理器不是TelegramConnectionManager类型");
      co_return std::nullopt;
    }

    auto file_content =
        co_await conn_manager->download_file_content(download_url);
    if (file_content.empty()) {
      OBCX_ERROR("下载文件内容为空: {}", download_url);
      co_return std::nullopt;
    }

    // 保存原始文件
    std::ofstream file(original_file_path, std::ios::binary);
    if (!file) {
      throw std::runtime_error("无法创建文件: " + original_file_path);
    }
    file.write(file_content.data(), file_content.size());
    file.close();

    OBCX_INFO("动画原始文件已下载: {} -> {} ({}字节)", media_info.file_id,
              original_file_path, file_content.size());

    std::string final_file_path = original_file_path;
    std::string final_container_path;
    std::string conversion_status = "success";

    // 如果是webm格式，需要转换为gif
    if (mime_type == "video/webm" || file_extension == ".webm") {
      OBCX_INFO("检测到webm格式动画，开始转换为gif: {}", original_file_path);

      // 生成转换后的gif文件路径
      std::string converted_filename = filename_prefix + ".gif";
      std::string converted_file_path =
          converted_dir + "/" + converted_filename;
      if (!std::filesystem::exists(converted_file_path)) {
        std::filesystem::create_directories(converted_dir);
      }

      // 使用MediaConverter进行转换
      bool conversion_success =
          obcx::common::MediaConverter::convert_webm_to_gif_with_fallback(
              original_file_path, converted_file_path, 5);

      if (conversion_success && std::filesystem::exists(converted_file_path)) {
        OBCX_INFO("webm到gif转换成功: {} -> {}", original_file_path,
                  converted_file_path);
        final_file_path = converted_file_path;
        final_container_path =
            "/root/llonebot/bridge_files/animations/converted/" +
            converted_filename;
        conversion_status = "success";
      } else {
        OBCX_WARN("webm到gif转换失败，使用原始webm文件: {}",
                  original_file_path);
        final_container_path =
            "/root/llonebot/bridge_files/animations/original/" +
            original_filename;
        conversion_status = "failed";
      }
    } else {
      // 非webm格式，直接使用原始文件
      final_container_path =
          "/root/llonebot/bridge_files/animations/original/" +
          original_filename;
    }

    // 只有在有 file_unique_id 时才保存到数据库
    if (!media_info.file_unique_id.empty()) {
      // 创建缓存信息
      obcx::storage::StickerCacheInfo new_cache_info;
      new_cache_info.platform = "telegram_animation";
      new_cache_info.sticker_id = media_info.file_id; // 原始file_id
      new_cache_info.sticker_hash =
          media_info.file_unique_id; // 用于查询的唯一ID
      new_cache_info.original_file_path = original_file_path; // 主机路径
      new_cache_info.file_size = file_content.size();
      new_cache_info.mime_type = mime_type;
      new_cache_info.conversion_status = conversion_status;
      new_cache_info.created_at = std::chrono::system_clock::now();
      new_cache_info.last_used_at = std::chrono::system_clock::now();
      new_cache_info.container_path = final_container_path; // 容器内路径

      // 如果有转换后的文件，也保存转换后的路径
      if (final_file_path != original_file_path) {
        new_cache_info.converted_file_path = final_file_path;
      }

      // 保存到缓存数据库
      if (!db_manager_->save_sticker_cache(new_cache_info)) {
        OBCX_WARN("保存动画缓存失败，但文件已下载: {}", final_file_path);
      }
    } else {
      OBCX_DEBUG("没有file_unique_id，跳过数据库保存");
    }

    OBCX_INFO("动画缓存完成: {} -> {}", media_info.file_id,
              final_container_path);
    co_return final_container_path;

  } catch (const std::exception &e) {
    OBCX_ERROR("下载动画失败 (file_id: {}): {}", media_info.file_id, e.what());
    co_return std::nullopt;
  }
}

} // namespace bridge
