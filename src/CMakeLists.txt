add_library(
  obcx_core SHARED
  common/logger.cpp
  common/json_utils.cpp
  common/message_type.cpp
  common/media_converter.cpp
  common/config_loader.cpp
  common/plugin_manager.cpp
  interfaces/plugin.cpp
  interfaces/bot.cpp
  interfaces/connection_manager.cpp
  network/http_client.cpp
  network/websocket_client.cpp
  network/proxy_http_client.cpp
  onebot11/network/http/connection_manager.cpp
  onebot11/network/websocket/connection_manager.cpp
  onebot11/adapter/protocol_adapter.cpp
  onebot11/adapter/message_converter.cpp
  onebot11/adapter/event_converter.cpp
  telegram/adapter/protocol_adapter.cpp
  telegram/network/http/connection_manager.cpp
  core/qq_bot.cpp
  core/tg_bot.cpp)

target_include_directories(obcx_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(
  obcx_core
  PUBLIC fmt::fmt
         spdlog::spdlog
         Boost::system
         nlohmann_json::nlohmann_json
         Threads::Threads
         OpenSSL::SSL
         OpenSSL::Crypto
         unofficial::sqlite3::sqlite3
         tomlplusplus::tomlplusplus)

target_compile_features(obcx_core PUBLIC cxx_std_20)

# Enable position independent code for shared library compatibility
set_target_properties(obcx_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_subdirectory(plugin)
