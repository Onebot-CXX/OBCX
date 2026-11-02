#pragma once

#include "onebot11/bot/standard_media_queries.hpp"
#include "standard_query_codec.hpp"

namespace obcx::onebot11::bot::standard::detail {

// Pure, unregistered codecs. Caller owns origin/disposition, transport and
// physical CallLimits; locators never authorize acquisition.
auto encode_get_image(const GetImageParameters &,
                      const obcx::bot::BotInstallationRef &expected,
                      const QueryNativeCodec &, const ValueLimits &)
    -> QueryJson;
auto encode_get_record(const GetRecordParameters &,
                       const obcx::bot::BotInstallationRef &expected,
                       const QueryNativeCodec &, const ValueLimits &)
    -> QueryJson;
auto decode_get_image_data(const QueryJson &, const ValueLimits &)
    -> ImageFileData;
auto decode_get_record_data(const QueryJson &, RecordFormat requested,
                            const ValueLimits &) -> ConvertedRecordData;

} // namespace obcx::onebot11::bot::standard::detail
