# QQ to Telegram Plugin CMakeLists.txt

# Find required packages
find_package(tomlplusplus REQUIRED)

# Create the QQ to TG plugin shared library
add_library(
  qq_to_tg_plugin SHARED
  qq_to_tg_plugin.cpp
  qq_to_tg_plugin.hpp
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
  qq_to_tg_plugin
  PROPERTIES OUTPUT_NAME "qq_to_tg"
             PREFIX "" # No lib prefix
             POSITION_INDEPENDENT_CODE ON
             CXX_STANDARD 20
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
