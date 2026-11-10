#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>

namespace obcx::integration {
// Shared only by the offline host and its installed-SDK consumer DSO.
// Not a production SDK service or a persistent message format.
struct Evidence {
  std::atomic_uint events{0};
  std::atomic_uint route_rejections{0};
  std::atomic_uint errors{0};
  std::atomic_uint entered_wait{0};
  std::atomic_uint cancelled_wait{0};
  std::atomic_bool block_next{false};
};
inline void require(bool value, const std::string &message) {
  if (!value)
    throw std::runtime_error(message);
}
} // namespace obcx::integration
