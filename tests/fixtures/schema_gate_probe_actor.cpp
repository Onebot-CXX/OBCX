// Independent ABI-2 metadata probe, intentionally not linked to the new SDK.
// Marker writes remain observable even when a staged DSO is unloaded on
// failure.
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#ifndef OBCX_PROBE_SCHEMA
#error "OBCX_PROBE_SCHEMA must be explicitly selected"
#endif
namespace {
void touched(const char *phase) {
  if (const auto *path = std::getenv("OBCX_SCHEMA_GATE_PROBE_MARKER")) {
    if (auto *file = std::fopen(path, "a")) {
      std::fputs(phase, file);
      std::fclose(file);
    }
  }
}
struct PreparationResult {
  std::uint32_t status;
  const char *message;
};
} // namespace
extern "C" auto obcx_get_actor_abi_generation() -> std::uint32_t { return 2; }
extern "C" auto obcx_get_actor_name_v2() -> const char * {
  return "test_actor_v2";
}
extern "C" auto obcx_get_actor_version_v2() -> const char * { return "1.0.0"; }
extern "C" auto obcx_get_actor_contract() -> const char * {
#if OBCX_PROBE_SCHEMA == 1
  return R"({"schema_version":1,"actor":"test_actor_v2","accepted_inputs":["obcx::tests::events::SdkSmoke"]})";
#elif OBCX_PROBE_SCHEMA == 999
  return R"({"schema_version":999,"actor":"test_actor_v2","accepted_inputs":["obcx::tests::events::SdkSmoke"]})";
#else
#error "unsupported fixture selection"
#endif
}
extern "C" auto obcx_create_actor_v2() -> void * {
  touched("factory\n");
  return nullptr;
}
extern "C" void obcx_destroy_actor_v2(void *) {}
extern "C" auto obcx_prepare_actor_generation_v2(void *, void *)
    -> PreparationResult {
  touched("preparation\n");
  return {1, "must not run"};
}
