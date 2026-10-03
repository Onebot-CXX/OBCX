#include "common/logger.hpp"

#include <string>

// An actor-owned implementation library, invoked on a blocking worker.
auto sdk_helper_identity() -> std::string {
  OBCX_INFO("Standalone SDK helper logging probe");
  return OBCX_CURRENT_LOGGER()->name() + ":" + OBCX_ACTOR_VERSION;
}
