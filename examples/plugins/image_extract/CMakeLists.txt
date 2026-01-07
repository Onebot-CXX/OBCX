# Image Extract Plugin CMakeLists.txt

# Create the Image Extract plugin shared library
add_library(
  image_extract_plugin SHARED
  image_extract_plugin.cpp
  image_extract_plugin.hpp)

# Set the output name to match plugin loading expectations
set_target_properties(
  image_extract_plugin
  PROPERTIES OUTPUT_NAME "image_extract"
             PREFIX "" # No lib prefix
             POSITION_INDEPENDENT_CODE ON
             CXX_STANDARD 26
             CXX_STANDARD_REQUIRED ON)

# Include directories
target_include_directories(
  image_extract_plugin PRIVATE ${CMAKE_SOURCE_DIR}/examples
                               ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

# Link against OBCX core
target_link_libraries(
  image_extract_plugin
  PRIVATE obcx_core)

# Set the output directory to build/plugins
set_target_properties(image_extract_plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY
                                                      ${CMAKE_BINARY_DIR}/plugins)
