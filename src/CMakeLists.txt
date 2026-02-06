add_library(obcx_core SHARED)

target_sources(
        obcx_core
        PUBLIC FILE_SET
        CXX_MODULES
        FILES
        module/obcx.cppm
        module/common/common.cppm
        module/common/logger.cppm
        module/common/cli_handler.cppm
        module/common/component_manager.cppm
        module/common/i18n_log_messages.cppm
        module/common/embedded_locales.cppm
        module/common/json_utils.cppm
        module/common/message_type.cppm
        module/common/media_converter.cppm
        module/common/config_loader.cppm
        module/common/plugin_manager.cppm
        module/core/core.cppm
        module/core/event_dispatcher.cppm
        module/core/qq_bot.cppm
        module/core/task_scheduler.cppm
        module/core/tg_bot.cppm
        module/interface/interface.cppm
        module/interface/plugin.cppm
        module/interface/bot.cppm
        module/interface/connection_manager.cppm
        module/interface/protocol_adapter.cppm
        module/network/network.cppm
        module/network/http_client.cppm
        module/network/websocket_client.cppm
        module/network/proxy_http_client.cppm
        module/onebot11/onebot11.cppm
        module/onebot11/network/http/connection_manager.cppm
        module/onebot11/network/websocket/connection_manager.cppm
        module/onebot11/adapter/protocol_adapter.cppm
        module/onebot11/adapter/message_converter.cppm
        module/onebot11/adapter/event_converter.cppm
        module/telegram/telegram.cppm
        module/telegram/adapter/protocol_adapter.cppm
        module/telegram/network/http/connection_manager.cppm
        module/tui/tui.cppm
        module/tui/app.cppm
        module/tui/sink.cppm)

target_sources(
        obcx_core
        PRIVATE common/logger.cpp
        common/cli_handler.cpp
        common/component_manager.cpp
        common/i18n_log_messages.cpp
        common/embedded_locales.cpp
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
        tui/tui_app.cpp
        core/qq_bot.cpp
        core/tg_bot.cpp)

target_include_directories(obcx_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(
        obcx_core
        PUBLIC
        fmt::fmt
        spdlog::spdlog
        Boost::system
        Boost::locale
        nlohmann_json::nlohmann_json
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto
        tomlplusplus::tomlplusplus
        ftxui::screen
        ftxui::dom
        ftxui::component)

target_compile_features(obcx_core PUBLIC cxx_std_26)

# Make obcx_core depend on i18n target to ensure .mo files are built first
if (TARGET i18n)
    add_dependencies(obcx_core i18n)
endif ()

# Enable position independent code for shared library compatibility
set_target_properties(obcx_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_subdirectory(plugin)
