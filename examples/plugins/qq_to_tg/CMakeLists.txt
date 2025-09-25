# QQ to Telegram Plugin CMakeLists.txt

# Find required packages
find_package(tomlplusplus REQUIRED)

# Create the QQ to TG plugin shared library
add_library(
  qq_to_tg_plugin SHARED
  QQToTGPlugin.cpp
  QQToTGPlugin.hpp
  # BridgeBot dependencies
  ../BridgeBot/Config.cpp
  ../BridgeBot/QQHandler.cpp
  ../BridgeBot/TelegramHandler.cpp
  ../BridgeBot/MediaProcessor.cpp
  ../BridgeBot/PathManager.cpp
  ../BridgeBot/DatabaseManager.cpp
  ../BridgeBot/RetryQueueManager.cpp
  ../BridgeBot/telegram/TelegramMediaProcessor.cpp
  ../BridgeBot/telegram/TelegramMessageFormatter.cpp
  ../BridgeBot/telegram/TelegramCommandHandler.cpp
  ../BridgeBot/telegram/TelegramEventHandler.cpp
  ../BridgeBot/qq/QQMediaProcessor.cpp)

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

# Link against OBCX core and BridgeBot dependencies
target_link_libraries(
  qq_to_tg_plugin
  PRIVATE obcx_core tomlplusplus::tomlplusplus unofficial::sqlite3::sqlite3
          OpenSSL::SSL OpenSSL::Crypto)

# Set the output directory to build/plugins
set_target_properties(qq_to_tg_plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY
                                                 ${CMAKE_BINARY_DIR}/plugins)

