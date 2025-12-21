#include "qq_handler.hpp"
#include "config.hpp"
#include "media_processor.hpp"
#include "retry_queue_manager.hpp"

#include "common/logger.hpp"
#include "core/qq_bot.hpp"
#include "core/tg_bot.hpp"
#include "network/http_client.hpp"

#include <boost/asio/io_context.hpp>
#include <fmt/format.h>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <utility>

namespace bridge {

// 辅助函数：将二进制数据转换为16进制字符串用于调试
std::string to_hex_string(const std::string &data, size_t max_bytes = 16) {
  std::ostringstream oss;
  size_t len = std::min(data.size(), max_bytes);
  for (size_t i = 0; i < len; ++i) {
    if (i > 0)
      oss << " ";
    oss << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
        << static_cast<unsigned char>(data[i]);
  }
  if (data.size() > max_bytes) {
    oss << " ...";
  }
  return oss.str();
}

/**
 * @brief 小程序解析结果结构
 */
struct MiniAppParseResult {
  bool success = false;
  std::string title;
  std::string description;
  std::vector<std::string> urls;
  std::string app_name;
  std::string raw_json;
};

/**
 * @brief 从JSON字符串中提取URLs的通用函数
 */
std::vector<std::string> extract_urls_from_json(const std::string &json_str) {
  std::vector<std::string> urls;

  // 使用正则表达式匹配JSON中的URL
  std::regex url_regex(R"((https?://[^\s\",}]+))");
  std::sregex_iterator url_iter(json_str.begin(), json_str.end(), url_regex);
  std::sregex_iterator url_end;

  for (; url_iter != url_end; ++url_iter) {
    urls.push_back(url_iter->str());
  }

  return urls;
}

/**
 * @brief 解析小程序JSON数据
 */
MiniAppParseResult parse_miniapp_json(const std::string &json_data) {
  MiniAppParseResult result;
  result.raw_json = json_data;

  if (!config::ENABLE_MINIAPP_PARSING) {
    return result;
  }

  try {
    nlohmann::json j = nlohmann::json::parse(json_data);

    // 提取应用名称
    if (j.contains("app")) {
      result.app_name = j["app"];
    }

    // 提取标题
    if (j.contains("prompt")) {
      result.title = j["prompt"];
    } else if (j.contains("meta") && j["meta"].contains("detail") &&
               j["meta"]["detail"].contains("title")) {
      result.title = j["meta"]["detail"]["title"];
    }

    // 提取描述
    if (j.contains("desc")) {
      result.description = j["desc"];
    } else if (j.contains("meta") && j["meta"].contains("detail") &&
               j["meta"]["detail"].contains("desc")) {
      result.description = j["meta"]["detail"]["desc"];
    }

    // 提取URLs - 多个位置查找
    std::vector<std::string> found_urls;

    // 1. 从meta.url提取
    if (j.contains("meta")) {
      auto meta = j["meta"];
      if (meta.contains("url") && meta["url"].is_string()) {
        found_urls.push_back(meta["url"]);
      }
      if (meta.contains("detail")) {
        auto detail = meta["detail"];
        if (detail.contains("url") && detail["url"].is_string()) {
          found_urls.push_back(detail["url"]);
        }
      }
    }

    // 2. 从顶级字段提取
    if (j.contains("url") && j["url"].is_string()) {
      found_urls.push_back(j["url"]);
    }

    // 3. 从任何地方用正则表达式提取
    auto regex_urls = extract_urls_from_json(json_data);
    found_urls.insert(found_urls.end(), regex_urls.begin(), regex_urls.end());

    // 去重
    std::sort(found_urls.begin(), found_urls.end());
    found_urls.erase(std::unique(found_urls.begin(), found_urls.end()),
                     found_urls.end());

    result.urls = found_urls;
    result.success = !found_urls.empty() || !result.title.empty();

    PLUGIN_DEBUG("qq_to_tg", "解析小程序: app={}, title={}, urls_count={}",
                 result.app_name, result.title, result.urls.size());

  } catch (const std::exception &e) {
    PLUGIN_DEBUG("qq_to_tg", "小程序JSON解析失败: {}", e.what());
    // 解析失败时仍然尝试用正则提取URL
    result.urls = extract_urls_from_json(json_data);
    result.success = !result.urls.empty();
  }

  return result;
}

/**
 * @brief 格式化小程序消息段用于发送到Telegram
 */
obcx::common::MessageSegment format_miniapp_message(
    const MiniAppParseResult &parse_result) {
  obcx::common::MessageSegment segment;
  segment.type = "text";

  std::string message_text;

  if (parse_result.success) {
    // 成功解析的情况
    message_text = "📱 ";

    if (!parse_result.title.empty()) {
      message_text += fmt::format("[{}]", parse_result.title);
    } else {
      message_text += "[小程序]";
    }

    if (!parse_result.description.empty() &&
        parse_result.description != parse_result.title) {
      message_text += fmt::format("\n{}", parse_result.description);
    }

    if (!parse_result.urls.empty()) {
      message_text += "\n🔗 链接:";
      for (const auto &url : parse_result.urls) {
        message_text += fmt::format("\n{}", url);
      }
    }

    if (!parse_result.app_name.empty()) {
      message_text += fmt::format("\n📦 应用: {}", parse_result.app_name);
    }

  } else {
    // 解析失败的情况
    message_text = "📱 [无法解析的小程序]";

    if (config::SHOW_RAW_JSON_ON_PARSE_FAIL) {
      std::string json_to_show = parse_result.raw_json;
      if (json_to_show.length() > config::MAX_JSON_DISPLAY_LENGTH) {
        json_to_show =
            json_to_show.substr(0, config::MAX_JSON_DISPLAY_LENGTH) + "...";
      }
      message_text +=
          fmt::format("\n原始数据:\n```json\n{}\n```", json_to_show);
    }
  }

  segment.data["text"] = message_text;
  return segment;
}

QQHandler::QQHandler(std::shared_ptr<obcx::storage::DatabaseManager> db_manager,
                     std::shared_ptr<RetryQueueManager> retry_manager)
    : db_manager_(std::move(db_manager)),
      retry_manager_(std::move(retry_manager)) {}

auto QQHandler::forward_to_telegram(obcx::core::IBot &telegram_bot,
                                    obcx::core::IBot &qq_bot,
                                    obcx::common::MessageEvent event)
    -> boost::asio::awaitable<void> {
  // 确保是群消息
  if (event.message_type != "group" || !event.group_id.has_value()) {
    co_return;
  }

  const std::string qq_group_id = event.group_id.value();
  std::string telegram_group_id;
  const GroupBridgeConfig *bridge_config = nullptr;

  // 查找对应的Telegram群ID、topic ID和桥接配置
  auto [tg_id, topic_id] = get_tg_group_and_topic_id(qq_group_id);
  PLUGIN_DEBUG("qq_to_tg", "QQ群 {} 查找结果: TG群={}, topic_id={}",
               qq_group_id, tg_id, topic_id);

  if (tg_id.empty()) {
    PLUGIN_DEBUG("qq_to_tg", "QQ群 {} 没有对应的Telegram群配置", qq_group_id);
    co_return;
  }

  telegram_group_id = tg_id;
  bridge_config = get_bridge_config(telegram_group_id);

  if (!bridge_config) {
    PLUGIN_DEBUG("qq_to_tg", "无法找到Telegram群 {} 的配置", telegram_group_id);
    co_return;
  }

  // 检查是否启用QQ到TG转发
  if (bridge_config->mode == BridgeMode::GROUP_TO_GROUP) {
    if (!bridge_config->enable_qq_to_tg) {
      PLUGIN_DEBUG("qq_to_tg", "QQ群 {} 到Telegram群 {} 的转发已禁用，跳过",
                   qq_group_id, telegram_group_id);
      co_return;
    }
  } else if (bridge_config->mode == BridgeMode::TOPIC_TO_GROUP) {
    // Topic模式：需要检查具体的topic配置
    const TopicBridgeConfig *topic_config =
        get_topic_config(telegram_group_id, topic_id);
    if (!topic_config || !topic_config->enable_qq_to_tg) {
      PLUGIN_DEBUG("qq_to_tg", "QQ群 {} 到Telegram topic {} 的转发已禁用，跳过",
                   qq_group_id, topic_id);
      co_return;
    }
  }

  // 检查是否是 /checkalive 命令
  if (event.raw_message.starts_with("/checkalive")) {
    PLUGIN_INFO("qq_to_tg", "检测到 /checkalive 命令，处理存活检查请求");
    co_await handle_checkalive_command(telegram_bot, qq_bot, event,
                                       telegram_group_id);
    co_return;
  }

  // 检查是否是回环消息（从Telegram转发过来的）
  if (event.raw_message.starts_with("[Telegram] ")) {
    PLUGIN_DEBUG("qq_to_tg", "检测到可能是回环的Telegram消息，跳过转发");
    co_return;
  }

  // 检查消息是否已转发（避免重复）
  if (db_manager_->get_target_message_id("qq", event.message_id, "telegram")
          .has_value()) {
    PLUGIN_DEBUG("qq_to_tg", "QQ消息 {} 已转发到Telegram，跳过重复处理",
                 event.message_id);
    co_return;
  }

  PLUGIN_INFO("qq_to_tg", "准备从QQ群 {} 转发消息到Telegram群 {}", qq_group_id,
              telegram_group_id);

  try {
    // 保存/更新用户信息
    db_manager_->save_user_from_event(event, "qq");
    // 保存消息信息
    db_manager_->save_message_from_event(event, "qq");

    // 获取用户显示名称（使用群组特定的昵称）
    std::string sender_display_name = db_manager_->get_user_display_name(
        "qq", event.user_id, event.group_id.value_or(""));

    // 如果仍然是用户ID（说明没有昵称信息），尝试同步获取一次
    if (sender_display_name == event.user_id &&
        db_manager_->should_fetch_user_info("qq", event.user_id,
                                            event.group_id.value_or(""))) {
      try {
        // 同步获取群成员信息（仅第一次）
        std::string response = co_await qq_bot.get_group_member_info(
            qq_group_id, event.user_id, false);
        nlohmann::json response_json = nlohmann::json::parse(response);

        PLUGIN_DEBUG("qq_to_tg", "QQ群成员信息API响应: {}", response);

        if (response_json.contains("status") &&
            response_json["status"] == "ok" && response_json.contains("data") &&
            response_json["data"].is_object()) {

          auto data = response_json["data"];
          PLUGIN_DEBUG("qq_to_tg", "QQ群成员信息详细数据: {}", data.dump());
          obcx::storage::UserInfo user_info;
          user_info.platform = "qq";
          user_info.user_id = event.user_id;
          user_info.group_id =
              event.group_id.value_or(""); // 群组特定的用户信息
          user_info.last_updated = std::chrono::system_clock::now();

          std::string general_nickname, card, title;

          if (data.contains("nickname") && data["nickname"].is_string()) {
            general_nickname = data["nickname"];
          }

          if (data.contains("card") && data["card"].is_string()) {
            card = data["card"];
          }

          if (data.contains("title") && data["title"].is_string()) {
            title = data["title"];
          }

          // 优先级：群名片 > 群头衔 > 一般昵称
          // 将最优先的名称存储在nickname字段中，便于显示逻辑处理
          if (!card.empty()) {
            user_info.nickname = card;
            PLUGIN_DEBUG("qq_to_tg", "使用QQ群名片作为显示名称: {} -> {}",
                         event.user_id, card);
          } else if (!title.empty()) {
            user_info.nickname = title;
            PLUGIN_DEBUG("qq_to_tg", "使用QQ群头衔作为显示名称: {} -> {}",
                         event.user_id, title);
          } else if (!general_nickname.empty()) {
            user_info.nickname = general_nickname;
            PLUGIN_DEBUG("qq_to_tg", "使用QQ一般昵称作为显示名称: {} -> {}",
                         event.user_id, general_nickname);
          }

          // 同时保存群头衔到title字段供后续使用
          if (!title.empty()) {
            user_info.title = title;
          }

          // 保存用户信息并更新显示名称
          if (db_manager_->save_or_update_user(user_info)) {
            sender_display_name = db_manager_->get_user_display_name(
                "qq", event.user_id, event.group_id.value_or(""));
            PLUGIN_DEBUG("qq_to_tg", "同步获取QQ用户信息成功：{} -> {}",
                         event.user_id, sender_display_name);
          }
        }
      } catch (const std::exception &e) {
        PLUGIN_DEBUG("qq_to_tg", "同步获取QQ用户信息失败：{}", e.what());
      }
    }

    // 创建转发消息，保留原始消息的所有段（包括图片）
    obcx::common::Message message_to_send;

    // 根据配置决定是否添加发送者信息
    bool show_sender = false;
    if (bridge_config->mode == BridgeMode::GROUP_TO_GROUP) {
      show_sender = bridge_config->show_qq_to_tg_sender;
    } else {
      // Topic模式：获取对应topic的配置
      const TopicBridgeConfig *topic_config =
          get_topic_config(telegram_group_id, topic_id);
      show_sender = topic_config ? topic_config->show_qq_to_tg_sender : false;
    }

    if (show_sender) {
      std::string sender_info = fmt::format("[{}]\t", sender_display_name);
      obcx::common::MessageSegment sender_segment;
      sender_segment.type = "text";
      sender_segment.data["text"] = sender_info;
      message_to_send.push_back(sender_segment);
      PLUGIN_DEBUG("qq_to_tg", "QQ到Telegram转发显示发送者：{}",
                   sender_display_name);
    } else {
      PLUGIN_DEBUG("qq_to_tg", "QQ到Telegram转发不显示发送者");
    }

    // 检查是否有引用消息
    std::optional<std::string> reply_message_id;
    for (const auto &segment : event.message) {
      if (segment.type == "reply") {
        // 获取被引用的QQ消息ID
        if (segment.data.contains("id")) {
          reply_message_id = segment.data["id"];
          PLUGIN_DEBUG("qq_to_tg", "检测到QQ引用消息，引用ID: {}",
                       reply_message_id.value());
          break;
        }
      }
    }

    // 如果有引用消息，尝试查找对应平台的消息ID
    if (reply_message_id.has_value()) {
      std::optional<std::string> target_telegram_message_id;

      // 情况1: 如果被回复的QQ消息曾经转发到Telegram过，找到TG的消息ID
      target_telegram_message_id = db_manager_->get_target_message_id(
          "qq", reply_message_id.value(), "telegram");

      // 情况2: 如果被回复的QQ消息来源于Telegram，找到TG的原始消息ID
      if (!target_telegram_message_id.has_value()) {
        target_telegram_message_id = db_manager_->get_source_message_id(
            "qq", reply_message_id.value(), "telegram");
      }

      // 如果最终仍未找到映射，清空reply_message_id以避免创建无效的reply段
      if (!target_telegram_message_id.has_value()) {
        reply_message_id.reset();
        PLUGIN_DEBUG("qq_to_tg", "清空reply_message_id，避免创建无效回复段");
      }

      PLUGIN_DEBUG("qq_to_tg", "QQ回复消息映射查找: QQ消息ID {} -> TG消息ID {}",
                   reply_message_id.has_value() ? reply_message_id.value()
                                                : "已清空",
                   target_telegram_message_id.has_value()
                       ? target_telegram_message_id.value()
                       : "未找到");

      if (target_telegram_message_id.has_value()) {
        // 创建Telegram引用消息段
        obcx::common::MessageSegment reply_segment;
        reply_segment.type = "reply";
        reply_segment.data["id"] = target_telegram_message_id.value();
        message_to_send.push_back(reply_segment);
        PLUGIN_DEBUG("qq_to_tg", "添加Telegram引用消息段，引用ID: {}",
                     target_telegram_message_id.value());
      } else {
        PLUGIN_DEBUG("qq_to_tg",
                     "未找到QQ引用消息对应的Telegram消息ID，可能是原生QQ消息");
      }
    }

    // 处理QQ消息中的不同文件类型
    auto handle_qq_media = [&](const obcx::common::MessageSegment &segment)
        -> boost::asio::awaitable<void> {
      obcx::common::MessageSegment converted_segment = segment;

      if (segment.type == "image") {
        // 检测是否为GIF图片或表情包
        std::string file_name = segment.data.value("file", "");
        std::string url = segment.data.value("url", "");

        // 判断是否是GIF的几种方式：
        bool is_gif = false;
        if (!file_name.empty() &&
            (file_name.find(".gif") != std::string::npos ||
             file_name.find(".GIF") != std::string::npos)) {
          is_gif = true;
        }
        if (!url.empty() && url.find("gif") != std::string::npos) {
          is_gif = true;
        }
        // 对于subType=1的情况，优先使用数据库缓存，谨慎使用HTTP HEAD请求
        if (segment.data.contains("subType") && segment.data["subType"] == 1 &&
            !url.empty()) {
          try {
            // 首先检查数据库缓存
            std::string qq_sticker_hash =
                obcx::storage::DatabaseManager::calculate_hash(url);
            auto cached_mapping =
                db_manager_->get_qq_sticker_mapping(qq_sticker_hash);

            if (cached_mapping && cached_mapping->is_gif.has_value()) {
              // 使用缓存的结果
              is_gif = cached_mapping->is_gif.value();
              PLUGIN_DEBUG("qq_to_tg",
                           "使用缓存的图片类型检测结果: {} -> is_gif={}", url,
                           is_gif);
            } else {
              // 缓存未命中，直接下载文件并进行本地MIME检测
              PLUGIN_INFO(
                  "qq_to_tg",
                  "[图片类型检测] "
                  "subType=1图片缓存未命中，开始下载文件进行本地检测: {}",
                  url);

              try {
                PLUGIN_DEBUG("qq_to_tg", "[图片类型检测] 开始下载文件");

                // 解析QQ文件URL获取主机和路径信息
                std::string url_str(url);
                size_t protocol_pos = url_str.find("://");
                if (protocol_pos == std::string::npos) {
                  throw std::runtime_error("无效的QQ文件URL格式");
                }

                size_t host_start = protocol_pos + 3;
                size_t path_start = url_str.find("/", host_start);
                if (path_start == std::string::npos) {
                  throw std::runtime_error("QQ文件URL中未找到路径部分");
                }

                std::string host =
                    url_str.substr(host_start, path_start - host_start);
                std::string path = url_str.substr(path_start);

                PLUGIN_DEBUG(
                    "qq_to_tg",
                    "[图片类型检测] QQ文件URL解析完成 - Host: {}, Path: {}",
                    host, path);

                // 创建专用的HttpClient配置（直连，无代理）
                obcx::common::ConnectionConfig qq_config;
                qq_config.host = host;
                qq_config.port = 443; // HTTPS默认端口
                qq_config.use_ssl = true;
                qq_config.access_token = ""; // QQ文件下载不需要令牌
                // 确保直连，不使用代理
                qq_config.proxy_host = "";
                qq_config.proxy_port = 0;
                qq_config.proxy_type = "";
                qq_config.proxy_username = "";
                qq_config.proxy_password = "";

                PLUGIN_DEBUG(
                    "qq_to_tg",
                    "[图片类型检测] 创建专用QQ文件下载HttpClient - 主机: {}:{}",
                    host, qq_config.port);

                // 为QQ文件下载创建临时IO上下文
                boost::asio::io_context temp_ioc;

                // 创建专用的HttpClient实例（直连，无代理）
                auto qq_http_client =
                    std::make_unique<obcx::network::HttpClient>(temp_ioc,
                                                                qq_config);

                // 使用空的头部映射，让HttpClient设置完整的Firefox浏览器头部
                // 添加Range头部只请求前32个字节（足够检测所有常见图片格式的Magic
                // Numbers）
                std::map<std::string, std::string> headers;
                headers["Range"] = "bytes=0-31";

                // 发送GET请求获取文件前32个字节
                obcx::network::HttpResponse response =
                    qq_http_client->get_sync(path, headers);

                if (response.is_success()) {
                  // 获取文件的前几个字节内容
                  std::string file_header = response.body;

                  if (!file_header.empty()) {
                    // 使用文件头部Magic Numbers检测MIME类型
                    std::string detected_mime =
                        MediaProcessor::detect_mime_type_from_content(
                            file_header);
                    is_gif = MediaProcessor::is_gif_from_content(file_header);

                    PLUGIN_INFO("qq_to_tg",
                                "[图片类型检测] 文件头部MIME检测成功: {} -> {} "
                                "(is_gif={}, 读取了{}字节)",
                                url, detected_mime, is_gif, file_header.size());
                    PLUGIN_DEBUG("qq_to_tg",
                                 "[图片类型检测] 文件头部16进制: {}",
                                 to_hex_string(file_header));

                    // 创建新的缓存记录
                    obcx::storage::QQStickerMapping new_mapping;
                    new_mapping.qq_sticker_hash = qq_sticker_hash;
                    new_mapping.telegram_file_id = ""; // 暂时为空
                    new_mapping.file_type = is_gif ? "animation" : "photo";
                    new_mapping.is_gif = is_gif;
                    new_mapping.content_type = detected_mime;
                    new_mapping.created_at = std::chrono::system_clock::now();
                    new_mapping.last_used_at = std::chrono::system_clock::now();
                    new_mapping.last_checked_at =
                        std::chrono::system_clock::now();
                    db_manager_->save_qq_sticker_mapping(new_mapping);
                    PLUGIN_DEBUG("qq_to_tg", "[图片类型检测] 缓存记录已保存");
                  } else {
                    is_gif = true;
                    PLUGIN_WARN(
                        "qq_to_tg",
                        "[图片类型检测] 文件头部内容为空，回退到默认行为: {}",
                        url);
                  }
                } else {
                  is_gif = true;
                  PLUGIN_WARN("qq_to_tg",
                              "[图片类型检测] Range请求失败，状态码: {}, "
                              "回退到默认行为: {}",
                              response.status_code, url);
                }
              } catch (const std::exception &e) {
                is_gif = true;
                PLUGIN_ERROR(
                    "qq_to_tg",
                    "[图片类型检测] "
                    "QQ文件Range请求或检测异常，回退到默认行为: {} - {}",
                    url, e.what());
              }
            }
          } catch (const std::exception &e) {
            // 异常情况下回退到旧逻辑
            is_gif = true;
            PLUGIN_ERROR("qq_to_tg",
                         "图片类型检测异常，回退到默认行为: {} - {}", url,
                         e.what());
            PLUGIN_WARN("qq_to_tg", "将该图片作为动图处理以确保正常转发");
          }
        }

        // 检测是否为表情包 (通过多个指标判断)
        bool is_sticker = false;
        // 1. 检查文件名是否包含表情包特征
        if (!file_name.empty() &&
            (file_name.find("sticker") != std::string::npos ||
             file_name.find("emoji") != std::string::npos)) {
          is_sticker = true;
        }
        // 2. 检查子类型 - subType=1可能表示动图表情
        if (segment.data.contains("subType") && segment.data["subType"] == 1) {
          is_sticker = true; // GIF表情包也算
        }
        // 3. 检查URL中的表情包特征
        if (!url.empty() && (url.find("emoticon") != std::string::npos ||
                             url.find("sticker") != std::string::npos ||
                             url.find("emoji") != std::string::npos)) {
          is_sticker = true;
        }

        if (is_sticker) {
          // QQ表情包处理：使用缓存系统优化
          try {
            // 计算QQ表情包的唯一hash
            std::string qq_sticker_hash =
                obcx::storage::DatabaseManager::calculate_hash(
                    segment.data.value("file", "") + "_" +
                    segment.data.value("url", ""));

            // 查询缓存
            auto cached_mapping =
                db_manager_->get_qq_sticker_mapping(qq_sticker_hash);
            if (cached_mapping.has_value()) {
              db_manager_->update_qq_sticker_last_used(qq_sticker_hash);

              // 根据模式获取显示发送者配置
              bool show_sender_for_sticker = false;
              if (bridge_config->mode == BridgeMode::GROUP_TO_GROUP) {
                show_sender_for_sticker = bridge_config->show_qq_to_tg_sender;
              } else {
                const TopicBridgeConfig *topic_config =
                    get_topic_config(telegram_group_id, topic_id);
                show_sender_for_sticker =
                    topic_config ? topic_config->show_qq_to_tg_sender : false;
              }

              std::string caption_info =
                  show_sender_for_sticker
                      ? fmt::format("[{}]\t", sender_display_name)
                      : "";

              std::string response;
              if (topic_id == -1) {
                // 群组模式：发送到群组
                response =
                    co_await static_cast<obcx::core::TGBot &>(telegram_bot)
                        .send_group_photo(telegram_group_id,
                                          cached_mapping->telegram_file_id,
                                          caption_info);
              } else {
                // Topic模式：使用topic消息发送
                obcx::common::Message sticker_message;
                obcx::common::MessageSegment img_segment;
                img_segment.type = "image";
                img_segment.data["file"] = cached_mapping->telegram_file_id;
                if (!caption_info.empty()) {
                  obcx::common::MessageSegment caption_segment;
                  caption_segment.type = "text";
                  caption_segment.data["text"] = caption_info;
                  sticker_message.push_back(caption_segment);
                }
                sticker_message.push_back(img_segment);
                response =
                    co_await static_cast<obcx::core::TGBot &>(telegram_bot)
                        .send_topic_message(telegram_group_id, topic_id,
                                            sticker_message);
              }

              PLUGIN_INFO("qq_to_tg", "使用缓存的QQ表情包发送成功: {} -> {}",
                          qq_sticker_hash, cached_mapping->telegram_file_id);
              co_return; // 直接返回，不添加到普通消息中
            }
            // 缓存未命中，使用普通方式发送并保存file_id
            PLUGIN_INFO("qq_to_tg", "QQ表情包缓存未命中，将上传并缓存: {}",
                        qq_sticker_hash);
          } catch (const std::exception &e) {
            PLUGIN_ERROR("qq_to_tg", "处理QQ表情包缓存时出错: {}", e.what());
          }

          // 无论是否有缓存，都继续普通流程（缓存未命中或出错时）
          if (is_gif) {
            converted_segment.type = "animation";
          } else {
            converted_segment.type = "image"; // 使用photo而不是image以启用压缩
          }
          PLUGIN_DEBUG("qq_to_tg", "检测到QQ表情包，使用压缩模式转发: {}",
                       file_name);
        } else if (is_gif) {
          // 普通GIF动图转换为Telegram animation
          converted_segment.type = "animation";
          PLUGIN_DEBUG("qq_to_tg", "检测到QQ GIF动图，转为Telegram动画: {}",
                       file_name);
        } else {
          // 普通图片保持不变
          PLUGIN_DEBUG("qq_to_tg", "转发QQ图片文件: {}", file_name);
        }
      } else if (segment.type == "record") {
        // QQ语音转为Telegram音频
        std::string file_name = segment.data.value("file", "");
        std::string url = segment.data.value("url", "");

        PLUGIN_DEBUG("qq_to_tg", "转发QQ语音文件: file={}, url={}", file_name,
                     url);

        // 优先使用URL进行远程下载
        if (!url.empty()) {
          converted_segment.data["file"] = url;
        }
      } else if (segment.type == "video") {
        // QQ视频直接转发
        std::string file_name = segment.data.value("file", "");
        std::string url = segment.data.value("url", "");

        PLUGIN_DEBUG("qq_to_tg", "转发QQ视频文件: file={}, url={}", file_name,
                     url);

        // 优先使用URL进行远程下载
        if (!url.empty()) {
          converted_segment.data["file"] = url;
        }
      } else if (segment.type == "file") {
        // QQ文件转为Telegram文档
        converted_segment.type = "document";

        // 提取QQ文件的file和url信息
        std::string file_name = segment.data.value("file", "");
        std::string url = segment.data.value("url", "");

        PLUGIN_DEBUG("qq_to_tg", "转发QQ文件: file={}, url={}", file_name, url);

        // 提取更多信息用于诊断
        std::string file_id = segment.data.value("file_id", "");
        std::string file_size = segment.data.value("file_size", "");

        if (!url.empty()) {
          // 有URL时使用远程下载
          converted_segment.data["file"] = url;
          PLUGIN_DEBUG("qq_to_tg", "使用QQ文件URL进行转发: {}", url);
        } else if (!file_id.empty()) {
          // URL为空但有file_id时，使用LLOneBot的文件URL获取API
          PLUGIN_DEBUG("qq_to_tg", "URL为空，尝试通过file_id获取文件: {}",
                       file_id);
          try {
            std::string response;
            // 根据消息来源选择API：群聊使用get_group_file_url，私聊使用get_private_file_url
            auto *qq_bot_ptr = static_cast<obcx::core::QQBot *>(&qq_bot);
            if (event.group_id.has_value()) {
              // 群聊文件
              std::string group_id = event.group_id.value();
              response =
                  co_await qq_bot_ptr->get_group_file_url(group_id, file_id);
              PLUGIN_DEBUG("qq_to_tg", "get_group_file_url API响应: {}",
                           response);
            } else {
              // 私聊文件
              std::string user_id = event.user_id;
              response =
                  co_await qq_bot_ptr->get_private_file_url(user_id, file_id);
              PLUGIN_DEBUG("qq_to_tg", "get_private_file_url API响应: {}",
                           response);
            }

            nlohmann::json response_json = nlohmann::json::parse(response);

            if (response_json.contains("status") &&
                response_json["status"] == "ok" &&
                response_json.contains("data") &&
                response_json["data"].contains("url")) {
              std::string download_url = response_json["data"]["url"];
              converted_segment.data.erase("file_id");
              converted_segment.data["url"] = download_url;
              PLUGIN_DEBUG("qq_to_tg", "成功通过API获取文件下载URL: {}",
                           download_url);
            } else {
              throw std::runtime_error("API响应中没有找到下载URL");
            }
          } catch (const std::exception &e) {
            PLUGIN_WARN("qq_to_tg", "通过API获取文件URL失败: {}", e.what());
            // 转换为错误提示
            converted_segment.type = "text";
            converted_segment.data.clear();
            converted_segment.data["text"] =
                fmt::format("[文件: {} ({} bytes)]\n❌ 无法获取下载链接",
                            file_name, file_size);
          }
        } else {
          // 既没有URL也没有file_id
          converted_segment.type = "text";
          converted_segment.data.clear();
          converted_segment.data["text"] = fmt::format(
              "[文件: {} ({} bytes)]\n❌ 缺少文件信息", file_name, file_size);
          PLUGIN_WARN("qq_to_tg", "QQ文件缺少URL和file_id信息: {}", file_name);
        }
      } else if (segment.type == "face") {
        // QQ表情转为文本（表情包优化：静默发送）
        converted_segment.type = "text";
        std::string face_id = segment.data.value("id", "0");
        converted_segment.data.clear();
        converted_segment.data["text"] = fmt::format("[QQ表情:{}]", face_id);
        PLUGIN_DEBUG("qq_to_tg", "转换QQ表情为文本提示: face_id={}", face_id);
      } else if (segment.type == "at") {
        // QQ@消息转为文本，使用用户名而不是QQ号
        converted_segment.type = "text";
        std::string qq_user_id = segment.data.value("qq", "unknown");
        converted_segment.data.clear();

        // 从数据库查询用户的显示名称（使用群组特定的昵称）
        std::string at_display_name = db_manager_->get_user_display_name(
            "qq", qq_user_id, event.group_id.value_or(""));

        // 如果查询到的显示名称不是用户ID本身，说明有昵称信息
        if (at_display_name != qq_user_id) {
          converted_segment.data["text"] = fmt::format("@{} ", at_display_name);
          PLUGIN_DEBUG("qq_to_tg", "转换QQ@消息: {} -> @{}", qq_user_id,
                       at_display_name);
        } else {
          // 没有昵称信息，回退到原来的格式但尝试获取一次
          if (db_manager_->should_fetch_user_info(
                  "qq", qq_user_id, event.group_id.value_or(""))) {
            try {
              // 尝试获取群成员信息
              std::string response = co_await qq_bot.get_group_member_info(
                  qq_group_id, qq_user_id, false);
              nlohmann::json response_json = nlohmann::json::parse(response);

              PLUGIN_DEBUG("qq_to_tg", "QQ@用户群成员信息API响应: {}",
                           response);

              if (response_json.contains("status") &&
                  response_json["status"] == "ok" &&
                  response_json.contains("data") &&
                  response_json["data"].is_object()) {
                auto data = response_json["data"];
                PLUGIN_DEBUG("qq_to_tg", "QQ@用户群成员信息详细数据: {}",
                             data.dump());
                obcx::storage::UserInfo user_info;
                user_info.platform = "qq";
                user_info.user_id = qq_user_id;
                user_info.group_id =
                    event.group_id.value_or(""); // 群组特定的用户信息
                user_info.last_updated = std::chrono::system_clock::now();

                std::string general_nickname, card, title;

                if (data.contains("nickname") && data["nickname"].is_string()) {
                  general_nickname = data["nickname"];
                }

                if (data.contains("card") && data["card"].is_string()) {
                  card = data["card"];
                }

                if (data.contains("title") && data["title"].is_string()) {
                  title = data["title"];
                }

                // 优先级：群名片 > 群头衔 > 一般昵称
                // 将最优先的名称存储在nickname字段中，便于显示逻辑处理
                if (!card.empty()) {
                  user_info.nickname = card;
                  PLUGIN_DEBUG("qq_to_tg",
                               "使用QQ@用户群名片作为显示名称: {} -> {}",
                               qq_user_id, card);
                } else if (!title.empty()) {
                  user_info.nickname = title;
                  PLUGIN_DEBUG("qq_to_tg",
                               "使用QQ@用户群头衔作为显示名称: {} -> {}",
                               qq_user_id, title);
                } else if (!general_nickname.empty()) {
                  user_info.nickname = general_nickname;
                  PLUGIN_DEBUG("qq_to_tg",
                               "使用QQ@用户一般昵称作为显示名称: {} -> {}",
                               qq_user_id, general_nickname);
                }

                // 同时保存群头衔到title字段供后续使用
                if (!title.empty()) {
                  user_info.title = title;
                }

                // 保存用户信息并更新显示名称
                if (db_manager_->save_or_update_user(user_info)) {
                  at_display_name = db_manager_->get_user_display_name(
                      "qq", qq_user_id, event.group_id.value_or(""));
                  converted_segment.data["text"] =
                      fmt::format("@{} ", at_display_name);
                  PLUGIN_DEBUG("qq_to_tg", "实时获取QQ@用户信息成功：{} -> @{}",
                               qq_user_id, at_display_name);
                } else {
                  converted_segment.data["text"] =
                      fmt::format("[@{}] ", qq_user_id);
                }
              } else {
                converted_segment.data["text"] =
                    fmt::format("[@{}] ", qq_user_id);
              }
            } catch (const std::exception &e) {
              PLUGIN_DEBUG("qq_to_tg", "获取QQ@用户信息失败：{}, 使用用户ID",
                           e.what());
              converted_segment.data["text"] =
                  fmt::format("[@{}] ", qq_user_id);
            }
          } else {
            converted_segment.data["text"] = fmt::format("[@{}] ", qq_user_id);
          }
        }
      } else if (segment.type == "shake") {
        // QQ戳一戳转为文本
        converted_segment.type = "text";
        converted_segment.data.clear();
        converted_segment.data["text"] = "[戳一戳]";
        PLUGIN_DEBUG("qq_to_tg", "转换QQ戳一戳为文本提示");
      } else if (segment.type == "music") {
        // QQ音乐分享转为文本
        converted_segment.type = "text";
        std::string title = segment.data.value("title", "未知音乐");
        converted_segment.data.clear();
        converted_segment.data["text"] = fmt::format("[音乐分享: {}]", title);
        PLUGIN_DEBUG("qq_to_tg", "转换QQ音乐分享为文本: title={}", title);
      } else if (segment.type == "share") {
        // QQ链接分享转为文本
        converted_segment.type = "text";
        std::string url = segment.data.value("url", "");
        std::string title = segment.data.value("title", "链接分享");
        converted_segment.data.clear();
        converted_segment.data["text"] = fmt::format("[{}]\t{}", title, url);
        PLUGIN_DEBUG("qq_to_tg", "转换QQ链接分享为文本: title={}, url={}",
                     title, url);
      } else if (segment.type == "json") {
        // QQ小程序JSON消息处理
        try {
          std::string json_data = segment.data.value("data", "");
          if (!json_data.empty()) {
            auto parse_result = parse_miniapp_json(json_data);
            converted_segment = format_miniapp_message(parse_result);
            PLUGIN_DEBUG("qq_to_tg", "转换QQ小程序JSON: success={}, title={}",
                         parse_result.success, parse_result.title);
          } else {
            converted_segment.type = "text";
            converted_segment.data.clear();
            converted_segment.data["text"] = "📱 [小程序-无数据]";
            PLUGIN_DEBUG("qq_to_tg", "QQ小程序JSON消息无数据");
          }
        } catch (const std::exception &e) {
          converted_segment.type = "text";
          converted_segment.data.clear();
          converted_segment.data["text"] = "📱 [小程序解析错误]";
          PLUGIN_ERROR("qq_to_tg", "处理QQ小程序JSON时出错: {}", e.what());
        }
      } else if (segment.type == "app") {
        // QQ应用分享消息处理
        try {
          std::string app_data = segment.data.dump();
          auto parse_result = parse_miniapp_json(app_data);
          if (!parse_result.success) {
            // 如果JSON解析失败，尝试直接提取字段
            parse_result.title = segment.data.value("title", "应用分享");
            parse_result.description = segment.data.value("content", "");
            parse_result.app_name = segment.data.value("name", "");
            if (segment.data.contains("url")) {
              parse_result.urls.push_back(segment.data.value("url", ""));
              parse_result.success = true;
            }
          }
          converted_segment = format_miniapp_message(parse_result);
          PLUGIN_DEBUG("qq_to_tg", "转换QQ应用分享: success={}, title={}",
                       parse_result.success, parse_result.title);
        } catch (const std::exception &e) {
          converted_segment.type = "text";
          converted_segment.data.clear();
          converted_segment.data["text"] = "📱 [应用分享解析错误]";
          PLUGIN_ERROR("qq_to_tg", "处理QQ应用分享时出错: {}", e.what());
        }
      } else if (segment.type == "ark") {
        // QQ ARK卡片消息处理
        try {
          std::string ark_data = segment.data.dump();
          auto parse_result = parse_miniapp_json(ark_data);
          if (!parse_result.success) {
            // ARK消息的特殊处理
            parse_result.title = segment.data.value("prompt", "ARK卡片");
            parse_result.description = segment.data.value("desc", "");

            // 从kv数组中提取信息
            if (segment.data.contains("kv") && segment.data["kv"].is_array()) {
              for (const auto &kv : segment.data["kv"]) {
                if (kv.contains("key") && kv.contains("value")) {
                  std::string key = kv["key"];
                  if (key.find("URL") != std::string::npos ||
                      key.find("url") != std::string::npos) {
                    parse_result.urls.push_back(kv["value"]);
                  }
                }
              }
            }
            parse_result.success =
                !parse_result.urls.empty() || !parse_result.title.empty();
          }
          converted_segment = format_miniapp_message(parse_result);
          PLUGIN_DEBUG("qq_to_tg", "转换QQ ARK卡片: success={}, title={}",
                       parse_result.success, parse_result.title);
        } catch (const std::exception &e) {
          converted_segment.type = "text";
          converted_segment.data.clear();
          converted_segment.data["text"] = "📱 [ARK卡片解析错误]";
          PLUGIN_ERROR("qq_to_tg", "处理QQ ARK卡片时出错: {}", e.what());
        }
      } else if (segment.type == "miniapp") {
        // QQ小程序专用消息处理 (如果存在此类型)
        try {
          std::string miniapp_data = segment.data.dump();
          auto parse_result = parse_miniapp_json(miniapp_data);
          if (!parse_result.success) {
            // 小程序消息的直接字段提取
            parse_result.title = segment.data.value("title", "小程序");
            parse_result.description = segment.data.value("desc", "");
            parse_result.app_name = segment.data.value("appid", "");
            if (segment.data.contains("url")) {
              parse_result.urls.push_back(segment.data.value("url", ""));
              parse_result.success = true;
            }
          }
          converted_segment = format_miniapp_message(parse_result);
          PLUGIN_DEBUG("qq_to_tg", "转换QQ小程序: success={}, title={}",
                       parse_result.success, parse_result.title);
        } catch (const std::exception &e) {
          converted_segment.type = "text";
          converted_segment.data.clear();
          converted_segment.data["text"] = "📱 [小程序解析错误]";
          PLUGIN_ERROR("qq_to_tg", "处理QQ小程序时出错: {}", e.what());
        }
      } else {
        // 其他类型保持原样
        PLUGIN_DEBUG("qq_to_tg", "保持QQ消息段原样: type={}", segment.type);
      }

      PLUGIN_DEBUG("qq_to_tg", "添加转换后的消息段到message_to_send: type={}",
                   converted_segment.type);
      message_to_send.push_back(converted_segment);
    };

    // 先收集消息中的所有图片，用于批量处理
    std::vector<obcx::common::MessageSegment> image_segments;
    std::vector<obcx::common::MessageSegment> other_segments;

    for (const auto &segment : event.message) {
      if (segment.type == "reply") {
        continue; // 跳过reply段
      }

      if (segment.type == "image") {
        image_segments.push_back(segment);
      } else {
        other_segments.push_back(segment);
      }
    }

    // 批量处理图片（如果有多张图片）
    if (image_segments.size() > 1) {
      PLUGIN_INFO("qq_to_tg", "检测到多张图片({})，进行聚合处理",
                  image_segments.size());

      // 添加多图片提示
      obcx::common::MessageSegment multi_image_tip;
      multi_image_tip.type = "text";
      multi_image_tip.data["text"] =
          fmt::format("\n📸 共{}张图片：\n", image_segments.size());
      message_to_send.push_back(multi_image_tip);

      // 依次处理每张图片，但标注序号
      for (size_t i = 0; i < image_segments.size(); i++) {
        const auto &img_segment = image_segments[i];

        // 添加图片序号
        obcx::common::MessageSegment img_number;
        img_number.type = "text";
        img_number.data["text"] = fmt::format("{}. ", i + 1);
        message_to_send.push_back(img_number);

        // 处理图片
        co_await handle_qq_media(img_segment);
      }

      PLUGIN_DEBUG("qq_to_tg", "完成{}张图片的聚合处理", image_segments.size());
    } else if (image_segments.size() == 1) {
      // 单张图片正常处理
      co_await handle_qq_media(image_segments[0]);
    }

    // 处理其他类型的消息段
    for (const auto &segment : other_segments) {
      // 跳过reply段，因为我们已经处理过了
      if (segment.type == "reply") {
        continue;
      }

      // 特殊处理合并转发消息
      if (segment.type == "forward") {
        try {
          // 获取转发消息ID
          std::string forward_id = segment.data.value("id", "");
          if (!forward_id.empty()) {
            PLUGIN_DEBUG("qq_to_tg", "处理合并转发消息，ID: {}", forward_id);

            // 获取合并转发内容
            std::string forward_response =
                co_await static_cast<obcx::core::QQBot &>(qq_bot)
                    .get_forward_msg(forward_id);
            nlohmann::json forward_json =
                nlohmann::json::parse(forward_response);

            if (forward_json.contains("status") &&
                forward_json["status"] == "ok" &&
                forward_json.contains("data") &&
                forward_json["data"].is_object()) {
              auto forward_data = forward_json["data"];

              // 添加合并转发标题
              obcx::common::MessageSegment forward_title_segment;
              forward_title_segment.type = "text";
              forward_title_segment.data["text"] = "\n📋 合并转发消息:\n";
              message_to_send.push_back(forward_title_segment);

              // 收集合并转发中的所有图片
              std::vector<obcx::common::MessageSegment> forward_images;

              // 处理转发消息中的每个节点
              if (forward_data.contains("messages") &&
                  forward_data["messages"].is_array()) {
                for (const auto &msg_node : forward_data["messages"]) {
                  if (msg_node.is_object()) {
                    // 获取发送者信息
                    std::string node_sender =
                        msg_node.value("sender", nlohmann::json::object())
                            .value("nickname", "未知用户");

                    // 处理content数组
                    std::string node_content = "";
                    int node_image_count = 0;
                    if (msg_node.contains("content") &&
                        msg_node["content"].is_array()) {
                      for (const auto &content_seg : msg_node["content"]) {
                        if (content_seg.is_object() &&
                            content_seg.contains("type")) {
                          std::string seg_type = content_seg["type"];
                          if (seg_type == "text" &&
                              content_seg.contains("data") &&
                              content_seg["data"].contains("text")) {
                            node_content +=
                                content_seg["data"]["text"].get<std::string>();
                          } else if (seg_type == "face" &&
                                     content_seg.contains("data") &&
                                     content_seg["data"].contains("id")) {
                            node_content += fmt::format(
                                "[表情:{}]",
                                content_seg["data"]["id"].get<std::string>());
                          } else if (seg_type == "image") {
                            // 收集图片信息用于后续发送
                            node_image_count++;
                            node_content += fmt::format(
                                "[图片{}]", forward_images.size() + 1);

                            // 创建图片消息段
                            obcx::common::MessageSegment img_segment;
                            img_segment.type = "image";
                            if (content_seg.contains("data")) {
                              auto img_data = content_seg["data"];
                              if (img_data.contains("url") &&
                                  img_data["url"].is_string()) {
                                img_segment.data["url"] =
                                    img_data["url"].get<std::string>();
                                img_segment.data["file"] =
                                    img_data["url"].get<std::string>();
                              } else if (img_data.contains("file") &&
                                         img_data["file"].is_string()) {
                                img_segment.data["file"] =
                                    img_data["file"].get<std::string>();
                              }
                              // 复制其他可能有用的字段
                              if (img_data.contains("subType")) {
                                img_segment.data["subType"] =
                                    img_data["subType"];
                              }
                            }
                            forward_images.push_back(img_segment);
                            PLUGIN_DEBUG(
                                "qq_to_tg", "收集合并转发中的图片: url={}",
                                img_segment.data.value(
                                    "url",
                                    img_segment.data.value("file", "无URL")));
                          } else if (seg_type == "at" &&
                                     content_seg.contains("data") &&
                                     content_seg["data"].contains("qq")) {
                            node_content += fmt::format(
                                "[@{}]",
                                content_seg["data"]["qq"].get<std::string>());
                          } else {
                            node_content += fmt::format("[{}]", seg_type);
                          }
                        }
                      }
                    } else if (msg_node.contains("content") &&
                               msg_node["content"].is_string()) {
                      // 兼容字符串格式的content
                      node_content = msg_node["content"].get<std::string>();
                    }

                    // 添加每个转发消息的内容
                    obcx::common::MessageSegment node_segment;
                    node_segment.type = "text";
                    node_segment.data["text"] =
                        fmt::format("👤 {}: {}\n", node_sender, node_content);
                    message_to_send.push_back(node_segment);
                  }
                }
              }

              // 如果收集到了图片，使用sendMediaGroup批量发送
              if (!forward_images.empty()) {
                PLUGIN_INFO(
                    "qq_to_tg",
                    "合并转发消息中发现 {} 张图片，准备使用MediaGroup发送",
                    forward_images.size());

                // 构建媒体组列表 (type, url)
                std::vector<std::pair<std::string, std::string>> media_list;
                for (const auto &img_seg : forward_images) {
                  std::string url =
                      img_seg.data.value("url", img_seg.data.value("file", ""));
                  if (!url.empty()) {
                    media_list.emplace_back("photo", url);
                    PLUGIN_DEBUG("qq_to_tg", "添加图片到MediaGroup: {}", url);
                  }
                }

                // 如果有有效的图片URL，使用sendMediaGroup发送
                if (!media_list.empty()) {
                  try {
                    std::string caption = fmt::format(
                        "📸 合并转发消息中的{}张图片", media_list.size());
                    std::optional<int64_t> opt_topic_id =
                        (topic_id == -1) ? std::nullopt
                                         : std::optional<int64_t>(topic_id);

                    std::string media_response =
                        co_await static_cast<obcx::core::TGBot &>(telegram_bot)
                            .send_media_group(telegram_group_id, media_list,
                                              caption, opt_topic_id,
                                              std::nullopt);

                    PLUGIN_INFO("qq_to_tg",
                                "成功通过MediaGroup发送 {} 张图片，响应: {}",
                                media_list.size(), media_response);
                  } catch (const std::exception &e) {
                    PLUGIN_ERROR("qq_to_tg", "通过MediaGroup发送图片失败: {}",
                                 e.what());
                    // 失败时添加错误提示到文本消息
                    obcx::common::MessageSegment error_segment;
                    error_segment.type = "text";
                    error_segment.data["text"] =
                        fmt::format("\n[发送{}张图片失败: {}]",
                                    media_list.size(), e.what());
                    message_to_send.push_back(error_segment);
                  }
                }
              }

              PLUGIN_INFO(
                  "qq_to_tg", "成功处理合并转发消息，包含 {} 条消息，{} 张图片",
                  forward_data.value("messages", nlohmann::json::array())
                      .size(),
                  forward_images.size());
            } else {
              PLUGIN_WARN("qq_to_tg", "获取合并转发内容失败: {}",
                          forward_response);
              // 添加失败提示
              obcx::common::MessageSegment error_segment;
              error_segment.type = "text";
              error_segment.data["text"] = "[合并转发消息获取失败]";
              message_to_send.push_back(error_segment);
            }
          }
        } catch (const std::exception &e) {
          PLUGIN_ERROR("qq_to_tg", "处理合并转发消息时出错: {}", e.what());
          // 添加错误提示
          obcx::common::MessageSegment error_segment;
          error_segment.type = "text";
          error_segment.data["text"] = "[合并转发消息处理失败]";
          message_to_send.push_back(error_segment);
        }
        continue;
      }

      // 处理单个node消息段（自定义转发节点）
      if (segment.type == "node") {
        try {
          // node段包含用户ID、昵称和内容
          std::string node_user_id = segment.data.value("user_id", "");
          std::string node_nickname =
              segment.data.value("nickname", "未知用户");

          // 内容可能是字符串或消息段数组
          if (segment.data.contains("content")) {
            auto content = segment.data["content"];

            obcx::common::MessageSegment node_segment;
            node_segment.type = "text";

            if (content.is_string()) {
              // 简单文本内容
              node_segment.data["text"] = fmt::format(
                  "👤 {}: {}\n", node_nickname, content.get<std::string>());
            } else if (content.is_array()) {
              // 复杂消息段内容
              std::string node_text = fmt::format("👤 {}: ", node_nickname);
              for (const auto &content_segment : content) {
                if (content_segment.is_object() &&
                    content_segment.contains("type")) {
                  std::string seg_type = content_segment["type"];
                  if (seg_type == "text" && content_segment.contains("data") &&
                      content_segment["data"].contains("text")) {
                    node_text +=
                        content_segment["data"]["text"].get<std::string>();
                  } else if (seg_type == "face") {
                    node_text += fmt::format(
                        "[表情:{}]",
                        content_segment.value("data", nlohmann::json::object())
                            .value("id", "0"));
                  } else if (seg_type == "image") {
                    node_text += "[图片]";
                  } else {
                    node_text += fmt::format("[{}]", seg_type);
                  }
                }
              }
              node_text += "\n";
              node_segment.data["text"] = node_text;
            } else {
              node_segment.data["text"] =
                  fmt::format("👤 {}: [未知内容]\n", node_nickname);
            }

            message_to_send.push_back(node_segment);
            PLUGIN_DEBUG("qq_to_tg", "处理node消息段: 用户 {} ({})",
                         node_nickname, node_user_id);
          }
        } catch (const std::exception &e) {
          PLUGIN_ERROR("qq_to_tg", "处理node消息段时出错: {}", e.what());
          obcx::common::MessageSegment error_segment;
          error_segment.type = "text";
          error_segment.data["text"] = "[转发节点处理失败]";
          message_to_send.push_back(error_segment);
        }
        continue;
      }

      // 处理其他消息类型
      co_await handle_qq_media(segment);
    }

    // 发送到Telegram群或特定topic（支持重试）
    std::optional<std::string> telegram_message_id;
    std::string failure_reason;

    try {
      std::string response;
      if (topic_id == -1) {
        // 群组模式：发送到群组
        response = co_await telegram_bot.send_group_message(telegram_group_id,
                                                            message_to_send);
        PLUGIN_DEBUG("qq_to_tg", "群组模式：QQ群 {} 转发到Telegram群 {}",
                     qq_group_id, telegram_group_id);
      } else {
        // Topic模式：发送到特定topic
        auto &tg_bot = static_cast<obcx::core::TGBot &>(telegram_bot);
        response = co_await tg_bot.send_topic_message(
            telegram_group_id, topic_id, message_to_send);
        PLUGIN_DEBUG("qq_to_tg",
                     "Topic模式：QQ群 {} 转发到Telegram群 {} 的topic {}",
                     qq_group_id, telegram_group_id, topic_id);
      }

      // 解析响应获取Telegram消息ID
      if (!response.empty()) {
        PLUGIN_DEBUG("qq_to_tg", "Telegram API响应: {}", response);
        nlohmann::json response_json = nlohmann::json::parse(response);
        if (response_json.contains("result") &&
            response_json["result"].is_object() &&
            response_json["result"].contains("message_id")) {
          telegram_message_id = std::to_string(
              response_json["result"]["message_id"].get<int64_t>());

          // 记录消息ID映射
          obcx::storage::MessageMapping mapping;
          mapping.source_platform = "qq";
          mapping.source_message_id = event.message_id;
          mapping.target_platform = "telegram";
          mapping.target_message_id = telegram_message_id.value();
          mapping.created_at = std::chrono::system_clock::now();
          db_manager_->add_message_mapping(mapping);

          PLUGIN_INFO("qq_to_tg",
                      "QQ消息 {} 成功转发到Telegram，Telegram消息ID: {}",
                      event.message_id, telegram_message_id.value());
        } else {
          failure_reason = fmt::format("Invalid response format: {}", response);
          PLUGIN_WARN("qq_to_tg",
                      "转发QQ消息后，无法解析Telegram消息ID。响应: {}",
                      response);
        }
      } else {
        failure_reason = "Empty response from Telegram API";
        PLUGIN_WARN("qq_to_tg", "Telegram API返回空响应");
      }
    } catch (const std::exception &e) {
      failure_reason = fmt::format("Send failed: {}", e.what());
      PLUGIN_WARN("qq_to_tg", "发送QQ消息到Telegram时出错: {}", e.what());
    }

    // 如果发送失败且启用了重试队列，添加到重试队列
    if (!telegram_message_id.has_value() && retry_manager_ &&
        config::ENABLE_RETRY_QUEUE) {
      PLUGIN_INFO("qq_to_tg", "消息发送失败，添加到重试队列: {} -> {}",
                  event.message_id, telegram_group_id);
      retry_manager_->add_message_retry(
          "qq", "telegram", event.message_id, message_to_send,
          telegram_group_id, qq_group_id, topic_id,
          config::MESSAGE_RETRY_MAX_ATTEMPTS, failure_reason);
    } else if (!telegram_message_id.has_value()) {
      // 如果没有启用重试或没有重试管理器，记录错误
      PLUGIN_ERROR("qq_to_tg", "消息发送失败且未启用重试: {}", failure_reason);
    }
  } catch (const std::exception &e) {
    PLUGIN_ERROR("qq_to_tg", "转发QQ消息到Telegram时出错: {}", e.what());
    qq_bot.error_notify(
        qq_group_id, fmt::format("转发消息到Telegram失败: {}", e.what()), true);
  }
}

auto QQHandler::handle_recall_event(obcx::core::IBot &telegram_bot,
                                    obcx::core::IBot &qq_bot,
                                    obcx::common::Event event)
    -> boost::asio::awaitable<void> {
  try {
    // 尝试转换为NoticeEvent
    auto notice_event = std::get<obcx::common::NoticeEvent>(event);

    // 检查是否是撤回事件
    if (notice_event.notice_type != "group_recall") {
      co_return;
    }

    // 确保是群消息撤回且有群ID
    if (!notice_event.group_id.has_value()) {
      PLUGIN_DEBUG("qq_to_tg", "撤回事件缺少群ID");
      co_return;
    }

    const std::string qq_group_id = notice_event.group_id.value();

    // 从事件数据中获取被撤回的消息ID
    std::string recalled_message_id;
    if (notice_event.data.contains("message_id")) {
      // message_id可能是整数或字符串，需要正确处理
      auto message_id_value = notice_event.data["message_id"];
      if (message_id_value.is_string()) {
        recalled_message_id = message_id_value.get<std::string>();
      } else if (message_id_value.is_number()) {
        recalled_message_id = std::to_string(message_id_value.get<int64_t>());
      } else {
        PLUGIN_WARN("qq_to_tg", "撤回事件message_id类型不支持: {}",
                    message_id_value.type_name());
        co_return;
      }
    } else {
      PLUGIN_WARN("qq_to_tg", "撤回事件缺少message_id信息");
      co_return;
    }

    PLUGIN_INFO("qq_to_tg", "处理QQ群 {} 中消息 {} 的撤回事件", qq_group_id,
                recalled_message_id);

    // 查找对应的Telegram消息ID
    auto target_message_id = db_manager_->get_target_message_id(
        "qq", recalled_message_id, "telegram");

    if (!target_message_id.has_value()) {
      PLUGIN_DEBUG("qq_to_tg", "未找到QQ消息 {} 对应的Telegram消息映射",
                   recalled_message_id);
      co_return;
    }

    try {
      // 尝试在Telegram上撤回消息
      auto response =
          co_await telegram_bot.delete_message(target_message_id.value());

      // 解析响应
      nlohmann::json response_json = nlohmann::json::parse(response);

      if (response_json.contains("ok") && response_json["ok"].get<bool>()) {
        PLUGIN_INFO("qq_to_tg", "成功在Telegram撤回消息: {}",
                    target_message_id.value());
      } else {
        PLUGIN_WARN("qq_to_tg", "Telegram撤回消息失败: {}, 响应: {}",
                    target_message_id.value(), response);
      }

    } catch (const std::exception &e) {
      PLUGIN_WARN("qq_to_tg", "尝试在Telegram撤回消息时出错: {}", e.what());
    }

    // 无论Telegram撤回是否成功，都删除数据库中的消息映射
    bool deleted = db_manager_->delete_message_mapping(
        "qq", recalled_message_id, "telegram");
    if (deleted) {
      PLUGIN_DEBUG("qq_to_tg", "已删除消息映射: qq:{} -> telegram:{}",
                   recalled_message_id, target_message_id.value());
    } else {
      PLUGIN_WARN("qq_to_tg", "删除消息映射失败: qq:{} -> telegram:{}",
                  recalled_message_id, target_message_id.value());
    }

  } catch (const std::bad_variant_access &e) {
    PLUGIN_DEBUG("qq_to_tg", "事件不是NoticeEvent类型，跳过撤回处理");
  } catch (const std::exception &e) {
    PLUGIN_ERROR("qq_to_tg", "处理QQ撤回事件时出错: {}", e.what());
  }
}

auto QQHandler::handle_checkalive_command(obcx::core::IBot &telegram_bot,
                                          obcx::core::IBot &qq_bot,
                                          obcx::common::MessageEvent event,
                                          const std::string &telegram_group_id)
    -> boost::asio::awaitable<void> {

  try {
    const std::string qq_group_id = event.group_id.value();

    // 获取QQ平台的心跳信息
    auto qq_heartbeat = db_manager_->get_platform_heartbeat("qq");
    // 获取Telegram平台的心跳信息
    auto telegram_heartbeat = db_manager_->get_platform_heartbeat("telegram");

    std::string response_text;

    if (qq_heartbeat.has_value()) {
      auto qq_time_point = qq_heartbeat->last_heartbeat_at;
      auto qq_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                              qq_time_point.time_since_epoch())
                              .count();

      // 计算距离现在的时间差
      auto now = std::chrono::system_clock::now();
      auto qq_duration =
          std::chrono::duration_cast<std::chrono::seconds>(now - qq_time_point)
              .count();

      response_text += fmt::format("🤖 QQ平台状态:\n");
      response_text +=
          fmt::format("最后心跳: {} ({} 秒前)\n", qq_timestamp, qq_duration);

      if (qq_duration > 60) {
        response_text += "⚠️ QQ平台可能离线\n";
      } else {
        response_text += "✅ QQ平台正常\n";
      }
    } else {
      response_text += "🤖 QQ平台状态: ❌ 无心跳记录\n";
    }

    response_text += "\n";

    if (telegram_heartbeat.has_value()) {
      auto tg_time_point = telegram_heartbeat->last_heartbeat_at;
      auto tg_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                              tg_time_point.time_since_epoch())
                              .count();

      // 计算距离现在的时间差
      auto now = std::chrono::system_clock::now();
      auto tg_duration =
          std::chrono::duration_cast<std::chrono::seconds>(now - tg_time_point)
              .count();

      response_text += fmt::format("💬 Telegram平台状态:\n");
      response_text +=
          fmt::format("最后活动: {} ({} 秒前)\n", tg_timestamp, tg_duration);

      if (tg_duration > 300) {
        // 5分钟无活动认为异常
        response_text += "⚠️ Telegram平台可能离线";
      } else {
        response_text += "✅ Telegram平台正常";
      }
    } else {
      response_text += "💬 Telegram平台状态: ❌ 无活动记录";
    }

    // 构造回复消息
    obcx::common::Message reply_message;

    // 添加回复segment
    obcx::common::MessageSegment reply_segment;
    reply_segment.type = "reply";
    reply_segment.data["id"] = event.message_id;
    reply_message.push_back(reply_segment);

    obcx::common::MessageSegment text_segment;
    text_segment.type = "text";
    text_segment.data["text"] = response_text;
    reply_message.push_back(text_segment);

    // 发送到QQ
    try {
      co_await qq_bot.send_group_message(qq_group_id, reply_message);
      PLUGIN_INFO("qq_to_tg", "/checkalive 命令处理完成");
    } catch (const std::exception &send_e) {
      PLUGIN_ERROR("qq_to_tg", "/checkalive 命令：发送回复消息失败: {}",
                   send_e.what());
    }

  } catch (const std::exception &e) {
    PLUGIN_ERROR("qq_to_tg", "处理 /checkalive 命令时出错: {}", e.what());

    // 发送错误消息 - 使用简单的错误处理，不使用co_await在catch块中
    // 这里记录错误但不发送消息，因为co_await不能在catch块中使用
  }
}

} // namespace bridge
