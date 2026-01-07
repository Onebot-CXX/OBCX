# QQ to Telegram Plugin CMakeLists.txt

# Find required packages
find_package(tomlplusplus REQUIRED)

# Create the QQ to TG plugin shared library
add_library(
  qq_to_tg_plugin SHARED
  qq_to_tg_plugin.cpp
  qq_to_tg_plugin.hpp
  # dependency/bridge_bot dependencies
  ../dependency/config.cpp
  ../dependency/qq_handler.cpp
  ../dependency/telegram_handler.cpp
  ../dependency/media_processor.cpp
  ../dependency/path_manager.cpp
  ../dependency/database_manager.cpp
  ../dependency/retry_queue_manager.cpp
  ../dependency/telegram/telegram_media_processor.cpp
  ../dependency/telegram/telegram_message_formatter.cpp
  ../dependency/telegram/telegram_command_handler.cpp
  ../dependency/telegram/telegram_event_handler.cpp
  ../dependency/qq/qq_media_processor.cpp
  ../dependency/qq/qq_command_handler.cpp
  ../dependency/qq/qq_event_handler.cpp
  ../dependency/qq/qq_message_formatter.cpp)

# Set the output name to match plugin loading expectations
set_target_properties(
  qq_to_tg_plugin
  PROPERTIES OUTPUT_NAME "qq_to_tg"
             PREFIX "" # No lib prefix
             POSITION_INDEPENDENT_CODE ON
             CXX_STANDARD 26
             CXX_STANDARD_REQUIRED ON)

# Include directories
target_include_directories(
  qq_to_tg_plugin PRIVATE ${CMAKE_SOURCE_DIR}/examples
                          ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

# Link against OBCX core and dependency/bridge_bot dependencies
target_link_libraries(
  qq_to_tg_plugin
  PRIVATE obcx_core tomlplusplus::tomlplusplus unofficial::sqlite3::sqlite3
          OpenSSL::SSL OpenSSL::Crypto)

# Set the output directory to build/plugins
set_target_properties(qq_to_tg_plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY
                                                 ${CMAKE_BINARY_DIR}/plugins)
