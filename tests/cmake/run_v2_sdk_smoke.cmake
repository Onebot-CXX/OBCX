if(NOT DEFINED OBCX_BUILD_DIR OR NOT DEFINED OBCX_SOURCE_DIR)
  message(FATAL_ERROR "OBCX_BUILD_DIR and OBCX_SOURCE_DIR are required")
endif()

set(_install_dir "${OBCX_BUILD_DIR}/sdk-v2-smoke-install")
set(_consumer_build "${OBCX_BUILD_DIR}/sdk-v2-smoke-consumer")
set(_consumer_install "${OBCX_BUILD_DIR}/sdk-v2-smoke-actor-install")
set(_consumer_source
    "${OBCX_SOURCE_DIR}/tests/fixtures/standalone_v2_actor")

file(REMOVE_RECURSE "${_install_dir}" "${_consumer_build}"
                    "${_consumer_install}")

set(_consumer_toolchain_args)
foreach(_flag IN ITEMS C_FLAGS CXX_FLAGS EXE_LINKER_FLAGS SHARED_LINKER_FLAGS)
  if(DEFINED OBCX_CONSUMER_${_flag} AND
     NOT OBCX_CONSUMER_${_flag} STREQUAL "")
    list(APPEND _consumer_toolchain_args
         "-DCMAKE_${_flag}=${OBCX_CONSUMER_${_flag}}")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${OBCX_BUILD_DIR}" --prefix
          "${_install_dir}"
  RESULT_VARIABLE _install_result)
if(NOT _install_result EQUAL 0)
  message(FATAL_ERROR "Failed to install the OBCX SDK for V2 smoke test")
endif()

file(GLOB_RECURSE _installed_headers
     RELATIVE "${_install_dir}"
     "${_install_dir}/include/*")
list(SORT _installed_headers)
set(_expected_headers
    include/obcx/common/config_snapshot.hpp
    include/obcx/common/bot_installation_metadata.hpp
    include/obcx/common/json_utils.hpp
    include/obcx/common/logger.hpp
    include/obcx/common/message_type.hpp
    include/obcx/core/actor/actor.hpp
    include/obcx/core/actor/actor_commands.hpp
    include/obcx/core/actor/actor_messages.hpp
    include/obcx/core/actor/actor_asio.hpp
    include/obcx/core/actor/actor_generation_lifecycle.hpp
    include/obcx/core/actor/actor_restart_constraints.hpp
    include/obcx/core/actor/actor_manager.hpp
    include/obcx/core/actor/actor_task.hpp
    include/obcx/core/actor/actor_work_stealing_executor.hpp
    include/obcx/core/infrastructure/db_manager.hpp
    include/obcx/core/actor/native_actor_scheduler.hpp
    include/obcx/core/actor/reflected_actor.hpp
    include/obcx/core/actor/blocking_executor.hpp
    include/obcx/core/bot/ids.hpp
    include/obcx/core/bot/json_codec.hpp
    include/obcx/core/bot/validation.hpp
    include/obcx/core/bot/references.hpp
    include/obcx/core/bot/operation_error.hpp
    include/obcx/core/bot/operation_result.hpp
    include/obcx/core/bot/operation_traits.hpp
    include/obcx/core/bot/messaging.hpp
    include/obcx/core/bot/gateway_codec.hpp
    include/obcx/core/bot/operation_gateway.hpp
    include/obcx/core/bot/typed_operation.hpp
    include/obcx/core/bot/messaging_client.hpp
    include/obcx/onebot11/bot/actions.hpp
    include/obcx/onebot11/bot/types.hpp
    include/obcx/onebot11/bot/operations.hpp
    include/obcx/onebot11/bot/client.hpp
    include/obcx/telegram/bot/actions.hpp
    include/obcx/telegram/bot/types.hpp
    include/obcx/telegram/bot/operations.hpp
    include/obcx/telegram/bot/client.hpp
    include/obcx/network/http_client.hpp
    include/obcx/network/proxy_http_client.hpp
    include/obcx/network/connection_config.hpp
    include/obcx/obcx/version.hpp)
list(SORT _expected_headers)
if(NOT "${_installed_headers}" STREQUAL "${_expected_headers}")
  message(FATAL_ERROR
          "Installed actor SDK header surface differs from the allowlist:\n"
          "actual=${_installed_headers}\nexpected=${_expected_headers}")
endif()

file(GLOB_RECURSE _retired_sdk_surfaces
     "${_install_dir}/*plugin*"
     "${_install_dir}/*Plugin*"
     "${_install_dir}/*asio_actor_v1*"
     "${_install_dir}/*task_scheduler*")
if(_retired_sdk_surfaces)
  message(FATAL_ERROR
          "Retired SDK surfaces were installed: ${_retired_sdk_surfaces}")
endif()
file(GLOB_RECURSE _internal_test_seams
     "${_install_dir}/*websocket_write_queue*"
     "${_install_dir}/*action_request_tracker*")
if(_internal_test_seams)
  message(FATAL_ERROR
          "Internal WebSocket test seams were installed: ${_internal_test_seams}")
endif()

foreach(_header IN LISTS _installed_headers)
  file(READ "${_install_dir}/${_header}" _header_content)
  foreach(_forbidden IN ITEMS "TaskScheduler" "get_task_scheduler"
                              "run_heavy_task")
    if(_header_content MATCHES "(^|[^A-Za-z0-9_])${_forbidden}([^A-Za-z0-9_]|$)")
      message(FATAL_ERROR
              "Retired bot scheduling API ${_forbidden} found in ${_header}")
    endif()
  endforeach()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${_consumer_source}" -B
          "${_consumer_build}" -DCMAKE_BUILD_TYPE=Debug
          "-DCMAKE_PREFIX_PATH=${_install_dir};${OBCX_DEPENDENCY_PREFIX}"
          ${_consumer_toolchain_args}
  RESULT_VARIABLE _configure_result)
if(NOT _configure_result EQUAL 0)
  message(FATAL_ERROR "Failed to configure standalone V2 actor consumer")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_consumer_build}" --target
          sdk_v2_smoke -j2
  RESULT_VARIABLE _build_result)
if(NOT _build_result EQUAL 0)
  message(FATAL_ERROR "Failed to build standalone V2 actor consumer")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${_consumer_build}" --prefix
          "${_consumer_install}"
  RESULT_VARIABLE _consumer_install_result)
if(NOT _consumer_install_result EQUAL 0)
  message(FATAL_ERROR
          "Failed to install standalone V2 actor package")
endif()

file(GLOB _installed_actors
     "${_consumer_install}/lib/obcx/actors/sdk_v2_fixture.*")
list(LENGTH _installed_actors _installed_actor_count)
if(NOT _installed_actor_count EQUAL 1)
  message(FATAL_ERROR
          "Expected one installed V2 actor, found ${_installed_actor_count}")
endif()
list(GET _installed_actors 0 _installed_actor)

execute_process(
  COMMAND "${_consumer_build}/sdk_v2_smoke" "${_installed_actor}"
  RESULT_VARIABLE _smoke_result)
if(NOT _smoke_result EQUAL 0)
  message(FATAL_ERROR "Standalone V2 actor smoke test failed: ${_smoke_result}")
endif()
