#pragma once
#include "core/bot/bot_queries.hpp"
#include "core/bot/send_gateway_codec.hpp"
#include "telegram/bot/bot_operations.hpp"
#include "telegram/bot/operation_component.hpp"
#include "telegram/bot/transport.hpp"
namespace obcx::core {
class ConfirmedAuthorEvidenceCollector;
}
namespace obcx::telegram::bot {
struct NativeText {
  std::string text;
  obcx::bot::Json entities;
};
NativeText native_text(const obcx::bot::RichText &, std::size_t maximum_units);
NativeText native_formatted_text(const FormattedText &,
                                 std::size_t maximum_units);
obcx::bot::ConversationCapabilities bot_capabilities(
    const obcx::bot::ConversationRef &, bool upload);
obcx::bot::ConversationCapabilities bot_capabilities(
    const obcx::bot::ConversationRef &, bool upload, bool file_read);
boost::asio::awaitable<obcx::bot::SendOutcome> send_bot_message(
    const obcx::bot::SendMessageRequest &, core::TelegramTransport &,
    core::TelegramMediaUploader *, std::chrono::milliseconds budget);
boost::asio::awaitable<obcx::bot::SendOutcome> send_bot_message(
    const obcx::bot::SendMessageRequest &, core::TelegramTransport &,
    core::TelegramMediaUploader *, std::shared_ptr<core::BoundedFileReader>,
    std::chrono::milliseconds budget);
boost::asio::awaitable<obcx::bot::SendOutcome> send_sticker(
    const SendStickerRequest &, core::TelegramTransport &,
    std::chrono::milliseconds budget);
// Borrowed only through the joined invocation; null preserves proof-free calls.
boost::asio::awaitable<obcx::bot::SendOutcome> send_bot_message(
    const obcx::bot::SendMessageRequest &, core::TelegramTransport &,
    core::TelegramMediaUploader *, std::shared_ptr<core::BoundedFileReader>,
    std::chrono::milliseconds budget, core::ConfirmedAuthorEvidenceCollector *);
boost::asio::awaitable<obcx::bot::SendOutcome> send_sticker(
    const SendStickerRequest &, core::TelegramTransport &,
    std::chrono::milliseconds budget, core::ConfirmedAuthorEvidenceCollector *);
} // namespace obcx::telegram::bot
