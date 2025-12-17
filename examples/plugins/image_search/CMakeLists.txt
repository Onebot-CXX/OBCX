# Telegram to QQ Plugin CMakeLists.txt

# Find required packages
find_package(tomlplusplus REQUIRED)

# Create the TG to QQ plugin shared library
add_library(
  tg_to_qq_plugin SHARED
  tg_to_qq_plugin.cpp
  tg_to_qq_plugin.hpp
  # dependency/bridge_bot dependencies
  ../dependency/bridge_bot/config.cpp
  ../dependency/bridge_bot/qq_handler.cpp
  ../dependency/bridge_bot/telegram_handler.cpp
  ../dependency/bridge_bot/media_processor.cpp
  ../dependency/bridge_bot/path_manager.cpp
  ../dependency/bridge_bot/database_manager.cpp
  ../dependency/bridge_bot/retry_queue_manager.cpp
  ../dependency/bridge_bot/telegram/telegram_media_processor.cpp
  ../dependency/bridge_bot/telegram/telegram_message_formatter.cpp
  ../dependency/bridge_bot/telegram/telegram_command_handler.cpp
  ../dependency/bridge_bot/telegram/telegram_event_handler.cpp
  ../dependency/bridge_bot/qq/qq_media_processor.cpp)

# Set the output name to match plugin loading expectations
set_target_properties(
  tg_to_qq_plugin
  PROPERTIES OUTPUT_NAME "tg_to_qq"
             PREFIX "" # No lib prefix
             POSITION_INDEPENDENT_CODE ON
             CXX_STANDARD 20
             CXX_STANDARD_REQUIRED ON)

# Include directories
target_include_directories(
  tg_to_qq_plugin PRIVATE ${CMAKE_SOURCE_DIR}/examples
                          ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

# Link against OBCX core and dependency/bridge_bot dependencies
target_link_libraries(
  tg_to_qq_plugin
  PRIVATE obcx_core tomlplusplus::tomlplusplus unofficial::sqlite3::sqlite3
          OpenSSL::SSL OpenSSL::Crypto)

# Set the output directory to build/plugins
set_target_properties(tg_to_qq_plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY
                                                 ${CMAKE_BINARY_DIR}/plugins)
