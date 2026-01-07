# MC RCON Plugin CMakeLists.txt

# Enable C language for mcrcon.c
enable_language(C)

# Find required packages
find_package(tomlplusplus REQUIRED)

# Create the MC RCON plugin shared library
add_library(
  mc_rcon_plugin SHARED
  mc_rcon_plugin.cpp
  mc_rcon_plugin.hpp
  rcon_client.cpp
  rcon_client.hpp
  mcrcon.c
  mcrcon.h
  config.cpp
  config.hpp)

# Set the source file language explicitly for mcrcon.c
set_source_files_properties(mcrcon.c PROPERTIES LANGUAGE C)

# Set the output name to match plugin loading expectations
set_target_properties(
  mc_rcon_plugin
  PROPERTIES OUTPUT_NAME "mc_rcon"
             PREFIX "" # No lib prefix
             POSITION_INDEPENDENT_CODE ON
             CXX_STANDARD 26
             CXX_STANDARD_REQUIRED ON
             C_STANDARD 11
             C_STANDARD_REQUIRED ON)

# Include directories
target_include_directories(
  mc_rcon_plugin PRIVATE ${CMAKE_SOURCE_DIR}/examples
                         ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

# Link against OBCX core and required dependencies
target_link_libraries(
  mc_rcon_plugin
  PRIVATE obcx_core tomlplusplus::tomlplusplus)

# Set the output directory to build/plugins
set_target_properties(mc_rcon_plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY
                                                ${CMAKE_BINARY_DIR}/plugins)
