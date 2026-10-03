#ifndef OBCX_INCLUDE_TELEGRAM_BOT_TRANSPORT_HPP_
#define OBCX_INCLUDE_TELEGRAM_BOT_TRANSPORT_HPP_

#include "common/message_type.hpp"
#include "core/bot/bot_component_runtime.hpp"
#include "telegram/adapter/protocol_adapter.hpp"
#include "telegram/bot/capability_ids.hpp"
#include "telegram/bot/configuration.hpp"
#include "telegram/provider_types.hpp"
#include <boost/asio/awaitable.hpp>
#include <functional>
#include <optional>
#include <variant>

namespace obcx::core {

class TelegramTransport {
public:
  using EventCallback = std::function<void(const common::Event &)>;

  TelegramTransport() = default;
  TelegramTransport(const TelegramTransport &) = delete;
  auto operator=(const TelegramTransport &) -> TelegramTransport & = delete;
  TelegramTransport(TelegramTransport &&) = delete;
  auto operator=(TelegramTransport &&) -> TelegramTransport & = delete;
  virtual ~TelegramTransport() = default;

  virtual void set_event_callback(EventCallback callback) = 0;
  [[nodiscard]] virtual auto is_connected() const -> bool = 0;
  virtual auto send_action(std::string payload, std::uint64_t echo)
      -> boost::asio::awaitable<std::string> = 0;
  virtual auto download_file(std::string file_id)
      -> boost::asio::awaitable<std::string> = 0;
  virtual auto download_file_content(std::string_view url,
                                     std::size_t maximum_bytes)
      -> boost::asio::awaitable<std::string> = 0;
  virtual auto upload_photo(std::string_view chat_id,
                            const TelegramMediaUpload &photo,
                            std::string_view caption,
                            std::optional<std::int64_t> topic_id)
      -> boost::asio::awaitable<std::string> = 0;
  virtual auto upload_media_group(
      std::string_view chat_id, const std::vector<TelegramMediaUpload> &media,
      std::string_view caption, std::optional<std::int64_t> topic_id,
      std::optional<std::string> reply_to_message_id,
      const std::vector<TelegramTextEntity> &caption_entities)
      -> boost::asio::awaitable<std::string> = 0;
};

class TelegramTransportCapability final : public TelegramTransport {
public:
  TelegramTransportCapability(
      boost::asio::io_context &io_context,
      obcx::telegram::configuration::HttpConnection config);
  ~TelegramTransportCapability() override;

  TelegramTransportCapability(const TelegramTransportCapability &) = delete;
  auto operator=(const TelegramTransportCapability &)
      -> TelegramTransportCapability & = delete;
  TelegramTransportCapability(TelegramTransportCapability &&) = delete;
  auto operator=(TelegramTransportCapability &&)
      -> TelegramTransportCapability & = delete;

  void configure(std::shared_ptr<adapter::telegram::ProtocolAdapter> protocol);
  void start();
  void stop() noexcept;
  void set_event_callback(EventCallback callback) override;
  [[nodiscard]] auto is_connected() const -> bool override;
  auto send_action(std::string payload, std::uint64_t echo)
      -> boost::asio::awaitable<std::string> override;
  auto download_file(std::string file_id)
      -> boost::asio::awaitable<std::string> override;
  auto download_file_content(std::string_view url, std::size_t maximum_bytes)
      -> boost::asio::awaitable<std::string> override;
  auto upload_photo(std::string_view chat_id, const TelegramMediaUpload &photo,
                    std::string_view caption,
                    std::optional<std::int64_t> topic_id)
      -> boost::asio::awaitable<std::string> override;
  auto upload_media_group(
      std::string_view chat_id, const std::vector<TelegramMediaUpload> &media,
      std::string_view caption, std::optional<std::int64_t> topic_id,
      std::optional<std::string> reply_to_message_id,
      const std::vector<TelegramTextEntity> &caption_entities)
      -> boost::asio::awaitable<std::string> override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class TelegramHttpTransportComponent final : public BotComponent {
public:
  TelegramHttpTransportComponent(
      boost::asio::io_context &io_context,
      obcx::telegram::configuration::HttpConnection config);

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  std::shared_ptr<TelegramTransportCapability> transport_;
};

} // namespace obcx::core

#endif
