find_package(tomlplusplus CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS program_options)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
  add_executable(obcx main.cpp)
  target_link_libraries(obcx PRIVATE obcx_core tomlplusplus::tomlplusplus
                                     Boost::program_options)

  install(TARGETS obcx RUNTIME DESTINATION bin)
endif()
