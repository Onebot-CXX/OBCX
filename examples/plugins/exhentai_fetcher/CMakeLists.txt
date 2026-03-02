# ExHentai Fetcher Plugin CMakeLists.txt

find_package(LibXml2 QUIET)
if(NOT LibXml2_FOUND)
  message(WARNING "LibXml2 not found — skipping exhentai_fetcher plugin (run: nix develop examples/plugins/exhentai_fetcher)")
  return()
endif()

add_library(exhentai_fetcher_plugin SHARED exhentai_fetcher_plugin.cpp
                                           exhentai_fetcher_plugin.hpp
                                           exhentai_parser.cpp
                                           exhentai_parser.hpp)

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

target_link_libraries(exhentai_fetcher_plugin PRIVATE obcx_core LibXml2::LibXml2)

set_target_properties(
  exhentai_fetcher_plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY
                                     ${CMAKE_BINARY_DIR}/plugins)
