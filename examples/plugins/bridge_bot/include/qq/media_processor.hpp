#pragma once

#include "config.hpp"
#include "database/manager.hpp"

#include <boost/asio.hpp>
#include <common/message_type.hpp>
#include <interfaces/bot.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bridge::qq {

/**
 * @brief QQ Face ID to Unicode Emoji mapping table
 *
 * Maps QQ face IDs (from CQ code [CQ:face,id=xxx]) to Unicode emoji characters.
 * Based on official QQ Bot API and community resources.
 */
// clang-format off
inline const std::unordered_map<int, std::string> QQ_FACE_TO_EMOJI = {
    // Basic expressions (0-39)
    {0, "😊"},    // 微笑
    {1, "😟"},    // 撇嘴
    {2, "😍"},    // 色
    {3, "😳"},    // 发呆
    {4, "😎"},    // 得意
    {5, "😭"},    // 流泪
    {6, "☺️"},    // 害羞
    {7, "🤐"},    // 闭嘴
    {8, "😴"},    // 睡
    {9, "😢"},    // 大哭
    {10, "😅"},   // 尴尬
    {11, "😡"},   // 发怒
    {12, "😛"},   // 调皮
    {13, "😁"},   // 呲牙
    {14, "😲"},   // 惊讶
    {15, "😞"},   // 难过
    {16, "😎"},   // 酷
    {17, "😰"},   // 冷汗 (reserved, may not exist)
    {18, "😤"},   // 抓狂
    {19, "🤮"},   // 吐
    {20, "🤭"},   // 偷笑
    {21, "😊"},   // 愉快
    {22, "🙄"},   // 白眼
    {23, "😏"},   // 傲慢
    {24, "😋"},   // 饥饿
    {25, "😪"},   // 困
    {26, "😨"},   // 惊恐
    {27, "😓"},   // 流汗
    {28, "😄"},   // 憨笑
    {29, "😌"},   // 悠闲
    {30, "💪"},   // 奋斗
    {31, "🤬"},   // 咒骂
    {32, "🤔"},   // 疑问
    {33, "🤫"},   // 嘘
    {34, "😵"},   // 晕
    {35, "🤪"},   // 疯了
    {36, "😣"},   // 衰
    {37, "💀"},   // 骷髅
    {38, "🔨"},   // 敲打
    {39, "👋"},   // 再见

    // Extended expressions (40-99)
    {40, "😥"},   // 擦汗 (reserved)
    {41, "👃"},   // 抠鼻
    {42, "👏"},   // 鼓掌
    {43, "😅"},   // 糗大了
    {44, "😏"},   // 坏笑 (reserved)
    {45, "🤨"},   // 左哼哼 (reserved)
    {46, "🤨"},   // 右哼哼
    {47, "🥱"},   // 哈欠 (reserved)
    {48, "😒"},   // 鄙视 (reserved)
    {49, "😤"},   // 委屈
    {50, "😭"},   // 快哭了 (reserved)
    {51, "😈"},   // 阴险 (reserved)
    {52, "😘"},   // 亲亲 (reserved)
    {53, "😱"},   // 吓
    {54, "🔪"},   // 菜刀
    {55, "🍺"},   // 啤酒
    {56, "🏀"},   // 篮球
    {57, "🏓"},   // 乒乓
    {58, "☕"},   // 咖啡 (reserved)
    {59, "🍚"},   // 饭
    {60, "🐷"},   // 猪头
    {61, "🌹"},   // 玫瑰
    {62, "🥀"},   // 凋谢
    {63, "💋"},   // 嘴唇
    {64, "❤️"},   // 爱心
    {65, "💔"},   // 心碎 (reserved)
    {66, "🎂"},   // 蛋糕
    {67, "⚡"},   // 闪电
    {68, "💣"},   // 炸弹 (reserved)
    {69, "🔪"},   // 刀
    {70, "⚽"},   // 足球 (reserved)
    {71, "🐞"},   // 瓢虫 (reserved)
    {72, "💩"},   // 便便
    {73, "🌙"},   // 月亮 (reserved)
    {74, "☀️"},   // 太阳
    {75, "🎁"},   // 礼物
    {76, "🤗"},   // 拥抱
    {77, "👍"},   // 强
    {78, "👎"},   // 弱
    {79, "🤝"},   // 握手
    {80, "✌️"},   // 胜利 (reserved)
    {81, "✊"},   // 抱拳 (reserved)
    {82, "🤟"},   // 勾引 (reserved)
    {83, "✊"},   // 拳头 (reserved)
    {84, "🤙"},   // 差劲 (reserved)
    {85, "🤟"},   // 爱你
    {86, "🚫"},   // NO
    {87, "👌"},   // OK (reserved)
    {88, "😘"},   // 亲亲 (reserved)
    {89, "💃"},   // 跳跳 (reserved)
    {90, "😱"},   // 发抖 (reserved)
    {91, "😤"},   // 怄火 (reserved)
    {92, "🔄"},   // 转圈 (reserved)
    {93, "🙇"},   // 磕头 (reserved)
    {94, "🤸"},   // 回头 (reserved)
    {95, "💃"},   // 跳绳 (reserved)
    {96, "👋"},   // 挥手
    {97, "🤩"},   // 激动
    {98, "💃"},   // 街舞
    {99, "😘"},   // 献吻

    // Additional reactions (100-150)
    {100, "🌹"},  // 玫瑰 (左边)
    {101, "🌹"},  // 玫瑰 (右边)
    {102, "😍"},  // 色色
    {103, "😭"},  // 嚎哭
    {104, "🙂"},  // 笑脸
    {105, "😊"},  // 笑
    {106, "😄"},  // 大笑
    {107, "😍"},  // 花心
    {108, "😓"},  // 汗
    {109, "😅"},  // 呃
    {110, "😤"},  // 气
    {111, "🤑"},  // 发
    {112, "🎉"},  // 庆祝
    {113, "🎤"},  // 唱歌
    {114, "🙏"},  // 祈祷
    {115, "💪"},  // 加油
    {116, "🤙"},  // 打电话
    {117, "🏃"},  // 跑
    {118, "🤺"},  // 击剑
    {119, "😇"},  // 天使
    {120, "💊"},  // 吃药
    {121, "💀"},  // 骷髅
    {122, "🙏"},  // 合十
    {123, "🔫"},  // 手枪
    {124, "🎈"},  // 气球
    {125, "🏮"},  // 灯笼
    {126, "🎉"},  // 鞭炮
    {127, "🍭"},  // 棒棒糖
    {128, "🍼"},  // 奶瓶
    {129, "✈️"},  // 飞机
    {130, "🚗"},  // 汽车
    {131, "🚂"},  // 火车
    {132, "⛵"},  // 帆船
    {133, "🏠"},  // 房子
    {134, "🌈"},  // 彩虹
    {135, "☁️"},  // 云
    {136, "🌧️"},  // 下雨
    {137, "❄️"},  // 雪花
    {138, "⭐"},  // 星星
    {139, "🌕"},  // 满月
    {140, "🍎"},  // 苹果
    {141, "🍇"},  // 葡萄
    {142, "🍊"},  // 橙子
    {143, "🍋"},  // 柠檬
    {144, "🍉"},  // 西瓜
    {145, "🍓"},  // 草莓
    {146, "🍑"},  // 桃子
    {147, "🍌"},  // 香蕉
    {148, "🥕"},  // 胡萝卜
    {149, "🌽"},  // 玉米
    {150, "🍄"},  // 蘑菇

    // Newer emojis (151-221+)
    {151, "🥗"},  // 沙拉
    {152, "🍕"},  // 披萨
    {153, "🍔"},  // 汉堡
    {154, "🍟"},  // 薯条
    {155, "🌭"},  // 热狗
    {156, "🍿"},  // 爆米花
    {157, "🍩"},  // 甜甜圈
    {158, "🍪"},  // 饼干
    {159, "🍫"},  // 巧克力
    {160, "🍬"},  // 糖果
    {161, "🍰"},  // 蛋糕
    {162, "🍦"},  // 冰淇淋
    {163, "🧁"},  // 纸杯蛋糕
    {164, "🍵"},  // 茶
    {165, "🥛"},  // 牛奶
    {166, "🍷"},  // 红酒
    {167, "🍸"},  // 鸡尾酒
    {168, "🥤"},  // 饮料
    {169, "🧃"},  // 果汁
    {170, "🐱"},  // 猫
    {171, "🐶"},  // 狗
    {172, "🐭"},  // 老鼠
    {173, "🐹"},  // 仓鼠
    {174, "🐰"},  // 兔子
    {175, "🦊"},  // 狐狸
    {176, "🐻"},  // 熊
    {177, "🐼"},  // 熊猫
    {178, "🐨"},  // 考拉
    {179, "🐯"},  // 老虎
    {180, "🦁"},  // 狮子
    {181, "🐮"},  // 牛
    {182, "🐽"},  // 猪鼻
    {183, "🐸"},  // 青蛙
    {184, "🐵"},  // 猴子
    {185, "🐔"},  // 鸡
    {186, "🐧"},  // 企鹅
    {187, "🐦"},  // 鸟
    {188, "🦆"},  // 鸭子
    {189, "🦅"},  // 老鹰
    {190, "🦉"},  // 猫头鹰
    {191, "🦇"},  // 蝙蝠
    {192, "🐺"},  // 狼
    {193, "🐗"},  // 野猪
    {194, "🐴"},  // 马
    {195, "🦄"},  // 独角兽
    {196, "🐝"},  // 蜜蜂
    {197, "🐛"},  // 毛毛虫
    {198, "🦋"},  // 蝴蝶
    {199, "🐌"},  // 蜗牛
    {200, "🐚"},  // 贝壳
    {201, "🐠"},  // 鱼
    {202, "🐬"},  // 海豚
    {203, "🐳"},  // 鲸鱼
    {204, "🦈"},  // 鲨鱼
    {205, "🐙"},  // 章鱼
    {206, "🦀"},  // 螃蟹
    {207, "🦐"},  // 虾
    {208, "🦑"},  // 鱿鱼
    {209, "🥺"},  // 可怜
    {210, "😃"},  // 笑脸
    {211, "🙃"},  // 翻转笑脸
    {212, "🤣"},  // 笑哭
    {213, "😜"},  // 眨眼吐舌
    {214, "🥳"},  // 派对
    {215, "🤩"},  // 星星眼
    {216, "🥰"},  // 爱心脸
    {217, "😇"},  // 天使
    {218, "🤠"},  // 牛仔
    {219, "🤡"},  // 小丑
    {220, "🥶"},  // 冷
    {221, "🥵"},  // 热

    // Extended IDs from official QQ documentation
    {222, "😶"},  // 无语
    {223, "🤥"},  // 说谎
    {224, "😬"},  // 鬼脸
    {225, "🤓"},  // 书呆子
    {226, "🧐"},  // 单片眼镜
    {227, "🤯"},  // 爆炸头
    {228, "🤪"},  // 疯狂
    {229, "😵‍💫"},  // 晕
    {230, "🫠"},  // 融化
    {231, "🫡"},  // 敬礼
    {232, "🫣"},  // 偷看
    {233, "🫢"},  // 捂嘴
    {234, "🫥"},  // 隐藏
    {235, "🫤"},  // 歪嘴
    {236, "🥹"},  // 感动
    {237, "🥲"},  // 笑着流泪
    {238, "🥸"},  // 伪装
    {239, "🤌"},  // 意大利手势
    {240, "🫰"},  // 爱心手势
    {241, "🫵"},  // 指向你
    {242, "🫶"},  // 心形手
    {243, "🤞"},  // 交叉手指
    {244, "🤙"},  // 打电话
    {245, "🤏"},  // 捏
    {246, "🦾"},  // 机械臂
    {247, "🦿"},  // 机械腿
    {248, "👀"},  // 眼睛
    {249, "👁️"},  // 单眼
    {250, "👃"},  // 鼻子

    // Additional commonly used IDs
    {260, "☕"},  // 咖啡
    {261, "🧧"},  // 红包
    {262, "🎆"},  // 烟花
    {263, "🎇"},  // 烟火
    {264, "🧨"},  // 鞭炮
    {265, "🪭"},  // 扇子
    {266, "🏮"},  // 红灯笼
    {267, "🎊"},  // 彩带
    {268, "🎋"},  // 许愿树
    {269, "🎍"},  // 门松
    {270, "🧧"},  // 红包
    {271, "🥮"},  // 月饼
    {272, "🥟"},  // 饺子
    {273, "🍙"},  // 饭团
    {274, "🍘"},  // 米饼
    {275, "🥠"},  // 幸运饼干
    {276, "🥡"},  // 外卖盒
    {277, "🍜"},  // 面条
    {278, "🍝"},  // 意面
    {279, "🍛"},  // 咖喱饭
    {280, "🍣"},  // 寿司
    {281, "🍱"},  // 便当
    {282, "🥧"},  // 派
    {283, "🧁"},  // 杯子蛋糕
    {284, "🍨"},  // 圣代
    {285, "🍧"},  // 刨冰
    {286, "🥤"},  // 杯装饮料
    {287, "🧋"},  // 奶茶
    {288, "🫖"},  // 茶壶
    {289, "🍶"},  // 清酒
    {290, "🍾"},  // 香槟
    {291, "🥂"},  // 碰杯
    {292, "🥃"},  // 威士忌
    {293, "🫗"},  // 倒水
    {294, "🧊"},  // 冰块
    {295, "🥄"},  // 勺子
    {296, "🍴"},  // 刀叉
    {297, "🥢"},  // 筷子

    // Gestures and hands (from official doc)
    {310, "✋"},  // 手掌
    {311, "🖐️"},  // 张开的手
    {312, "🖖"},  // 瓦肯手势
    {313, "🤚"},  // 手背
    {314, "✋"},  // 举手
    {315, "🫱"},  // 右手
    {316, "🫲"},  // 左手
    {317, "🫳"},  // 手掌向下
    {318, "🫴"},  // 手掌向上
    {319, "🤲"},  // 双手合十
    {320, "👐"},  // 张开双手
    {321, "🙌"},  // 举起双手
    {322, "👆"},  // 向上指
    {323, "👇"},  // 向下指
    {324, "👈"},  // 向左指
    {325, "👉"},  // 向右指
    {326, "🖕"},  // 中指
};
// clang-format on

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
 * @brief QQ媒体文件处理器
 *
 * 处理QQ到Telegram的媒体文件转换和处理
 */
