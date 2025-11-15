cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(tomlplusplus REQUIRED)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
  add_executable(obcx main.cpp)

  target_link_libraries(obcx PRIVATE obcx_core tomlplusplus::tomlplusplus)

  install(TARGETS obcx RUNTIME DESTINATION bin)
endif()

