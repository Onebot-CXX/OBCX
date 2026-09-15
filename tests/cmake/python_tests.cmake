function(obcx_add_python_unittest name labels)
  add_test(NAME ${name} COMMAND ${Python3_EXECUTABLE} -m unittest -v
                                ${CMAKE_CURRENT_SOURCE_DIR}/python/${name}.py)
  set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                                          LABELS "${labels}")
endfunction()

obcx_add_python_unittest(actor_metadata_test "contract;actor-package;metadata")
obcx_add_python_unittest(actor_package_manifest_test
                         "contract;actor-package;metadata")
obcx_add_python_unittest(actor_vcpkg_manifest_test
                         "contract;actor-package;metadata;packaging")
obcx_add_python_unittest(actor_release_tools_test
                         "contract;actor-package;release;deployment")

obcx_add_python_unittest(bot_platform_modularity_test
                         "architecture;bot-runtime;isolation")
