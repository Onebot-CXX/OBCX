#include <atomic>
#include <cstdint>

// Deliberately incompatible, inert loader fixture. It is built with the same
// frozen toolchain but reports schema2. Keep a host dlopen reference while the
// real loader rejects it so the counter cannot reset on unload.
namespace {
std::atomic_uint factory_calls{0};
}
extern "C" {
std::uint32_t obcx_get_actor_abi_generation() { return 2; }
void *obcx_create_actor_v2() {
  ++factory_calls;
  return nullptr;
}
void obcx_destroy_actor_v2(void *) {}
const char *obcx_get_actor_name_v2() { return "offline_legacy"; }
const char *obcx_get_actor_version_v2() { return "0.1.0"; }
const char *obcx_get_actor_contract() {
  return R"({"schema_version":2,"actor":"offline_legacy","accepted_inputs":[],"commands":[]})";
}
unsigned obcx_offline_factory_calls() { return factory_calls.load(); }
}
