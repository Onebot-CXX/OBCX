foreach(_required IN ITEMS OBCX_SOURCE_DIR OBCX_BUILD_DIR OBCX_BOT_SDK_KIND OBCX_CTEST_COMMAND)
  if(NOT DEFINED ${_required})
    message(FATAL_ERROR "${_required} must be specified")
  endif()
endforeach()
set(_root "${OBCX_BUILD_DIR}/bot-sdk-isolation/${OBCX_BOT_SDK_KIND}")
set(_prefix "${_root}/sdk")
file(REMOVE_RECURSE "${_root}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${OBCX_BUILD_DIR}" --prefix "${_prefix}"
                RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "fresh SDK install failed")
endif()
foreach(_platform IN ITEMS onebot11 telegram)
  if(NOT OBCX_BOT_SDK_KIND STREQUAL _platform)
    file(REMOVE_RECURSE "${_prefix}/include/obcx/${_platform}")
  endif()
endforeach()
# Bot contract consumers cannot fall back to Actor host or networking headers.
file(REMOVE_RECURSE "${_prefix}/include/obcx/core/actor"
                    "${_prefix}/include/obcx/core/infrastructure"
                    "${_prefix}/include/obcx/network")
set(_toolchain_args)
foreach(_flag IN ITEMS C_FLAGS CXX_FLAGS EXE_LINKER_FLAGS SHARED_LINKER_FLAGS)
  if(DEFINED OBCX_CONSUMER_${_flag} AND NOT OBCX_CONSUMER_${_flag} STREQUAL "")
    list(APPEND _toolchain_args "-DCMAKE_${_flag}=${OBCX_CONSUMER_${_flag}}")
  endif()
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}"
  -S "${OBCX_SOURCE_DIR}/tests/fixtures/bot_sdk_isolation" -B "${_root}/build"
  "-DCMAKE_PREFIX_PATH=${_prefix};${OBCX_DEPENDENCY_PREFIX}"
  "-DOBCX_TEST_ROOT=${OBCX_SOURCE_DIR}/tests" "-DOBCX_BOT_SDK_KIND=${OBCX_BOT_SDK_KIND}"
  -DCMAKE_BUILD_TYPE=Debug ${_toolchain_args} RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "isolated SDK configuration failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_root}/build" -j2 RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "isolated SDK compilation failed")
endif()
execute_process(COMMAND "${OBCX_CTEST_COMMAND}" --test-dir "${_root}/build" --output-on-failure
                RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "isolated SDK contracts failed")
endif()
