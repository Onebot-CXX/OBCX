#pragma once
#include "core/bot/platform_catalog.hpp"
#include <atomic>
#include <mutex>

namespace obcx::tests::echo {
struct Observations {
  std::atomic_uint parses{}, constructions{}, prepares{}, starts{}, calls{};
  std::mutex mutex;
  std::vector<std::string> stopped;
};
void register_module(core::BotPlatformCatalog &catalog,
                     std::shared_ptr<Observations> observations);
} // namespace obcx::tests::echo
