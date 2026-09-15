add_test(
  NAME actor_sdk_v2_smoke
  COMMAND
    ${CMAKE_COMMAND} -DOBCX_BUILD_DIR=${CMAKE_BINARY_DIR}
    -DOBCX_SOURCE_DIR=${CMAKE_SOURCE_DIR}
    "-DOBCX_DEPENDENCY_PREFIX=${CMAKE_PREFIX_PATH}"
    "-DOBCX_CONSUMER_C_FLAGS=${CMAKE_C_FLAGS}"
    "-DOBCX_CONSUMER_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
    "-DOBCX_CONSUMER_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
    "-DOBCX_CONSUMER_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}" -P
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_v2_sdk_smoke.cmake)
set_tests_properties(actor_sdk_v2_smoke PROPERTIES LABELS
                                                   "contract;installed-sdk")

foreach(_kind IN ITEMS common onebot11 telegram)
  add_test(NAME bot_sdk_${_kind}_isolation
    COMMAND ${CMAKE_COMMAND}
      -DOBCX_SOURCE_DIR=${CMAKE_SOURCE_DIR} -DOBCX_BUILD_DIR=${CMAKE_BINARY_DIR}
      -DOBCX_BOT_SDK_KIND=${_kind} -DOBCX_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}
      "-DOBCX_DEPENDENCY_PREFIX=${CMAKE_PREFIX_PATH}"
      "-DOBCX_CONSUMER_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
      "-DOBCX_CONSUMER_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_bot_sdk_isolation.cmake)
  set_tests_properties(bot_sdk_${_kind}_isolation PROPERTIES
    LABELS "contract;installed-sdk;isolation" TIMEOUT 300)
endforeach()

add_test(
  NAME validate_config_cli
  COMMAND
    ${CMAKE_COMMAND} -DOBCX_EXECUTABLE=$<TARGET_FILE:obcx>
    -DOBCX_TEST_ACTOR=$<TARGET_FILE:obcx_test_actor_v2> -P
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_validate_config_cli.cmake)
set_tests_properties(validate_config_cli
                     PROPERTIES LABELS "full;integration;actor-runtime;cli")
