add_library(obcx_multiple_inheritance_actor SHARED
            fixtures/multiple_inheritance_actor.cpp)
target_link_libraries(obcx_multiple_inheritance_actor PRIVATE obcx_core)
set_target_properties(obcx_multiple_inheritance_actor
                      PROPERTIES OUTPUT_NAME multiple_inheritance_actor)

foreach(_schema IN ITEMS 999)
  add_library(obcx_schema_probe_${_schema} SHARED fixtures/schema_gate_probe_actor.cpp)
  target_compile_definitions(obcx_schema_probe_${_schema} PRIVATE OBCX_PROBE_SCHEMA=${_schema})
endforeach()

add_library(obcx_invalid_actor SHARED fixtures/invalid_actor.cpp)
set_target_properties(obcx_invalid_actor PROPERTIES OUTPUT_NAME invalid_actor)

add_library(obcx_activation_failure_actor SHARED
            fixtures/activation_failure_actor.cpp)
set_target_properties(obcx_activation_failure_actor
                      PROPERTIES OUTPUT_NAME activation_failure_actor)

add_library(obcx_test_actor_v2 SHARED fixtures/test_actor_v2.cpp)
target_link_libraries(obcx_test_actor_v2 PRIVATE obcx_core)
set_target_properties(obcx_test_actor_v2 PROPERTIES OUTPUT_NAME test_actor_v2)

add_library(obcx_reload_lifecycle_actor SHARED
            fixtures/reload_lifecycle_actor.cpp)
target_link_libraries(obcx_reload_lifecycle_actor PRIVATE obcx_core)
set_target_properties(obcx_reload_lifecycle_actor
                      PROPERTIES OUTPUT_NAME reload_lifecycle_actor)

foreach(generation IN ITEMS 1 2)
  add_library(obcx_private_dependency_v${generation} SHARED
              fixtures/private_generation_dependency.cpp)
  target_compile_definitions(
    obcx_private_dependency_v${generation}
    PRIVATE OBCX_PRIVATE_GENERATION_VALUE=${generation})
  set_target_properties(
    obcx_private_dependency_v${generation}
    PROPERTIES OUTPUT_NAME private_generation_dependency
               SOVERSION 1
               LIBRARY_OUTPUT_DIRECTORY
               "${CMAKE_CURRENT_BINARY_DIR}/private-generation/v${generation}")

  add_library(obcx_private_actor_v${generation} SHARED
              fixtures/private_dependency_actor.cpp)
  target_link_libraries(
    obcx_private_actor_v${generation}
    PRIVATE obcx_core obcx_private_dependency_v${generation})
  set_target_properties(
    obcx_private_actor_v${generation}
    PROPERTIES OUTPUT_NAME private_dependency_actor
               PREFIX ""
               BUILD_RPATH "\$ORIGIN"
               LIBRARY_OUTPUT_DIRECTORY
               "${CMAKE_CURRENT_BINARY_DIR}/private-generation/v${generation}")

  add_library(obcx_rebuilt_actor_v${generation} SHARED
              fixtures/test_actor_v2.cpp fixtures/rebuilt_actor_marker.cpp)
  target_compile_definitions(
    obcx_rebuilt_actor_v${generation}
    PRIVATE OBCX_REBUILT_ACTOR_GENERATION=${generation})
  target_link_libraries(obcx_rebuilt_actor_v${generation} PRIVATE obcx_core)
  set_target_properties(
    obcx_rebuilt_actor_v${generation}
    PROPERTIES OUTPUT_NAME generic_rebuilt_actor
               PREFIX ""
               LIBRARY_OUTPUT_DIRECTORY
               "${CMAKE_CURRENT_BINARY_DIR}/rebuilt-actor/v${generation}")
endforeach()

add_library(obcx_missing_v2_factory_actor SHARED
            fixtures/missing_v2_factory_actor.cpp)
set_target_properties(obcx_missing_v2_factory_actor
                      PROPERTIES OUTPUT_NAME missing_v2_factory_actor)

add_library(obcx_unsupported_actor SHARED fixtures/unsupported_actor.cpp)
set_target_properties(obcx_unsupported_actor PROPERTIES OUTPUT_NAME
                                                        unsupported_actor)

function(obcx_add_contract_fixture name contract_case)
  add_library(obcx_contract_${name} SHARED fixtures/contract_actor.cpp)
  target_compile_definitions(obcx_contract_${name}
                             PRIVATE OBCX_CONTRACT_CASE=${contract_case})
  set_target_properties(obcx_contract_${name} PROPERTIES OUTPUT_NAME
                                                         contract_${name})
endfunction()

obcx_add_contract_fixture(missing 0)
obcx_add_contract_fixture(null 1)
obcx_add_contract_fixture(invalid_json 2)
obcx_add_contract_fixture(unsupported_schema 3)
obcx_add_contract_fixture(name_mismatch 4)
obcx_add_contract_fixture(duplicate_input 5)
obcx_add_contract_fixture(outputs 6)
obcx_add_contract_fixture(malformed_input 7)
obcx_add_contract_fixture(commands_not_array 8)
obcx_add_contract_fixture(duplicate_command 9)
obcx_add_contract_fixture(unsorted_command 10)
obcx_add_contract_fixture(command_callable 11)
obcx_add_contract_fixture(command_unsupported_input 12)
obcx_add_contract_fixture(command_invalid_name 13)
obcx_add_contract_fixture(command_reserved_name 24)
obcx_add_contract_fixture(command_invalid_pattern 14)
obcx_add_contract_fixture(command_matcher_callable 15)
obcx_add_contract_fixture(command_matcher_kind 16)
obcx_add_contract_fixture(command_pattern_too_large 17)
obcx_add_contract_fixture(collection_callable 18)
obcx_add_contract_fixture(collection_duplicate_field 19)
obcx_add_contract_fixture(collection_duplicate_type 20)
obcx_add_contract_fixture(collection_invalid_alternative 21)
obcx_add_contract_fixture(collection_unknown_unique_field 22)
obcx_add_contract_fixture(collection_unknown_reference 23)
