# Chat LLM Plugin CMakeLists.txt

# Find required packages
find_package(nlohmann_json REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

# Create the Chat LLM plugin shared library
add_library(
  chat_llm_plugin SHARED
  chat_llm_plugin.cpp
  chat_llm_plugin.hpp)

# Set the output name to match plugin loading expectations
set_target_properties(
  chat_llm_plugin
  PROPERTIES OUTPUT_NAME "chat_llm"
             PREFIX "" # No lib prefix
             POSITION_INDEPENDENT_CODE ON
             CXX_STANDARD 26
             CXX_STANDARD_REQUIRED ON)

# Include directories
target_include_directories(
  chat_llm_plugin
  PRIVATE ${CMAKE_SOURCE_DIR}/examples
          ${CMAKE_SOURCE_DIR}/include
          ${CMAKE_SOURCE_DIR}/src)

# Link against OBCX core and required libraries
target_link_libraries(chat_llm_plugin
                      PRIVATE obcx_core nlohmann_json::nlohmann_json unofficial::sqlite3::sqlite3)

# Set the output directory to build/plugins
set_target_properties(
  chat_llm_plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY
                             ${CMAKE_BINARY_DIR}/plugins)
