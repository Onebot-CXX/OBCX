# OBCXPlugin.cmake — Helper module for building OBCX plugins
#
# Usage (in plugin CMakeLists.txt):
#   include(OBCXPlugin)
#   obcx_add_plugin(my_plugin
#       SOURCES main.cpp helper.cpp
#       DEPS tomlplusplus::tomlplusplus
#       OUTPUT_NAME my_plugin_name)

# ===== Resolve obcx_core dependency =====
if(TARGET obcx_core)
    # In-tree build: obcx_core is already available
    if(NOT TARGET obcx::obcx_core)
        add_library(obcx::obcx_core ALIAS obcx_core)
    endif()
else()
    # Out-of-tree / standalone plugin build
    find_package(obcx-sdk REQUIRED)
endif()

# ===== obcx_add_plugin function =====
function(obcx_add_plugin PLUGIN_NAME)
    cmake_parse_arguments(PARSE_ARGV 1 PLUGIN "" "OUTPUT_NAME" "SOURCES;DEPS")

    if(NOT PLUGIN_SOURCES)
        message(FATAL_ERROR "obcx_add_plugin(${PLUGIN_NAME}): SOURCES must not be empty")
    endif()

    set(_TARGET "${PLUGIN_NAME}_plugin")

    # Create shared library
    add_library(${_TARGET} SHARED ${PLUGIN_SOURCES})

    # Determine output name
    if(PLUGIN_OUTPUT_NAME)
        set(_OUTPUT_NAME "${PLUGIN_OUTPUT_NAME}")
    else()
        set(_OUTPUT_NAME "${PLUGIN_NAME}")
    endif()

    # Set standard plugin properties
    set_target_properties(${_TARGET} PROPERTIES
        OUTPUT_NAME "${_OUTPUT_NAME}"
        PREFIX ""
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")

    # Link to obcx core
    if(TARGET obcx_core)
        # In-tree: link directly to obcx_core
        target_link_libraries(${_TARGET} PRIVATE obcx_core)
    else()
        target_link_libraries(${_TARGET} PRIVATE obcx::obcx_core)
    endif()

    # Link additional dependencies
    if(PLUGIN_DEPS)
        target_link_libraries(${_TARGET} PRIVATE ${PLUGIN_DEPS})
    endif()

    # Include directories for in-tree builds
    if(TARGET obcx_core)
        target_include_directories(${_TARGET} PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/examples)
    endif()
endfunction()
