# Emoji Export Plugin CMakeLists.txt

# Create the Emoji Export plugin shared library
add_library(
  sticker_export_plugin SHARED
  sticker_export_plugin.cpp
  sticker_export_plugin.hpp)

# Set the output name to match plugin loading expectations
set_target_properties(
  sticker_export_plugin
  PROPERTIES OUTPUT_NAME "sticker_export"
             PREFIX "" # No lib prefix
             POSITION_INDEPENDENT_CODE ON
             CXX_STANDARD 26
             CXX_STANDARD_REQUIRED ON)

# Include directories
target_include_directories(
  sticker_export_plugin PRIVATE ${CMAKE_SOURCE_DIR}/examples
                               ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

# Link against OBCX core
target_link_libraries(
  sticker_export_plugin
  PRIVATE obcx_core)

# Set the output directory to build/plugins
set_target_properties(sticker_export_plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY
                                                      ${CMAKE_BINARY_DIR}/plugins)
