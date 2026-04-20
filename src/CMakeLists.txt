add_library(
        obcx_core SHARED
        common/logger.cpp
        common/cli_handler.cpp
        tui/tui_app.cpp
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
        network/compression.cpp
        network/http_client.cpp
        network/http_client_sync.cpp
        network/websocket_client.cpp
        network/proxy_http_client.cpp
        network/proxy_tunnel.cpp
        onebot11/network/http/connection_manager.cpp
        onebot11/network/websocket/connection_manager.cpp
        onebot11/adapter/protocol_adapter.cpp
        onebot11/adapter/message_converter.cpp
        onebot11/adapter/event_converter.cpp
        telegram/adapter/protocol_adapter.cpp
        telegram/network/http/connection_manager.cpp
        core/qq_bot.cpp
        core/tg_bot.cpp)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

target_include_directories(obcx_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/obcx>)

target_link_libraries(
        obcx_core
        PUBLIC fmt::fmt
        spdlog::spdlog
        ${BOOST_SYSTEM_TARGET}
        Boost::locale
        ZLIB::ZLIB
        ${BROTLIDEC_TARGET}
        ${ZSTD_TARGET}
        nlohmann_json::nlohmann_json
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto
        tomlplusplus::tomlplusplus
        ftxui::screen
        ftxui::dom
        ftxui::component
        re2::re2)

target_compile_features(obcx_core PUBLIC cxx_std_20)

# Make obcx_core depend on i18n target to ensure .mo files are built first
if (TARGET i18n)
    add_dependencies(obcx_core i18n)
endif ()

# Enable position independent code for shared library compatibility
set_target_properties(obcx_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Alias target for consistent usage (in-tree and installed)
add_library(obcx::obcx_core ALIAS obcx_core)

# Include the generated version header
target_include_directories(obcx_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>)

# ===== Install rules =====

# Install the library target
install(TARGETS obcx_core
    EXPORT obcx-sdk-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx)

# Install public headers
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx
    FILES_MATCHING
        PATTERN "*.hpp"
        PATTERN "*.h")

# Install the generated version.hpp
install(FILES ${CMAKE_BINARY_DIR}/generated/obcx/version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx/obcx)

# Export targets with namespace
install(EXPORT obcx-sdk-targets
    FILE obcx-sdk-targets.cmake
    NAMESPACE obcx::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/obcx-sdk)

# Generate and install package config files
configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/obcx-sdk-config.cmake.in
    ${CMAKE_BINARY_DIR}/obcx-sdk-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/obcx-sdk)

write_basic_package_version_file(
    ${CMAKE_BINARY_DIR}/obcx-sdk-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    ${CMAKE_BINARY_DIR}/obcx-sdk-config.cmake
    ${CMAKE_BINARY_DIR}/obcx-sdk-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/obcx-sdk)

# Install the plugin helper module alongside the SDK
install(FILES ${CMAKE_SOURCE_DIR}/cmake/OBCXPlugin.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/obcx-sdk)

add_subdirectory(plugin)
