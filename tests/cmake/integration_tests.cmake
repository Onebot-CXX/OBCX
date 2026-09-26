# Evidence for the existing installed-SDK smoke: only trusted SDK provider
# targets, never package source/include directories or arbitrary Nix roots.
set(_sdk_queue obcx::obcx_core)
set(_sdk_seen)
set(_sdk_targets "[]")
while(_sdk_queue)
  list(POP_FRONT _sdk_queue _target)
  _obcx_canonical(_target "${_target}")
  if(_target IN_LIST _sdk_seen)
    continue()
  endif()
  list(APPEND _sdk_seen "${_target}")
  _obcx_target_record(_record "${_target}")
  string(JSON _count LENGTH "${_sdk_targets}")
  string(JSON _sdk_targets SET "${_sdk_targets}" ${_count} "${_record}")
  string(REGEX REPLACE "\\$<[A-Za-z0-9_]+:" "" _references "${_record}")
  string(REGEX MATCHALL "[A-Za-z_][A-Za-z0-9_.:+-]*" _tokens "${_references}")
  foreach(_token IN LISTS _tokens)
    if(TARGET "${_token}")
      list(APPEND _sdk_queue "${_token}")
    endif()
  endforeach()
endwhile()
file(WRITE "${CMAKE_BINARY_DIR}/sdk-smoke-provider-targets.json" "${_sdk_targets}\n")
get_property(_sdk_graph GLOBAL PROPERTY OBCX_PACKAGE_GRAPH)
string(JSON _sdk_platform GET "${_sdk_graph}" lock platform)

add_test(
  NAME actor_sdk_v2_smoke
  COMMAND
    ${CMAKE_COMMAND} -DOBCX_BUILD_DIR=${CMAKE_BINARY_DIR}
    -DOBCX_SOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DOBCX_SDK_VERSION=${PROJECT_VERSION}
    -DOBCX_PACKAGE_PLATFORM=${_sdk_platform}
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