class QQMediaProcessor {
public:
  /**
   * @brief 构造函数
   * @param db_manager 数据库管理器
   */
  explicit QQMediaProcessor(
      std::shared_ptr<storage::DatabaseManager> db_manager);

  /**
   * @brief 处理QQ消息段并转换为Telegram格式
   * @param qq_bot QQ机器人实例
   * @param telegram_bot Telegram机器人实例
   * @param segment QQ消息段
   * @param event 原始消息事件
   * @param telegram_group_id Telegram群ID
   * @param topic_id Topic ID
   * @param sender_display_name 发送者显示名称
   * @param bridge_config 桥接配置
   * @param temp_files_to_cleanup 临时文件清理列表
   * @return 转换后的Telegram消息段（可能为空表示已直接发送）
   */
  auto process_qq_media_segment(obcx::core::IBot &qq_bot,
                                obcx::core::IBot &telegram_bot,
                                const obcx::common::MessageSegment &segment,
                                const obcx::common::MessageEvent &event,
                                const std::string &telegram_group_id,
                                int64_t topic_id,
                                const std::string &sender_display_name,
                                const GroupBridgeConfig *bridge_config,
                                std::vector<std::string> &temp_files_to_cleanup)
      -> boost::asio::awaitable<std::optional<obcx::common::MessageSegment>>;

