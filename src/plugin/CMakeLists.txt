find_package(tomlplusplus CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS program_options)
find_package(nlohmann_json CONFIG REQUIRED)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
  add_executable(obcx
    main.cpp
    plugin_cli.cpp
    plugin_registry.cpp
    plugin_installer.cpp
    plugin_resolver.cpp)
  target_link_libraries(obcx PRIVATE obcx_core tomlplusplus::tomlplusplus
                                     Boost::program_options
                                     nlohmann_json::nlohmann_json)

  install(TARGETS obcx RUNTIME DESTINATION bin)
endif()
