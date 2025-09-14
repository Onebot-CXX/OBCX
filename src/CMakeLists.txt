add_library(
        obcx_core SHARED
        common/Logger.cpp
        common/JsonUtils.cpp
        common/MessageType.cpp
        common/MediaConverter.cpp
        common/ConfigLoader.cpp
        common/PluginManager.cpp
        
        interface/IPlugin.cpp

        network/HttpClient.cpp
        network/WebsocketClient.cpp
        network/ProxyHttpClient.cpp
        network/IConnectionManager.cpp

        onebot11/network/http/ConnectionManager.cpp
        onebot11/network/websocket/ConnectionManager.cpp
        onebot11/adapter/ProtocolAdapter.cpp
        onebot11/adapter/MessageConverter.cpp
        onebot11/adapter/EventConverter.cpp

        telegram/adapter/ProtocolAdapter.cpp
        telegram/network/http/ConnectionManager.cpp

        core/QQBot.cpp
        core/TGBot.cpp
        core/IBot.cpp)

target_include_directories(obcx_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(
        obcx_core 
        PUBLIC 
            fmt::fmt
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
