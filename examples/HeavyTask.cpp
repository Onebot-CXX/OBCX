/**
 * @file heavy_task_example.cpp
 * @brief 演示 OBCX 框架中重负载任务调度的使用示例
 *
 * 这个示例展示了如何使用 TaskScheduler 来执行 CPU 密集型任务，
 * 同时保持事件循环的响应性。
 */

#include "common/logger.hpp"
#include "common/message_types.hpp"
#include "core/qq_bot.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace obcx;
using namespace std::chrono_literals;

auto get_thread_id() -> std::string {
  auto thread_id = std::this_thread::get_id();
  return (std::stringstream() << thread_id).str();
}
/**
 * @brief 模拟 CPU 密集型任务：图像处理
 */
auto simulate_image_processing(const std::string &image_data) -> std::string {
  auto thread_id = get_thread_id();
  OBCX_INFO("开始处理图像数据: {} (线程ID: {})", image_data, thread_id);

  // 模拟复杂的图像处理操作
  std::this_thread::sleep_for(2s);

  // 模拟处理结果
  std::string processed = "processed_" + image_data + ".jpg";

  OBCX_INFO("图像处理完成: {} (线程ID: {})", processed, thread_id);

  return processed;
}

/**
 * @brief 模拟 CPU 密集型任务：数据分析
 */
struct AnalysisResult {
  int processed_count;
  double average_value;
  std::string summary;
};

auto simulate_data_analysis(const std::vector<int> &data) {
  auto thread_id = get_thread_id();
  OBCX_INFO("开始数据分析，数据点数量: {} (线程ID: {})", data.size(),
            thread_id);

  // 模拟复杂的数据分析
  std::this_thread::sleep_for(1500ms);

  // 计算实际统计信息
  int sum = 0;
  for (int value : data) {
    sum += value;
  }

  AnalysisResult result{
      .processed_count = static_cast<int>(data.size()),
      .average_value =
          data.empty() ? 0.0 : static_cast<double>(sum) / data.size(),
      .summary = "数据分析完成，发现了有趣的模式"};

  OBCX_INFO("数据分析完成: 平均值={:.2f} (线程ID: {})", result.average_value,
            thread_id);

  return result;
}

/**
 * @brief 消息事件处理器，演示在事件处理中使用重负载任务
 */
