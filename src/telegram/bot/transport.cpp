#include "telegram/bot/transport.hpp"
#include "telegram/network/connection_manager.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace obcx::core {
namespace {

auto proxy_type_id(const obcx::telegram::configuration::ProxyType type)
    -> std::string {
  switch (type) {
  case obcx::telegram::configuration::ProxyType::Http:
    return "http";
  case obcx::telegram::configuration::ProxyType::Https:
    return "https";
  case obcx::telegram::configuration::ProxyType::Socks5:
    return "socks5";
  }
  throw BotComponentRuntimeError("unsupported typed proxy type");
}

auto legacy_config(const obcx::telegram::configuration::HttpConnection &typed)
    -> common::ConnectionConfig {
  common::ConnectionConfig config;
  config.host = typed.host;
  config.port = typed.port;
  config.access_token = typed.access_token;
  config.use_ssl = typed.use_tls;
  config.connect_timeout = typed.connect_timeout;
  config.action_timeout = typed.action_timeout;
  config.poll_timeout = typed.poll_timeout;
  config.poll_force_close = typed.poll_force_close;
  config.poll_retry_interval = typed.poll_retry_interval;
  if (typed.proxy) {
    config.proxy_host = typed.proxy->host;
    config.proxy_port = typed.proxy->port;
    config.proxy_type = proxy_type_id(typed.proxy->type);
    config.proxy_username = typed.proxy->username;
    config.proxy_password = typed.proxy->password;
  }
  return config;
}

} // namespace

class TelegramTransportCapability::Impl {
public:
  Impl(boost::asio::io_context &io_context,
       obcx::telegram::configuration::HttpConnection config)
      : io_context(&io_context), config(std::move(config)) {}

  boost::asio::io_context *io_context;
  obcx::telegram::configuration::HttpConnection config;
  std::shared_ptr<adapter::telegram::ProtocolAdapter> protocol;
  std::unique_ptr<network::TelegramConnectionManager> manager;
  EventCallback event_callback;
  bool running{};
};

TelegramTransportCapability::TelegramTransportCapability(
    boost::asio::io_context &io_context,
    obcx::telegram::configuration::HttpConnection config)
    : impl_(std::make_unique<Impl>(io_context, std::move(config))) {}

TelegramTransportCapability::~TelegramTransportCapability() { stop(); }

void TelegramTransportCapability::configure(
    std::shared_ptr<adapter::telegram::ProtocolAdapter> protocol) {
  if (protocol == nullptr || impl_->manager != nullptr) {
    throw BotComponentRuntimeError(
        "Telegram transport can only be configured once with a protocol");
  }
  impl_->protocol = std::move(protocol);
  impl_->manager = std::make_unique<network::TelegramConnectionManager>(
      *impl_->io_context, *impl_->protocol);
  if (impl_->event_callback) {
    impl_->manager->set_event_callback(impl_->event_callback);
  }
}

void TelegramTransportCapability::start() {
  if (impl_->running) {
    return;
  }
  if (impl_->manager == nullptr) {
    throw BotComponentRuntimeError("Telegram transport is not configured");
  }
  impl_->manager->connect(legacy_config(impl_->config));
  impl_->running = true;
}

void TelegramTransportCapability::stop() noexcept {
  try {
    if (impl_->manager != nullptr) {
      impl_->manager->disconnect();
    }
  } catch (...) { // NOLINT(bugprone-empty-catch)
  }
  impl_->running = false;
}

void TelegramTransportCapability::set_event_callback(EventCallback callback) {
  impl_->event_callback = std::move(callback);
  if (impl_->manager != nullptr) {
    impl_->manager->set_event_callback(impl_->event_callback);
  }
}

auto TelegramTransportCapability::is_connected() const -> bool {
  return impl_->manager != nullptr && impl_->manager->is_connected();
}

auto TelegramTransportCapability::send_action(std::string payload,
                                              const std::uint64_t echo)
    -> boost::asio::awaitable<std::string> {
  if (impl_->manager == nullptr) {
    throw BotComponentRuntimeError("Telegram transport is not configured");
  }
  co_return co_await impl_->manager->send_action_and_wait_async(
      std::move(payload), echo);
}

auto TelegramTransportCapability::download_file(std::string file_id)
    -> boost::asio::awaitable<std::string> {
  if (impl_->manager == nullptr) {
    throw BotComponentRuntimeError("Telegram transport is not configured");
  }
  co_return co_await impl_->manager->download_file(std::move(file_id));
}

auto TelegramTransportCapability::download_file_content(
    const std::string_view url, const std::size_t maximum_bytes)
    -> boost::asio::awaitable<std::string> {
  if (impl_->manager == nullptr) {
    throw BotComponentRuntimeError("Telegram transport is not configured");
  }
  co_return co_await impl_->manager->download_file_content(url, maximum_bytes);
}

auto TelegramTransportCapability::upload_photo(
    const std::string_view chat_id, const TelegramMediaUpload &photo,
    const std::string_view caption, const std::optional<std::int64_t> topic_id)
    -> boost::asio::awaitable<std::string> {
  if (impl_->manager == nullptr) {
    throw BotComponentRuntimeError("Telegram transport is not configured");
  }
  co_return co_await impl_->manager->upload_photo_multipart(
      chat_id, photo.data, photo.filename, photo.mime_type, caption, topic_id);
}

auto TelegramTransportCapability::upload_media_group(
    const std::string_view chat_id,
    const std::vector<TelegramMediaUpload> &media,
    const std::string_view caption, const std::optional<std::int64_t> topic_id,
    std::optional<std::string> reply_to_message_id,
    const std::vector<TelegramTextEntity> &caption_entities)
    -> boost::asio::awaitable<std::string> {
  if (impl_->manager == nullptr) {
    throw BotComponentRuntimeError("Telegram transport is not configured");
  }
  co_return co_await impl_->manager->upload_media_group_multipart_with_entities(
      chat_id, media, caption, topic_id, std::move(reply_to_message_id),
      caption_entities);
}

TelegramHttpTransportComponent::TelegramHttpTransportComponent(
    boost::asio::io_context &io_context,
    obcx::telegram::configuration::HttpConnection config)
    : transport_(std::make_shared<TelegramTransportCapability>(
          io_context, std::move(config))) {}

auto TelegramHttpTransportComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"telegram.transport.http"},
      .provides = {CapabilityId{
          std::string{obcx::telegram::bot::capability_ids::transport}}},
      .required = {CapabilityId{
          std::string{obcx::telegram::bot::capability_ids::protocol}}},
  };
}

void TelegramHttpTransportComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install<TelegramTransport>(
      ComponentId{"telegram.transport.http"},
      CapabilityId{std::string{obcx::telegram::bot::capability_ids::transport}},
      transport_);
}

void TelegramHttpTransportComponent::prepare(
    const CapabilityRegistry &registry) {
  transport_->configure(
      registry.get<adapter::telegram::ProtocolAdapter>(CapabilityId{
          std::string{obcx::telegram::bot::capability_ids::protocol}}));
}

void TelegramHttpTransportComponent::start() { transport_->start(); }
void TelegramHttpTransportComponent::stop() { transport_->stop(); }

} // namespace obcx::core
