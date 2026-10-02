#include "common/logger.hpp"

// Compiled twice with separate generated package identities, one with trace
// source decoration enabled. The same call site must retain its source owner.
extern "C" void OBCX_LOG_PROBE() {
  OBCX_INFO("actor ownership probe");
  OBCX_DEBUG("actor debug probe");
}