auto handle_message_with_heavy_tasks(core::Bot &bot,
                                     const common::MessageEvent event)
    -> asio::awaitable<void> {
  auto thread_id = get_thread_id();

  OBCX_INFO("收到消息事件，用户ID: {} (线程ID: {})", event.user_id, thread_id);

  const auto &message_text = event.raw_message;

  try {
    if (message_text.find("处理图像") != std::string::npos) {
      // 场景1: 单个重负载任务 - 图像处理
      OBCX_INFO("用户请求图像处理，调度到线程池...");

      auto result = co_await bot.run_heavy_task([&]() {
        return simulate_image_processing("user_image_" + event.user_id);
      });

      OBCX_INFO("图像处理任务完成，返回事件循环 (线程ID: {})", get_thread_id());

      // 发送处理结果
      common::Message response;
      common::MessageSegment text_segment;
      text_segment.type = "text";
      text_segment.data =
          nlohmann::json{{"text", "图像处理完成！结果: " + result}};
      response.push_back(text_segment);
      co_await bot.send_private_message(event.user_id, response);

    } else if (message_text.find("批量分析") != std::string::npos) {
      // 场景2: 批量重负载任务 - 数据分析
      OBCX_INFO("用户请求批量数据分析...");

      // 创建多个数据集进行分析
      std::vector<std::vector<int>> datasets = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
                                                {10, 20, 30, 40, 50},
                                                {100, 200, 150, 175, 225}};

      // 使用任务调度器的批量执行功能
      std::vector<std::function<AnalysisResult()>> tasks;
      for (const auto &i : datasets) {
        tasks.emplace_back(
            [dataset = i, &i]() { return simulate_data_analysis(dataset); });
      }

      auto &scheduler = bot.get_task_scheduler();
      auto results = co_await scheduler.run_heavy_tasks_batch(std::move(tasks));

      OBCX_INFO("批量分析任务完成，返回事件循环 (线程ID: {})", get_thread_id());

      // 汇总结果
      std::string summary = "批量分析完成！\n";
      for (size_t i = 0; i < results.size(); ++i) {
        const auto &result = results[i];
        summary += "数据集" + std::to_string(i + 1) +
                   ": 平均值=" + std::to_string(result.average_value) + "\n";
      }

      common::Message response;
      common::MessageSegment text_segment;
      text_segment.type = "text";
      text_segment.data = nlohmann::json{{"text", summary}};
      co_await bot.send_private_message(event.user_id, response);

    } else if (message_text.find("超时测试") != std::string::npos) {
      // 场景3: 带超时的任务执行
      OBCX_INFO("用户请求超时测试...");

      auto &scheduler = bot.get_task_scheduler();
      auto result = co_await scheduler.run_heavy_task_with_timeout(
          []() {
            // 模拟一个可能超时的任务
            std::this_thread::sleep_for(1s);
            return std::string("超时测试完成");
          },
          500ms // 设置500ms超时
      );

      common::Message response;
      common::MessageSegment text_segment;
      text_segment.type = "text";
      if (result.has_value()) {
        text_segment.data =
            nlohmann::json{{"text", "任务完成: " + result.value()}};
      } else {
        text_segment.data = nlohmann::json{{"text", "任务超时或执行失败"}};
      }
      response.push_back(text_segment);

      co_await bot.send_private_message(event.user_id, response);

    } else if (message_text.find("计算质数") != std::string::npos) {
      // 场景4: CPU 密集型计算任务
      OBCX_INFO("用户请求质数计算...");

      auto result = co_await bot.run_heavy_task([&]() {
        // 模拟计算大量质数
        auto thread_id = get_thread_id();
        OBCX_INFO("开始计算质数 (线程ID: {})", thread_id);

        std::this_thread::sleep_for(3s); // 模拟重计算

        int prime_count = 1000; // 模拟计算结果
        OBCX_INFO("质数计算完成，找到 {} 个质数 (线程ID: {})", prime_count,
                  thread_id);

        return prime_count;
      });

      OBCX_INFO("质数计算任务完成，返回事件循环 (线程ID: {})", get_thread_id());

      common::Message response;
      common::MessageSegment text_segment;
      text_segment.type = "text";
      text_segment.data =
          nlohmann::json{{"text", "质数计算完成！找到了 " +
                                      std::to_string(result) + " 个质数"}};
      response.push_back(text_segment);
      co_await bot.send_private_message(event.user_id, response);

    } else {
      // 普通消息处理（不涉及重负载任务）
      OBCX_INFO("处理普通消息，无需调度重负载任务");

      common::Message response;
      common::MessageSegment text_segment;
      text_segment.type = "text";
      text_segment.data = nlohmann::json{
          {"text", "我收到了您的消息: " + message_text +
                       "\n\n可以尝试发送以下关键词来体验重负载任务调度：\n" +
                       "• '处理图像' - 单个重负载任务\n" +
                       "• '批量分析' - 批量任务处理\n" +
                       "• '超时测试' - 带超时的任务\n" +
                       "• '计算质数' - CPU密集型计算"}};
      response.push_back(text_segment);
      co_await bot.send_private_message(event.user_id, response);
    }

  } catch (const std::exception &e) {
    OBCX_ERROR("处理消息时发生异常: {}", e.what());

    common::Message error_response;
    common::MessageSegment text_segment;
    text_segment.type = "text";
    text_segment.data =
        nlohmann::json{{"text", "处理您的请求时发生错误，请稍后重试。"}};
    error_response.push_back(text_segment);
    bot.async_send_private_message(event.user_id, error_response);
  }
}

auto main() -> int {
  try {
    OBCX_INFO("启动重负载任务调度示例...");

    // 创建 Bot 实例
    core::Bot bot;

    // 注册消息事件处理器
    bot.on_event<common::MessageEvent>(
        [](core::Bot &bot,
           const common::MessageEvent &event) -> asio::awaitable<void> {
          co_await handle_message_with_heavy_tasks(bot, event);
        });

    // 显示任务调度器信息
    auto &scheduler = bot.get_task_scheduler();
    // OBCX_INFO("任务调度器已初始化，线程池大小: {}",
    // scheduler.thread_count());

    // 连接到 OneBot 实现 (这里使用示例配置)
    // 在实际使用中，请根据您的 OneBot 实现调整连接参数
    OBCX_INFO("尝试连接到 OneBot 实现...");
    bot.connect_ws("127.0.0.1", 3001, "your_access_token");

    OBCX_INFO("Bot 启动完成，开始处理事件...");
    OBCX_INFO("发送包含 '处理图像'、'批量分析' 或 '超时测试' "
              "的消息来体验重负载任务调度！");

    // 启动 Bot (阻塞运行)
    bot.run();

  } catch (const std::exception &e) {
    OBCX_ERROR("程序运行时发生异常: {}", e.what());
    return 1;
  }

  OBCX_INFO("重负载任务调度示例结束");
  return 0;
}
