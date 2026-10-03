if(NOT DEFINED OBCX_EXECUTABLE OR NOT DEFINED OBCX_TEST_ACTOR)
  message(FATAL_ERROR "OBCX_EXECUTABLE and OBCX_TEST_ACTOR are required")
endif()

set(_root "${CMAKE_CURRENT_BINARY_DIR}/validate-config-cli")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_root}")

# Name-based lookup must use the active CMake binary directory, even when the
# process working directory is unrelated to the build tree.
get_filename_component(_executable_dir "${OBCX_EXECUTABLE}" DIRECTORY)
get_filename_component(_binary_dir "${_executable_dir}/../.." ABSOLUTE)
set(_actor_dir "${_binary_dir}/actors")
file(MAKE_DIRECTORY "${_actor_dir}")
get_filename_component(_test_actor_filename "${OBCX_TEST_ACTOR}" NAME)
file(COPY_FILE "${OBCX_TEST_ACTOR}"
     "${_actor_dir}/${_test_actor_filename}" ONLY_IF_DIFFERENT)

set(_valid "${_root}/valid.toml")
file(WRITE "${_valid}" "
[bots.primary]
enabled = true
surface = \"onebot11.qq\"
transport = \"http\"
[bots.primary.connection]
host = \"localhost\"
port = 3000
access_token = \"\"
use_tls = false
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_interval_ms = 1000

[actors.test_actor_v2]
library = \"test_actor_v2\"
enabled = true

[actors.test_actor_v2.config]
label = \"validation\"
target_installation = \"primary\"

[pipelines.sdk]
source = \"obcx::tests::events::SdkSmoke\"

[[pipelines.sdk.stages]]
name = \"handle\"
actor = \"test_actor_v2\"
input = \"obcx::tests::events::SdkSmoke\"
output = \"ignored::Output\"
")

execute_process(
  COMMAND "${OBCX_EXECUTABLE}" --validate-config "${_valid}"
  WORKING_DIRECTORY "${_root}"
  OUTPUT_VARIABLE _valid_stdout
  ERROR_VARIABLE _valid_stderr
  RESULT_VARIABLE _valid_result)
if(NOT _valid_result EQUAL 0 OR
   NOT _valid_stdout MATCHES "Configuration and actor contracts are valid")
  message(FATAL_ERROR
    "valid --validate-config failed: ${_valid_stdout}${_valid_stderr}")
endif()
foreach(_forbidden IN ITEMS "Runtime thread budget" "Starting bot"
                            "Registered actor runtime message ingress")
  if("${_valid_stdout}${_valid_stderr}" MATCHES "${_forbidden}")
    message(FATAL_ERROR
      "validation-only mode started runtime activity: ${_forbidden}")
  endif()
endforeach()

set(_valid_collection "${_root}/valid-collection.toml")
file(WRITE "${_valid_collection}" "
[bots.primary]
enabled = true
surface = \"onebot11.qq\"
transport = \"http\"
[bots.primary.connection]
host = \"localhost\"
port = 3000
access_token = \"\"
use_tls = false
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_interval_ms = 1000

[actors.test_actor_v2]
library = \"test_actor_v2\"
enabled = true

[actors.test_actor_v2.config]
label = \"validation\"

[[actors.test_actor_v2.config.target_installations]]
id = \"primary-route\"
target_installation = \"primary\"

[pipelines.sdk]
source = \"obcx::tests::events::SdkSmoke\"

[[pipelines.sdk.stages]]
name = \"handle\"
actor = \"test_actor_v2\"
input = \"obcx::tests::events::SdkSmoke\"
")
execute_process(
  COMMAND "${OBCX_EXECUTABLE}" --validate-config "${_valid_collection}"
  WORKING_DIRECTORY "${_root}"
  OUTPUT_VARIABLE _valid_collection_stdout
  ERROR_VARIABLE _valid_collection_stderr
  RESULT_VARIABLE _valid_collection_result)
if(NOT _valid_collection_result EQUAL 0)
  message(FATAL_ERROR
    "valid installation collection failed: ${_valid_collection_stdout}${_valid_collection_stderr}")
endif()

set(_invalid_collection "${_root}/invalid-collection.toml")
file(WRITE "${_invalid_collection}" "
[bots.primary]
enabled = true
surface = \"onebot11.qq\"
transport = \"http\"
[bots.primary.connection]
host = \"localhost\"
port = 3000
access_token = \"\"
use_tls = false
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_interval_ms = 1000

[actors.test_actor_v2]
library = \"test_actor_v2\"
enabled = true

[actors.test_actor_v2.config]
label = \"validation\"

[[actors.test_actor_v2.config.target_installations]]
id = \"duplicate\"
target_installation = \"primary\"

[[actors.test_actor_v2.config.target_installations]]
id = \"duplicate\"
target_installation = \"primary\"

[pipelines.sdk]
source = \"obcx::tests::events::SdkSmoke\"

[[pipelines.sdk.stages]]
name = \"handle\"
actor = \"test_actor_v2\"
input = \"obcx::tests::events::SdkSmoke\"
")
execute_process(
  COMMAND "${OBCX_EXECUTABLE}" --validate-config "${_invalid_collection}"
  WORKING_DIRECTORY "${_root}"
  OUTPUT_VARIABLE _invalid_collection_stdout
  ERROR_VARIABLE _invalid_collection_stderr
  RESULT_VARIABLE _invalid_collection_result)
if(_invalid_collection_result EQUAL 0 OR
   NOT "${_invalid_collection_stdout}${_invalid_collection_stderr}" MATCHES
       "reload_actor_config_invalid")
  message(FATAL_ERROR
    "invalid installation collection was not rejected: ${_invalid_collection_stdout}${_invalid_collection_stderr}")
endif()

set(_unsupported "${_root}/unsupported.toml")
file(WRITE "${_unsupported}" "
[bots.primary]
enabled = true
surface = \"onebot11.qq\"
transport = \"http\"
[bots.primary.connection]
host = \"localhost\"
port = 3000
access_token = \"\"
use_tls = false
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_interval_ms = 1000

[actors.test_actor_v2]
library = \"${OBCX_TEST_ACTOR}\"
enabled = true

[actors.test_actor_v2.config]
label = \"validation\"
target_installation = \"primary\"

[pipelines.sdk]
source = \"wrong::Input\"

[[pipelines.sdk.stages]]
name = \"handle\"
actor = \"test_actor_v2\"
input = \"wrong::Input\"
")
execute_process(
  COMMAND "${OBCX_EXECUTABLE}" --validate-config "${_unsupported}"
  WORKING_DIRECTORY "${_root}"
  OUTPUT_VARIABLE _unsupported_stdout
  ERROR_VARIABLE _unsupported_stderr
  RESULT_VARIABLE _unsupported_result)
if(_unsupported_result EQUAL 0 OR
   NOT "${_unsupported_stdout}${_unsupported_stderr}" MATCHES
       "reload_contract_invalid")
  message(FATAL_ERROR
    "unsupported input was not rejected: ${_unsupported_stdout}${_unsupported_stderr}")
endif()

set(_missing "${_root}/missing.toml")
file(WRITE "${_missing}" "
[actors.missing]
library = \"${_root}/does-not-exist.so\"
enabled = true

[pipelines.sdk]
source = \"test::Input\"

[[pipelines.sdk.stages]]
name = \"handle\"
actor = \"missing\"
input = \"test::Input\"
")
execute_process(
  COMMAND "${OBCX_EXECUTABLE}" --validate-config "${_missing}"
  WORKING_DIRECTORY "${_root}"
  OUTPUT_VARIABLE _missing_stdout
  ERROR_VARIABLE _missing_stderr
  RESULT_VARIABLE _missing_result)
if(_missing_result EQUAL 0 OR
   NOT "${_missing_stdout}${_missing_stderr}" MATCHES
       "reload_actor_unavailable")
  message(FATAL_ERROR
    "unavailable actor was not rejected: ${_missing_stdout}${_missing_stderr}")
endif()
