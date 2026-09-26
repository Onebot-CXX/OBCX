#include "telegram/bot/operations.hpp"

#include <gtest/gtest.h>

namespace {
namespace telegram = obcx::telegram::bot;
using obcx::bot::GatewayCodec;
using obcx::bot::Json;
using Upload = telegram::SendTelegramMediaGroupUploadsRequest;

auto upload(const std::size_t bytes_per_item) -> Upload {
  return {.target = {.installation = {.installation_id = "tg-fixture",
                                      .surface = telegram::surface},
                     .native_group_id = "-1001"},
          .media = {{.type = "photo",
                     .filename = "a.jpg",
                     .mime_type = "image/jpeg",
                     .bytes = std::vector<std::uint8_t>(bytes_per_item, 127)},
                    {.type = "photo",
                     .filename = "b.jpg",
                     .mime_type = "image/jpeg",
                     .bytes = std::vector<std::uint8_t>(bytes_per_item, 255)}},
          .maximum_bytes = 2U * bytes_per_item};
}

TEST(BotGatewayMediaTest, FourMiBUploadTransfersBuffersWithoutNumericArrays) {
  constexpr std::size_t bytes = 2U * 1024U * 1024U;
  auto request = upload(bytes);
  const auto *first = request.media[0].bytes.data();
  const auto *second = request.media[1].bytes.data();
  const auto target = request.target;
  auto payload = GatewayCodec<Upload>::encode(request);
  ASSERT_TRUE(payload.at("media").at(0).at("bytes").is_binary());
  ASSERT_TRUE(payload.at("media").at(1).at("bytes").is_binary());
  EXPECT_EQ(payload.at("media").at(0).at("bytes").get_binary().data(), first);
  EXPECT_EQ(payload.at("media").at(1).at("bytes").get_binary().data(), second);
  EXPECT_TRUE(request.media[0].bytes.empty());
  EXPECT_TRUE(request.media[1].bytes.empty());
  // Metadata needed for result validation is not consumed by byte encoding.
  EXPECT_EQ(request.target, target);
  EXPECT_EQ(request.media.size(), 2U);
  auto decoded = GatewayCodec<Upload>::decode(std::move(payload));
  EXPECT_EQ(decoded.media[0].bytes.data(), first);
  EXPECT_EQ(decoded.media[1].bytes.data(), second);
  EXPECT_EQ(decoded.media[0].bytes.size(), bytes);
  EXPECT_EQ(decoded.media[1].bytes.size(), bytes);
  EXPECT_EQ(decoded.media[0].bytes.front(), 127);
  EXPECT_EQ(decoded.media[1].bytes.back(), 255);
  EXPECT_EQ(decoded.target, target);
}

TEST(BotGatewayMediaTest, RejectsNumericArraysEmptyBinaryAndRequestOverflow) {
  for (const auto &invalid_bytes :
       {Json::array({1, 2}), Json("not bytes"),
        Json::binary(std::vector<std::uint8_t>{}),
        Json::binary(std::vector<std::uint8_t>{1}, 7)}) {
    auto request = upload(2);
    auto payload = GatewayCodec<Upload>::encode(request);
    payload["media"][0]["bytes"] = invalid_bytes;
    EXPECT_THROW((void)GatewayCodec<Upload>::decode(std::move(payload)),
                 std::invalid_argument);
  }
  auto request = upload(2);
  auto payload = GatewayCodec<Upload>::encode(request);
  payload["maximum_bytes"] = std::size_t{3};
  EXPECT_THROW((void)GatewayCodec<Upload>::decode(std::move(payload)),
               std::invalid_argument);
}

TEST(BotGatewayMediaTest, FileBinaryIsTransferredButPublicJsonStaysAnArray) {
  telegram::FetchedTelegramFile file{
      .installation = {.installation_id = "tg-fixture",
                       .surface = telegram::surface},
      .file = {.file_id = "file-id", .file_type = "photo"},
      .bytes = {0, 127, 128, 255}};
  const auto public_json = Json(file);
  ASSERT_TRUE(public_json.at("bytes").is_array());
  const auto *data = file.bytes.data();
  auto payload = GatewayCodec<telegram::FetchedTelegramFile>::encode(file);
  ASSERT_TRUE(payload.at("bytes").is_binary());
  EXPECT_EQ(payload.at("bytes").get_binary().data(), data);
  const auto decoded =
      GatewayCodec<telegram::FetchedTelegramFile>::decode(std::move(payload));
  EXPECT_EQ(decoded.bytes.data(), data);
  EXPECT_EQ(Json(decoded), public_json);
  EXPECT_THROW(
      (void)GatewayCodec<telegram::FetchedTelegramFile>::decode(public_json),
      std::invalid_argument);
}

} // namespace
