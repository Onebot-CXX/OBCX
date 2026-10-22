cmake_minimum_required(VERSION 3.30)

# A separate entry point: never add the core source tree to this build graph.
foreach(_option obcx-sdk_DIR CMAKE_CXX_COMPILER CMAKE_BUILD_TYPE
                ENABLE_DEBUG_TRACE)
  if(NOT DEFINED ${_option} OR "${${_option}}" STREQUAL "")
    message(
      FATAL_ERROR "Explicit ${_option} is required for an isolated actor build")
  endif()
endforeach()

project(OBCXActors LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(
  obcx-sdk
  CONFIG
  REQUIRED
  GLOBAL
  PATHS
  "${obcx-sdk_DIR}"
  NO_DEFAULT_PATH)
get_target_property(_imported obcx::obcx_core IMPORTED)
if(NOT _imported)
  message(
    FATAL_ERROR "Actor-only builds require an imported, installed OBCX SDK")
endif()

if(ENABLE_DEBUG_TRACE)
  add_compile_definitions(OBCX_DEBUG_TRACE)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(__OBCX_DEBUG_COMPILATION)
endif()

include(OBCXPackages)
obcx_load_configured_workspace()
