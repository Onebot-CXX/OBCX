#include <cstdint>
#include <string>

#ifndef OBCX_CONTRACT_CASE
#define OBCX_CONTRACT_CASE 0
#endif

extern "C" auto obcx_get_actor_abi_generation() -> std::uint32_t { return 2; }

extern "C" void *obcx_create_actor_v2() { return nullptr; }

extern "C" void obcx_destroy_actor_v2(void *) {}

extern "C" auto obcx_get_actor_name_v2() -> const char * {
  return "contract_fixture";
}

extern "C" auto obcx_get_actor_version_v2() -> const char * { return "1.0"; }

#if OBCX_CONTRACT_CASE != 0
extern "C" auto obcx_get_actor_contract() -> const char * {
#if OBCX_CONTRACT_CASE == 1
  return nullptr;
#elif OBCX_CONTRACT_CASE == 2
  return "not-json";
#elif OBCX_CONTRACT_CASE == 3
  return R"({"schema_version":3,"actor":"contract_fixture","accepted_inputs":["test::Message"]})";
#elif OBCX_CONTRACT_CASE == 4
  return R"({"schema_version":2,"actor":"wrong_actor","accepted_inputs":["test::Message"]})";
#elif OBCX_CONTRACT_CASE == 5
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message","test::Message"]})";
#elif OBCX_CONTRACT_CASE == 6
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"outputs":[]})";
#elif OBCX_CONTRACT_CASE == 7
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test:::Message"]})";
#elif OBCX_CONTRACT_CASE == 8
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"commands":{}})";
#elif OBCX_CONTRACT_CASE == 9
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::First","test::Second"],"commands":[{"name":"ping","description":"Ping","request_type":"test::First"},{"name":"ping","description":"Again","request_type":"test::Second"}]})";
#elif OBCX_CONTRACT_CASE == 10
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::First","test::Second"],"commands":[{"name":"zeta","description":"Zeta","request_type":"test::First"},{"name":"alpha","description":"Alpha","request_type":"test::Second"}]})";
#elif OBCX_CONTRACT_CASE == 11
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"commands":[{"name":"ping","description":"Ping","request_type":"test::Message","handler":"on_ping"}]})";
#elif OBCX_CONTRACT_CASE == 12
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"commands":[{"name":"ping","description":"Ping","request_type":"test::Other"}]})";
#elif OBCX_CONTRACT_CASE == 13
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"commands":[{"name":"Ping!","description":"","request_type":"test::Message"}]})";
#elif OBCX_CONTRACT_CASE == 14
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"commands":[{"name":"ping","description":"Ping","request_type":"test::Message","matcher":{"kind":"re2","pattern":"(","mode":"full"}}]})";
#elif OBCX_CONTRACT_CASE == 15
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"commands":[{"name":"ping","description":"Ping","request_type":"test::Message","matcher":{"kind":"re2","pattern":"^ping$","mode":"full","handler":"on_ping"}}]})";
#elif OBCX_CONTRACT_CASE == 16
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"commands":[{"name":"ping","description":"Ping","request_type":"test::Message","matcher":{"kind":"std_regex","pattern":"^ping$","mode":"full"}}]})";
#elif OBCX_CONTRACT_CASE == 17
  static const auto contract = [] {
    return std::string{
               R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"commands":[{"name":"ping","description":"Ping","request_type":"test::Message","matcher":{"kind":"re2","pattern":")"} +
           std::string(4097, 'a') + R"(","mode":"full"}}]})";
  }();
  return contract.c_str();
#elif OBCX_CONTRACT_CASE == 18
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"configuration":{"bot_installation_collections":{"pairs":{"minimum_items":1,"identity":"id","bot_installations":{"target":"qq"},"validator":"run"}}}})";
#elif OBCX_CONTRACT_CASE == 19
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"configuration":{"bot_installation_collections":{"pairs":{"minimum_items":1,"identity":"target","bot_installations":{"target":"qq"}}}}})";
#elif OBCX_CONTRACT_CASE == 20
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"configuration":{"bot_installation_collections":{"pairs":{"minimum_items":1,"identity":"id","bot_installations":{"target":["qq","qq"]}}}}})";
#elif OBCX_CONTRACT_CASE == 21
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"configuration":{"bot_installation_collections":{"pairs":{"minimum_items":1,"identity":"id","bot_installations":{"target":"qq"},"alternative_group":"pair_form"}}}})";
#elif OBCX_CONTRACT_CASE == 22
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"configuration":{"bot_installation_collections":{"pairs":{"minimum_items":1,"identity":"id","bot_installations":{"target":"qq"},"unique_fields":["missing"]}}}})";
#elif OBCX_CONTRACT_CASE == 23
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"configuration":{"bot_installation_collections":{"pairs":{"minimum_items":1,"identity":"id","bot_installations":{"target":"qq"}}},"collection_identity_references":[{"source_key":"pair","target_collection":"missing","target_identity":"id"}]}})";
#elif OBCX_CONTRACT_CASE == 24
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"],"commands":[{"name":"help","description":"Reserved","request_type":"test::Message"}]})";
#else
  return R"({"schema_version":2,"actor":"contract_fixture","accepted_inputs":["test::Message"]})";
#endif
}
#endif
