function(obcx_add_reflection_compile_test name case_id should_compile
         diagnostic)
  add_test(
    NAME reflection_compile.${name}
    COMMAND
      ${CMAKE_COMMAND} "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DCASE_ID=${case_id}" "-DSHOULD_COMPILE=${should_compile}"
      "-DEXPECTED_DIAGNOSTIC=${diagnostic}"
      "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}" -P
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_reflection_compile_case.cmake")
  set_tests_properties(
    reflection_compile.${name}
    PROPERTIES LABELS "unit;actor-runtime;reflection;compile-contract"
               TIMEOUT 60)
endfunction()

obcx_add_reflection_compile_test(valid_sync_async 0 TRUE "")
obcx_add_reflection_compile_test(no_handler 1 FALSE
                                 OBCX_REFLECTED_ACTOR_NO_HANDLER)
obcx_add_reflection_compile_test(non_public 2 FALSE
                                 OBCX_REFLECTED_ACTOR_NON_PUBLIC_HANDLER)
obcx_add_reflection_compile_test(wrong_arity 3 FALSE
                                 OBCX_REFLECTED_ACTOR_WRONG_ARITY)
obcx_add_reflection_compile_test(wrong_cvref 4 FALSE
                                 OBCX_REFLECTED_ACTOR_MESSAGE_NOT_CONST)
obcx_add_reflection_compile_test(wrong_envelope 5 FALSE
                                 OBCX_REFLECTED_ACTOR_WRONG_ENVELOPE_PARAMETER)
obcx_add_reflection_compile_test(wrong_context 6 FALSE
                                 OBCX_REFLECTED_ACTOR_WRONG_CONTEXT_PARAMETER)
obcx_add_reflection_compile_test(bad_return 7 FALSE
                                 OBCX_REFLECTED_ACTOR_BAD_RETURN)
obcx_add_reflection_compile_test(duplicate_input 8 FALSE
                                 OBCX_REFLECTED_ACTOR_DUPLICATE_INPUT)
obcx_add_reflection_compile_test(
  missing_decode 9 FALSE OBCX_REFLECTED_ACTOR_MISSING_JSON_DESERIALIZATION)
obcx_add_reflection_compile_test(local_message 10 FALSE
                                 OBCX_REFLECTED_MESSAGE_LOCAL_TYPE)
obcx_add_reflection_compile_test(anonymous_namespace 11 FALSE
                                 OBCX_REFLECTED_MESSAGE_UNSTABLE_IDENTITY)
obcx_add_reflection_compile_test(unnamed_message 12 FALSE
                                 OBCX_REFLECTED_MESSAGE_UNSTABLE_IDENTITY)
obcx_add_reflection_compile_test(
  missing_encode 13 FALSE OBCX_REFLECTED_ACTOR_MISSING_JSON_SERIALIZATION)
obcx_add_reflection_compile_test(valid_command_contract 14 TRUE "")
obcx_add_reflection_compile_test(missing_command_input 15 FALSE
                                 OBCX_COMMAND_REQUEST_INPUT_MISSING)
obcx_add_reflection_compile_test(duplicate_command_name 16 FALSE
                                 OBCX_COMMAND_DUPLICATE_NAME)
obcx_add_reflection_compile_test(invalid_command_message 17 FALSE
                                 OBCX_COMMAND_REQUEST_TYPE_REQUIRED)
obcx_add_reflection_compile_test(empty_command_pattern 18 FALSE
                                 OBCX_COMMAND_MATCHER_PATTERN_EMPTY)
obcx_add_reflection_compile_test(invalid_pattern_command_name 19 FALSE
                                 OBCX_COMMAND_INVALID_NAME)
obcx_add_reflection_compile_test(reserved_command_name 20 FALSE
                                 OBCX_COMMAND_RESERVED_NAME)
