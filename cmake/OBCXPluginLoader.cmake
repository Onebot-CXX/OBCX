# OBCXPluginLoader.cmake — Load plugins from plugins.toml
include(FetchContent)

function(obcx_load_plugins MANIFEST_FILE)
    # Ensure external plugins can find OBCXPlugin.cmake via include()
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
    set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)

    # Find Python3
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(NOT Python3_FOUND)
        find_program(PYTHON_EXEC python3)
        if(NOT PYTHON_EXEC)
            find_program(PYTHON_EXEC python)
        endif()
    else()
        set(PYTHON_EXEC ${Python3_EXECUTABLE})
    endif()

    if(NOT PYTHON_EXEC)
        message(WARNING "[OBCX Plugins] Python not found, falling back to add_subdirectory(examples)")
        add_subdirectory(${CMAKE_SOURCE_DIR}/examples ${CMAKE_BINARY_DIR}/examples)
        return()
    endif()

    # Run parser
    execute_process(
        COMMAND ${PYTHON_EXEC} ${CMAKE_SOURCE_DIR}/cmake/parse_plugins.py ${MANIFEST_FILE}
        OUTPUT_VARIABLE PLUGIN_OUTPUT
        ERROR_VARIABLE PLUGIN_ERROR
        RESULT_VARIABLE PLUGIN_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    if(NOT PLUGIN_RESULT EQUAL 0)
        message(WARNING "[OBCX Plugins] Failed to parse ${MANIFEST_FILE}: ${PLUGIN_ERROR}")
        message(WARNING "[OBCX Plugins] Falling back to add_subdirectory(examples)")
        add_subdirectory(${CMAKE_SOURCE_DIR}/examples ${CMAKE_BINARY_DIR}/examples)
        return()
    endif()

    # Parse output — use newline as list separator
    # Note: pipe '|' is used as field delimiter (not ';') to avoid conflicts
    # with CMake's list separator
    set(ENABLED_LOCAL "")
    set(DISABLED_LOCAL "")
    set(REMOTE_PLUGINS "")

    # Split output into lines
    string(REPLACE "\n" ";" PLUGIN_LINES "${PLUGIN_OUTPUT}")
    set(_OVERRIDES "")

    # Collect overrides first
    foreach(LINE ${PLUGIN_LINES})
        if(LINE MATCHES "^OVERRIDE\\|([^|]+)\\|([^|]*)\\|([^|]*)$")
            list(APPEND _OVERRIDES "${CMAKE_MATCH_1}")
            set(_OVERRIDE_TAG_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
            set(_OVERRIDE_BRANCH_${CMAKE_MATCH_1} "${CMAKE_MATCH_3}")
        endif()
    endforeach()

    # Process plugins
    foreach(LINE ${PLUGIN_LINES})
        if(LINE MATCHES "^LOCAL\\|([^|]+)\\|([^|]+)\\|([^|]+)$")
            set(_NAME "${CMAKE_MATCH_1}")
            set(_ENABLED "${CMAKE_MATCH_2}")
            set(_PATH "${CMAKE_MATCH_3}")

            # Allow cmake -DPLUGIN_xxx=OFF override
            if(DEFINED PLUGIN_${_NAME})
                set(_ENABLED "${PLUGIN_${_NAME}}")
            endif()

            if(_ENABLED STREQUAL "true" OR _ENABLED STREQUAL "ON" OR _ENABLED STREQUAL "1")
                # Support both relative (to source dir) and absolute paths
                if(IS_ABSOLUTE ${_PATH})
                    set(_FULL_PATH "${_PATH}")
                else()
                    set(_FULL_PATH "${CMAKE_SOURCE_DIR}/${_PATH}")
                endif()
                get_filename_component(_FULL_PATH "${_FULL_PATH}" ABSOLUTE)

                if(EXISTS ${_FULL_PATH}/CMakeLists.txt)
                    add_subdirectory(${_FULL_PATH} ${CMAKE_BINARY_DIR}/plugins_build/${_NAME})
                    list(APPEND ENABLED_LOCAL "${_NAME}")
                else()
                    message(WARNING "[OBCX Plugins] Local plugin '${_NAME}' path not found: ${_PATH}")
                endif()
            else()
                list(APPEND DISABLED_LOCAL "${_NAME}")
            endif()

        elseif(LINE MATCHES "^REMOTE\\|([^|]+)$")
            set(_REPO "${CMAKE_MATCH_1}")
            set(_GIT_URL "https://github.com/${_REPO}.git")

            # Sanitize name for CMake target
            string(REPLACE "/" "_" _CMAKE_NAME "${_REPO}")

            # Determine version
            set(_GIT_TAG "")
            list(FIND _OVERRIDES "${_REPO}" _OVERRIDE_IDX)
            if(NOT _OVERRIDE_IDX EQUAL -1)
                if(NOT "${_OVERRIDE_TAG_${_REPO}}" STREQUAL "")
                    set(_GIT_TAG "${_OVERRIDE_TAG_${_REPO}}")
                elseif(NOT "${_OVERRIDE_BRANCH_${_REPO}}" STREQUAL "")
                    set(_GIT_TAG "${_OVERRIDE_BRANCH_${_REPO}}")
                endif()
            endif()

            if("${_GIT_TAG}" STREQUAL "")
                # Default: use HEAD of default branch
                set(_GIT_TAG "HEAD")
            endif()

            message(STATUS "[OBCX Plugins] Fetching remote plugin: ${_REPO} (${_GIT_TAG})")

            FetchContent_Declare(
                plugin_${_CMAKE_NAME}
                GIT_REPOSITORY ${_GIT_URL}
                GIT_TAG ${_GIT_TAG}
                GIT_SHALLOW TRUE
                SOURCE_DIR ${CMAKE_BINARY_DIR}/_plugins/${_CMAKE_NAME})

            # Print dependency hints before FetchContent_MakeAvailable
            # so the message appears above any find_package errors.
            # If source was already fetched (cached), read plugin.toml for specifics.
            set(_PLUGIN_TOML "${CMAKE_BINARY_DIR}/_plugins/${_CMAKE_NAME}/plugin.toml")
            if(EXISTS "${_PLUGIN_TOML}" AND PYTHON_EXEC)
                execute_process(
                    COMMAND ${PYTHON_EXEC}
                        "${CMAKE_SOURCE_DIR}/cmake/check_plugin_deps.py"
                        "${_PLUGIN_TOML}"
                    OUTPUT_VARIABLE _PLUGIN_VCPKG_DEPS
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                    RESULT_VARIABLE _PARSE_RESULT)

                if(_PARSE_RESULT EQUAL 0 AND _PLUGIN_VCPKG_DEPS)
                    string(REPLACE "\n" ", " _DEPS_STR "${_PLUGIN_VCPKG_DEPS}")
                    message(STATUS "[OBCX Plugins] '${_REPO}' requires: ${_DEPS_STR}")
                    message(STATUS "[OBCX Plugins] If packages are missing:")
                    message(STATUS "[OBCX Plugins]   vcpkg: python3 cmake/gen_vcpkg_manifest.py plugins.toml && vcpkg install")
                    message(STATUS "[OBCX Plugins]   system: install the packages above via your package manager")
                    message(STATUS "[OBCX Plugins]   list all deps: python3 cmake/gen_vcpkg_manifest.py plugins.toml --list")
                endif()
            else()
                message(STATUS "[OBCX Plugins] If configure fails due to missing packages:")
                message(STATUS "[OBCX Plugins]   vcpkg: python3 cmake/gen_vcpkg_manifest.py plugins.toml && vcpkg install")
                message(STATUS "[OBCX Plugins]   system: install the required packages via your package manager")
                message(STATUS "[OBCX Plugins]   list all deps: python3 cmake/gen_vcpkg_manifest.py plugins.toml --list")
            endif()

            FetchContent_MakeAvailable(plugin_${_CMAKE_NAME})

            list(APPEND REMOTE_PLUGINS "${_REPO}")
        endif()
    endforeach()

    # Summary
    if(ENABLED_LOCAL)
        list(JOIN ENABLED_LOCAL ", " _ENABLED_STR)
        message(STATUS "[OBCX Plugins] Local enabled: ${_ENABLED_STR}")
    endif()
    if(DISABLED_LOCAL)
        list(JOIN DISABLED_LOCAL ", " _DISABLED_STR)
        message(STATUS "[OBCX Plugins] Local disabled: ${_DISABLED_STR}")
    endif()
    if(REMOTE_PLUGINS)
        list(JOIN REMOTE_PLUGINS ", " _REMOTE_STR)
        message(STATUS "[OBCX Plugins] Remote: ${_REMOTE_STR}")
    endif()
    if(NOT ENABLED_LOCAL AND NOT REMOTE_PLUGINS)
        message(STATUS "[OBCX Plugins] No plugins enabled")
    endif()
endfunction()
