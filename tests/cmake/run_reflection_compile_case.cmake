foreach(required CXX_COMPILER CASE_ID SHOULD_COMPILE SOURCE_ROOT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "Missing required argument: ${required}")
  endif()
endforeach()

execute_process(
  COMMAND
    "${CXX_COMPILER}" -std=c++26 -freflection -fsyntax-only
    "-DOBCX_CASE=${CASE_ID}" "-I${SOURCE_ROOT}/include"
    "${SOURCE_ROOT}/tests/actor/reflected_actor_cases.cpp"
  RESULT_VARIABLE compile_result
  OUTPUT_VARIABLE compile_stdout
  ERROR_VARIABLE compile_stderr)

set(compile_output "${compile_stdout}\n${compile_stderr}")
if(SHOULD_COMPILE)
  if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR
            "Reflection compile case ${CASE_ID} failed:\n${compile_output}")
  endif()
elseif(compile_result EQUAL 0)
  message(FATAL_ERROR
          "Reflection compile case ${CASE_ID} unexpectedly compiled")
else()
  string(FIND "${compile_output}" "${EXPECTED_DIAGNOSTIC}"
              diagnostic_position)
  if(diagnostic_position EQUAL -1)
    message(
      FATAL_ERROR
        "Reflection compile case ${CASE_ID} missed ${EXPECTED_DIAGNOSTIC}:\n${compile_output}"
    )
  endif()
endif()
