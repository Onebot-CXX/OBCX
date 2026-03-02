#include "network/compression.hpp"

#include <brotli/decode.h>
#include <zlib.h>
#include <zstd.h>

#include <cctype>
#include <string>

namespace obcx::network {

namespace http = boost::beast::http;

void decompress_inplace(http::response<http::string_body> &res) {
  auto ce = res[http::field::content_encoding];
  if (ce.empty()) {
    return;
  }
  const std::string &body = res.body();
  if (body.empty()) {
    return;
  }
  std::string encoding(ce);
  for (auto &c : encoding) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }

  try {
    std::string decompressed;

    if (encoding == "gzip" || encoding == "x-gzip" || encoding == "deflate") {
      // gzip: wbits=31 (15+16), deflate: try zlib-wrapped (15) then raw (-15)
      int wbits = (encoding == "deflate") ? 15 : 15 + 16;

      auto try_zlib = [&](int wb) -> bool {
        z_stream zs{};
        if (inflateInit2(&zs, wb) != Z_OK) {
          return false;
        }
        zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(body.data()));
        zs.avail_in = static_cast<uInt>(body.size());

        std::string out;
        char buf[32768];
        int ret = Z_OK;
        do {
          zs.next_out = reinterpret_cast<Bytef *>(buf);
          zs.avail_out = sizeof(buf);
          ret = inflate(&zs, Z_NO_FLUSH);
          if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR ||
              ret == Z_MEM_ERROR) {
            inflateEnd(&zs);
            return false;
          }
          out.append(buf, sizeof(buf) - zs.avail_out);
        } while (ret != Z_STREAM_END && zs.avail_out == 0);
        inflateEnd(&zs);
        if (ret != Z_STREAM_END) {
          return false;
        }
        decompressed = std::move(out);
        return true;
      };

      if (!try_zlib(wbits)) {
        if (encoding != "deflate" || !try_zlib(-15)) {
          return;
        }
      }

    } else if (encoding == "br") {
      size_t decoded_size = body.size() * 4;
      if (decoded_size < 1024) {
        decoded_size = 1024;
      }

      BrotliDecoderResult result;
      do {
        decompressed.resize(decoded_size);
        size_t out_size = decoded_size;
        result = BrotliDecoderDecompress(
            body.size(), reinterpret_cast<const uint8_t *>(body.data()),
            &out_size, reinterpret_cast<uint8_t *>(decompressed.data()));
        if (result == BROTLI_DECODER_RESULT_SUCCESS) {
          decompressed.resize(out_size);
        } else if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
          decoded_size *= 2;
        }
      } while (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT);

      if (result != BROTLI_DECODER_RESULT_SUCCESS) {
        return;
      }

    } else if (encoding == "zstd") {
      unsigned long long const content_size =
          ZSTD_getFrameContentSize(body.data(), body.size());

      if (content_size != ZSTD_CONTENTSIZE_ERROR &&
          content_size != ZSTD_CONTENTSIZE_UNKNOWN) {
        decompressed.resize(static_cast<size_t>(content_size));
        size_t const result = ZSTD_decompress(decompressed.data(),
                                              static_cast<size_t>(content_size),
                                              body.data(), body.size());
        if (ZSTD_isError(result)) {
          return;
        }
        decompressed.resize(result);
      } else {
        // Streaming decompress for unknown content size
        ZSTD_DCtx *dctx = ZSTD_createDCtx();
        if (!dctx) {
          return;
        }
        ZSTD_inBuffer input = {body.data(), body.size(), 0};
        while (input.pos < input.size) {
          char buf[32768];
          ZSTD_outBuffer output = {buf, sizeof(buf), 0};
          size_t const ret = ZSTD_decompressStream(dctx, &output, &input);
          if (ZSTD_isError(ret)) {
            ZSTD_freeDCtx(dctx);
            return;
          }
          decompressed.append(buf, output.pos);
        }
        ZSTD_freeDCtx(dctx);
      }

    } else {
      return;
    }

    res.body() = std::move(decompressed);
    res.erase(http::field::content_encoding);
    res.prepare_payload();
  } catch (...) {
    // Decompression failed: leave body as-is
  }
}

} // namespace obcx::network
