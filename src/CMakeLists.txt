set(OBCX_GENERIC_RUNTIME_SOURCES
    common/logger.cpp
    common/cli_handler.cpp
    tui/tui_app.cpp
    tui/tui_layout.cpp
    common/json_utils.cpp
    common/message_type.cpp
    common/config_snapshot.cpp
    core/runtime/process_configuration.cpp
    network/curl_asio_multi.cpp
    network/http_client.cpp
    network/http_client_sync.cpp
    network/websocket_client.cpp
    network/websocket_write_queue.cpp
    network/proxy_http_client.cpp
    core/actor/actor_manager.cpp
    core/actor/actor_package_stager.cpp
    core/actor/actor_work_stealing_executor.cpp
    core/actor/native_actor_scheduler.cpp
    core/bot/bot_component_runtime.cpp
    core/bot/bot_event_components.cpp
    core/bot/bot_installation_assembler.cpp
    core/bot/bot_installation_directory.cpp
    core/bot/bot_operation_dispatcher.cpp
    core/bot/operation_registry.cpp
    core/bot/platform_catalog.cpp
    core/bot/installation_plan.cpp
    core/command/command_coordinator.cpp
    core/command/command_matcher.cpp
    core/infrastructure/db_manager.cpp
    core/infrastructure/process_staging_uuid.cpp
    core/runtime/actor_runtime_reload_controller.cpp
    core/runtime/message_event_ingress.cpp
    core/runtime/orchestrator.cpp
    core/runtime/runtime_generation.cpp
    core/runtime/runtime_thread_budget.cpp
)
add_library(obcx_generic_runtime_objects OBJECT ${OBCX_GENERIC_RUNTIME_SOURCES})

set(OBCX_ONEBOT11_RUNTIME_SOURCES
    onebot11/network/http/connection_manager.cpp
    onebot11/network/websocket/action_request_tracker.cpp
    onebot11/network/websocket/connection_manager.cpp
    onebot11/adapter/protocol_adapter.cpp
    onebot11/adapter/message_converter.cpp
    onebot11/adapter/event_converter.cpp
    onebot11/bot/operations.cpp
    onebot11/bot/response_parser.cpp
    onebot11/bot/configuration.cpp
    onebot11/bot/recipe.cpp
    onebot11/bot/protocol.cpp
    onebot11/bot/transport.cpp
    onebot11/bot/event_ingress.cpp
    onebot11/bot/command_adapter.cpp
)
add_library(obcx_onebot11_runtime_objects OBJECT ${OBCX_ONEBOT11_RUNTIME_SOURCES})

set(OBCX_TELEGRAM_RUNTIME_SOURCES
    telegram/adapter/protocol_adapter.cpp
    telegram/network/http/connection_manager.cpp
    telegram/bot/operations.cpp
    telegram/bot/response_parser.cpp
    telegram/bot/configuration.cpp
    telegram/bot/recipe.cpp
    telegram/bot/protocol.cpp
    telegram/bot/transport.cpp
    telegram/bot/event_ingress.cpp
    telegram/bot/command_adapter.cpp
    telegram/bot/command_catalog_component.cpp
)
add_library(obcx_telegram_runtime_objects OBJECT ${OBCX_TELEGRAM_RUNTIME_SOURCES})

# The generic archive is independently linkable without either platform module.
add_library(obcx_generic_runtime STATIC $<TARGET_OBJECTS:obcx_generic_runtime_objects>)
add_library(obcx_core SHARED
  $<TARGET_OBJECTS:obcx_generic_runtime_objects>
  $<TARGET_OBJECTS:obcx_onebot11_runtime_objects>
  $<TARGET_OBJECTS:obcx_telegram_runtime_objects>)

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
        ZLIB::ZLIB
        ${BROTLIDEC_TARGET}
        ${ZSTD_TARGET}
        nlohmann_json::nlohmann_json
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto
        ${OBCX_SQLITE_TARGET}
        tomlplusplus::tomlplusplus
        ftxui::screen
        ftxui::dom
        ftxui::component
        re2::re2)

