cmake_minimum_required(VERSION 3.30)
project(obcx_offline_bot_acceptance LANGUAGES CXX)
if(NOT DEFINED OBCX_SOURCE_ROOT OR NOT DEFINED OBCX_FROZEN_SDK)
  message(FATAL_ERROR "OBCX_SOURCE_ROOT and OBCX_FROZEN_SDK must be explicit")
endif()
find_package(obcx-sdk 2.4.1 CONFIG REQUIRED)
add_executable(bot_sdk_acceptance host.cpp)
set_target_properties(bot_sdk_acceptance PROPERTIES
  CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES NO_SYSTEM_FROM_IMPORTED YES)
target_compile_options(bot_sdk_acceptance PRIVATE -freflection)
# Only the offline process host needs internal event ingress/lifecycle seams.
# The independently built consumer DSO uses installed public headers only.
target_include_directories(bot_sdk_acceptance BEFORE PRIVATE
  "${OBCX_FROZEN_SDK}/include/obcx" "${OBCX_SOURCE_ROOT}/include")
target_link_libraries(bot_sdk_acceptance PRIVATE obcx::obcx_core ${CMAKE_DL_LIBS})
