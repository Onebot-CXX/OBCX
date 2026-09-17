#include <cstdint>

extern "C" auto obcx_get_actor_abi_generation() -> std::uint32_t { return 2; }
extern "C" auto obcx_create_actor_v2() -> void * { return nullptr; }
extern "C" void obcx_destroy_actor_v2(void *) {}
extern "C" auto obcx_get_actor_name_v2() -> const char * {
  return "activation_failure_actor";
}
extern "C" auto obcx_get_actor_version_v2() -> const char * { return "1.0.0"; }
extern "C" auto obcx_get_actor_contract() -> const char * {
  return R"({"schema_version":2,"actor":"activation_failure_actor","accepted_inputs":["obcx::tests::events::SdkSmoke"]})";
}