# Identical toolchain/network definitions across object boundaries. These
# implementation targets are process-private and are not exported in the SDK.
foreach(_runtime IN ITEMS obcx_generic_runtime_objects obcx_onebot11_runtime_objects
                          obcx_telegram_runtime_objects obcx_generic_runtime)
  target_include_directories(${_runtime} PUBLIC
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
  target_link_libraries(${_runtime} PUBLIC fmt::fmt
        spdlog::spdlog
        ${BOOST_SYSTEM_TARGET}
        ZLIB::ZLIB
        ${BROTLIDEC_TARGET}
        ${ZSTD_TARGET}
        nlohmann_json::nlohmann_json
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto
        ${OBCX_SQLITE_TARGET}
        tomlplusplus::tomlplusplus
        ftxui::screen
        ftxui::dom
        ftxui::component
        re2::re2)
  target_compile_features(${_runtime} PUBLIC cxx_std_26)
  target_compile_options(${_runtime} PUBLIC -freflection)
  target_compile_definitions(${_runtime} PRIVATE
    OBCX_PATCHELF_EXECUTABLE="${OBCX_PATCHELF_EXECUTABLE}")
  set_target_properties(${_runtime} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(OBCX_ENABLE_IO_URING AND OBCX_IO_URING_TARGET)
    target_compile_definitions(${_runtime} PUBLIC BOOST_ASIO_HAS_IO_URING=1)
    target_link_libraries(${_runtime} PUBLIC ${OBCX_IO_URING_TARGET})
  endif()
endforeach()

target_link_libraries(obcx_core PRIVATE CURL::libcurl)
target_link_libraries(obcx_generic_runtime_objects PRIVATE CURL::libcurl)
target_link_libraries(obcx_generic_runtime PRIVATE CURL::libcurl)
target_link_libraries(obcx_onebot11_runtime_objects PRIVATE obcx_generic_runtime)
target_link_libraries(obcx_telegram_runtime_objects PRIVATE obcx_generic_runtime)

target_compile_features(obcx_core PUBLIC cxx_std_26)
target_compile_options(obcx_core PUBLIC -freflection)
target_compile_definitions(obcx_core PRIVATE
    OBCX_PATCHELF_EXECUTABLE="${OBCX_PATCHELF_EXECUTABLE}")

# ============================================================================
# Boost.Asio io_uring backend
# ----------------------------------------------------------------------------
# When OBCX_ENABLE_IO_URING is ON and liburing was found (see top-level
# CMakeLists.txt), expose Boost.Asio's io_uring support and link liburing.
# Keep epoll enabled so socket/timer I/O does not depend on io_uring queue
# creation succeeding at runtime. These macros are PUBLIC because Asio is
# header-only and every translation unit that includes <boost/asio.hpp> must
# see the same config.
# ============================================================================
if(OBCX_ENABLE_IO_URING AND OBCX_IO_URING_TARGET)
  target_compile_definitions(obcx_core PUBLIC BOOST_ASIO_HAS_IO_URING=1)
  target_link_libraries(obcx_core PUBLIC ${OBCX_IO_URING_TARGET})
endif()

# Enable position independent code for shared library compatibility
set_target_properties(
  obcx_core PROPERTIES POSITION_INDEPENDENT_CODE ON
                       INSTALL_RPATH_USE_LINK_PATH TRUE)

# Alias target for consistent usage (in-tree and installed)
add_library(obcx::obcx_core ALIAS obcx_core)

# Include the generated version header
target_include_directories(obcx_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>)

# Independently consumable Bot contracts, separate from the Actor host library.
add_library(bot_common_sdk INTERFACE)
add_library(obcx::bot_common_sdk ALIAS bot_common_sdk)
target_include_directories(bot_common_sdk INTERFACE
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/obcx>)
target_compile_features(bot_common_sdk INTERFACE cxx_std_26)
target_link_libraries(bot_common_sdk INTERFACE
  nlohmann_json::nlohmann_json ${BOOST_SYSTEM_TARGET} Threads::Threads)
foreach(_platform IN ITEMS onebot11 telegram)
  add_library(bot_${_platform}_sdk INTERFACE)
  add_library(obcx::bot_${_platform}_sdk ALIAS bot_${_platform}_sdk)
  target_link_libraries(bot_${_platform}_sdk INTERFACE bot_common_sdk)
endforeach()

# ===== Install rules =====

# Install the library target
install(TARGETS obcx_core bot_common_sdk bot_onebot11_sdk bot_telegram_sdk
    EXPORT obcx-sdk-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx)

# Install the actor-package SDK surface. Runtime application, protocol, and
# transport implementation headers are intentionally not exported.
install(FILES
    ${CMAKE_SOURCE_DIR}/include/common/config_snapshot.hpp
    ${CMAKE_SOURCE_DIR}/include/common/bot_installation_metadata.hpp
    ${CMAKE_SOURCE_DIR}/include/common/json_utils.hpp
    ${CMAKE_SOURCE_DIR}/include/common/logger.hpp
    ${CMAKE_SOURCE_DIR}/include/common/message_type.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx/common)

install(FILES
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor_asio.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor_generation_lifecycle.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor_restart_constraints.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor_commands.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor_manager.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor_messages.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor_task.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/actor_work_stealing_executor.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/blocking_executor.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/native_actor_scheduler.hpp
    ${CMAKE_SOURCE_DIR}/include/core/actor/reflected_actor.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx/core/actor)

install(FILES
    ${CMAKE_SOURCE_DIR}/include/core/bot/ids.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/json_codec.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/validation.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/references.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/operation_error.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/operation_result.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/operation_traits.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/messaging.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/gateway_codec.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/operation_gateway.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/typed_operation.hpp
    ${CMAKE_SOURCE_DIR}/include/core/bot/messaging_client.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx/core/bot)

install(FILES
    ${CMAKE_SOURCE_DIR}/include/onebot11/bot/actions.hpp
    ${CMAKE_SOURCE_DIR}/include/onebot11/bot/types.hpp
    ${CMAKE_SOURCE_DIR}/include/onebot11/bot/operations.hpp
    ${CMAKE_SOURCE_DIR}/include/onebot11/bot/client.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx/onebot11/bot)

install(FILES
    ${CMAKE_SOURCE_DIR}/include/telegram/bot/actions.hpp
    ${CMAKE_SOURCE_DIR}/include/telegram/bot/types.hpp
    ${CMAKE_SOURCE_DIR}/include/telegram/bot/operations.hpp
    ${CMAKE_SOURCE_DIR}/include/telegram/bot/client.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx/telegram/bot)

install(FILES
    ${CMAKE_SOURCE_DIR}/include/core/infrastructure/db_manager.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx/core/infrastructure)

install(FILES
    ${CMAKE_SOURCE_DIR}/include/network/http_client.hpp
    ${CMAKE_SOURCE_DIR}/include/network/proxy_http_client.hpp
    ${CMAKE_SOURCE_DIR}/include/network/connection_config.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obcx/network)

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

# Install build helper modules alongside the SDK
install(FILES
    ${CMAKE_SOURCE_DIR}/cmake/OBCXActor.cmake
    ${CMAKE_SOURCE_DIR}/cmake/actor_metadata.py
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/obcx-sdk)

add_subdirectory(app)
