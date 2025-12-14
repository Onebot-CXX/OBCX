find_package(tomlplusplus REQUIRED)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
  add_executable(obcx main.cpp)
#  set_target_properties(obcx PROPERTIES
#          CXX_STANDARD 23
#          CXX_STANDARD_REQUIRED OFF
#  )
  target_link_libraries(obcx PRIVATE obcx_core tomlplusplus::tomlplusplus)

  install(TARGETS obcx RUNTIME DESTINATION bin)
endif()

