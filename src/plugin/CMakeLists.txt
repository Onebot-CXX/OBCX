find_package(tomlplusplus CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS program_options)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
  add_executable(obcx main.cpp)
  # set_target_properties(obcx PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED
  # OFF )
  target_link_libraries(obcx PRIVATE obcx_core tomlplusplus::tomlplusplus
                                     Boost::program_options)

  install(TARGETS obcx RUNTIME DESTINATION bin)
endif()
