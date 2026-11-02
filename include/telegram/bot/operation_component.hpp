#pragma once

#include "core/bot/bot_component_runtime.hpp"
#include "core/bot/operation_registry.hpp"
#include "core/bot/self_identity_cache.hpp"
#include "telegram/provider_types.hpp"

namespace obcx::core {

class BoundedFileReader;

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
      nlohmann::json parameters,
      const std::vector<std::pair<std::string, TelegramMediaUpload>> &files)
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
  TelegramOperationsComponent(std::string installation_id, bool include_upload,
                              boost::asio::any_io_executor executor,
                              std::chrono::milliseconds self_refresh_interval,
                              std::chrono::milliseconds action_timeout);
  TelegramOperationsComponent(std::string installation_id, bool include_upload,
                              boost::asio::any_io_executor executor,
                              std::chrono::milliseconds self_refresh_interval,
                              std::chrono::milliseconds action_timeout,
                              std::shared_ptr<BoundedFileReader> file_reader);

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  class Endpoint;
  std::shared_ptr<Endpoint> endpoint_;
  std::shared_ptr<OperationRegistry> operations_;
  std::shared_ptr<SelfIdentityCache> self_identity_;
  bool include_upload_;
};

} // namespace obcx::core
