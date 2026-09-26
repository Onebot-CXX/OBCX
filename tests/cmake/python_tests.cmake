function(obcx_add_python_unittest source labels)
  get_filename_component(name "${source}" NAME_WE)
  add_test(NAME ${name} COMMAND ${Python3_EXECUTABLE} -m unittest -v
                                ${CMAKE_CURRENT_SOURCE_DIR}/${source})
  set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                                          LABELS "${labels}")
endfunction()

obcx_add_python_unittest(package/package_contract_test.py "contract;package;metadata")
obcx_add_python_unittest(package/package_resolution_test.py "contract;package;resolver")
obcx_add_python_unittest(package/package_sources_test.py "contract;package;sources")
obcx_add_python_unittest(package/package_provider_test.py "contract;package;providers")
obcx_add_python_unittest(package/package_cmake_test.py "contract;package;cmake")

# V1 parser cases are superseded by the canonical package contract/resolver
# suites above. Keep registry-specific checks without duplicating those rules.
obcx_add_python_unittest(package/package_registry_test.py "contract;package;registry")
obcx_add_python_unittest(package/actor_vcpkg_manifest_test.py
                         "contract;actor-package;metadata;packaging")
obcx_add_python_unittest(package/actor_release_tools_test.py
                         "contract;actor-package;release;deployment")

obcx_add_python_unittest(bot/bot_platform_modularity_test.py
                         "architecture;bot-runtime;isolation")
