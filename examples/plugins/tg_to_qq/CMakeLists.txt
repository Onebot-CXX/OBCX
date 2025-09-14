# Telegram to QQ Plugin CMakeLists.txt

# Find required packages
find_package(tomlplusplus REQUIRED)

# Create the TG to QQ plugin shared library
add_library(tg_to_qq_plugin SHARED
    TGToQQPlugin.cpp
    TGToQQPlugin.hpp
    # BridgeBot dependencies
    ${CMAKE_SOURCE_DIR}/examples/BridgeBot/Config.cpp
    ${CMAKE_SOURCE_DIR}/examples/BridgeBot/QQHandler.cpp
    ${CMAKE_SOURCE_DIR}/examples/BridgeBot/TelegramHandler.cpp
    ${CMAKE_SOURCE_DIR}/examples/BridgeBot/MediaProcessor.cpp
    ${CMAKE_SOURCE_DIR}/examples/BridgeBot/PathManager.cpp
    ${CMAKE_SOURCE_DIR}/examples/BridgeBot/DatabaseManager.cpp
    ${CMAKE_SOURCE_DIR}/examples/BridgeBot/RetryQueueManager.cpp
    ${CMAKE_SOURCE_DIR}/examples/BridgeBot/telegram/TelegramMediaProcessor.cpp
    ${CMAKE_SOURCE_DIR}/examples/BridgeBot/telegram/TelegramMessageFormatter.cpp
    ${CMAKE_SOURCE_DIR}/examples/BridgeBot/telegram/TelegramCommandHandler.cpp
    ${CMAKE_SOURCE_DIR}/examples/BridgeBot/telegram/TelegramEventHandler.cpp
    ${CMAKE_SOURCE_DIR}/examples/BridgeBot/qq/QQMediaProcessor.cpp
)

# Set the output name to match plugin loading expectations
set_target_properties(tg_to_qq_plugin PROPERTIES
    OUTPUT_NAME "tg_to_qq"
    PREFIX ""  # No lib prefix
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Include directories
target_include_directories(tg_to_qq_plugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/examples
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

# Link against OBCX core and BridgeBot dependencies
target_link_libraries(tg_to_qq_plugin
    PRIVATE
        obcx_core
        tomlplusplus::tomlplusplus
        unofficial::sqlite3::sqlite3 
        OpenSSL::SSL 
        OpenSSL::Crypto
)

# Set the output directory to build/plugins
set_target_properties(tg_to_qq_plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
)