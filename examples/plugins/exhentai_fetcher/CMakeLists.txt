# ExHentai Fetcher Plugin CMakeLists.txt

add_library(exhentai_fetcher_plugin SHARED exhentai_fetcher_plugin.cpp
                                           exhentai_fetcher_plugin.hpp)

set_target_properties(
  exhentai_fetcher_plugin
  PROPERTIES OUTPUT_NAME "exhentai_fetcher"
             PREFIX ""
             POSITION_INDEPENDENT_CODE ON
             CXX_STANDARD 26
             CXX_STANDARD_REQUIRED ON)

target_include_directories(
  exhentai_fetcher_plugin
  PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src
          ${CMAKE_SOURCE_DIR}/examples)

target_link_libraries(exhentai_fetcher_plugin PRIVATE obcx_core)

set_target_properties(
  exhentai_fetcher_plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY
                                     ${CMAKE_BINARY_DIR}/plugins)
