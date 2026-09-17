#ifndef OBCX_INCLUDE_TELEGRAM_BOT_OPERATION_COMPONENT_HPP_
#define OBCX_INCLUDE_TELEGRAM_BOT_OPERATION_COMPONENT_HPP_

#include "core/bot/bot_component_runtime.hpp"
#include "core/bot/operation_registry.hpp"
#include "telegram/bot/operations.hpp"

namespace obcx::core {

class TelegramMediaUploader {
public:
  TelegramMediaUploader() = default;
  TelegramMediaUploader(const TelegramMediaUploader &) = delete;
  auto operator=(const TelegramMediaUploader &)
      -> TelegramMediaUploader & = delete;
  TelegramMediaUploader(TelegramMediaUploader &&) = delete;
  auto operator=(TelegramMediaUploader &&) -> TelegramMediaUploader & = delete;
  virtual ~TelegramMediaUploader() = default;

  virtual auto upload(
      const obcx::telegram::bot::SendTelegramPhotoUploadRequest &request)
      -> boost::asio::awaitable<std::string> = 0;
  virtual auto upload(
      const obcx::telegram::bot::SendTelegramMediaGroupUploadsRequest &request)
      -> boost::asio::awaitable<std::string> = 0;
};

class TelegramMediaUploadComponent final : public BotComponent {
public:
  TelegramMediaUploadComponent();

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  class Uploader;
  std::shared_ptr<Uploader> uploader_;
};

class TelegramOperationsComponent final : public BotComponent {
public:
  TelegramOperationsComponent(std::string installation_id, bool include_upload);

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  class Endpoint;
  std::shared_ptr<Endpoint> endpoint_;
  std::shared_ptr<OperationRegistry> operations_;
  bool include_upload_;
};

} // namespace obcx::core

#endif