  /**
   * @brief 处理图片段（包含表情包检测和缓存）
   */
  auto process_image_segment(obcx::core::IBot &qq_bot,
                             obcx::core::IBot &telegram_bot,
                             const obcx::common::MessageSegment &segment,
                             const obcx::common::MessageEvent &event,
                             const std::string &telegram_group_id,
                             int64_t topic_id,
                             const std::string &sender_display_name,
                             const GroupBridgeConfig *bridge_config,
                             std::vector<std::string> &temp_files_to_cleanup)
      -> boost::asio::awaitable<std::optional<obcx::common::MessageSegment>>;

  /**
   * @brief 处理文件段（包含URL获取）
   */
  auto process_file_segment(obcx::core::IBot &qq_bot,
                            const obcx::common::MessageSegment &segment,
                            const obcx::common::MessageEvent &event)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理@消息段（包含用户信息同步）
   */
  auto process_at_segment(obcx::core::IBot &qq_bot,
                          const obcx::common::MessageSegment &segment,
                          const obcx::common::MessageEvent &event)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理语音段
   */
  static auto process_record_segment(
      const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理视频段
   */
  static auto process_video_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理QQ表情段
   */
  static auto process_face_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理QQ超级表情/表情包段 (mface)
   */
  static auto process_mface_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理戳一戳段
   */
  static auto process_shake_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理音乐分享段
   */
  static auto process_music_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理链接分享段
   */
  static auto process_share_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理JSON小程序段
   */
  static auto process_json_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理应用分享段
   */
  static auto process_app_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理ARK卡片段
   */
  static auto process_ark_segment(const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

  /**
   * @brief 处理小程序段
   */
  static auto process_miniapp_segment(
      const obcx::common::MessageSegment &segment)
      -> boost::asio::awaitable<obcx::common::MessageSegment>;

private:
  std::shared_ptr<storage::DatabaseManager> db_manager_;

  /**
   * @brief 检测图片是否为GIF格式
   */
  auto detect_gif_format(const std::string &url)
      -> boost::asio::awaitable<bool>;

  /**
   * @brief 检测是否为表情包
   */
  static auto is_sticker(const obcx::common::MessageSegment &segment) -> bool;

  /**
   * @brief 处理表情包缓存
   */
  auto handle_sticker_cache(obcx::core::IBot &telegram_bot,
                            const obcx::common::MessageSegment &segment,
                            const std::string &telegram_group_id,
                            int64_t topic_id,
                            const std::string &sender_display_name,
                            const GroupBridgeConfig *bridge_config)
      -> boost::asio::awaitable<bool>;

  /**
   * @brief 解析小程序JSON数据
   */
  static auto parse_miniapp_json(const std::string &json_data)
      -> MiniAppParseResult;

  /**
   * @brief 格式化小程序消息段
   */
  static auto format_miniapp_message(const MiniAppParseResult &parse_result)
      -> obcx::common::MessageSegment;

  /**
   * @brief 从JSON字符串中提取URLs
   */
  static auto extract_urls_from_json(const std::string &json_str)
      -> std::vector<std::string>;

  /**
   * @brief 将二进制数据转换为16进制字符串用于调试
   */
  static auto to_hex_string(const std::string &data, size_t max_bytes = 16)
      -> std::string;
};

} // namespace bridge::qq
