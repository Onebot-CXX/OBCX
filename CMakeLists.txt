cmake_minimum_required(VERSION 3.30)

set(OBCX_IN_NIX_SHELL OFF)
if(NOT "$ENV{IN_NIX_SHELL}" STREQUAL "")
  set(OBCX_IN_NIX_SHELL ON)
endif()

project(OBCX VERSION 1.1.0 LANGUAGES CXX)

find_program(OBCX_PATCHELF_EXECUTABLE NAMES patchelf REQUIRED)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR
    "OBCX reflected actor SDK supports Linux x86_64 and arm64 only")
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|aarch64|arm64)$")
  message(FATAL_ERROR
    "OBCX reflected actor SDK supports Linux x86_64 and arm64 only; got "
    "${CMAKE_SYSTEM_PROCESSOR}")
endif()
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16.1)
  message(FATAL_ERROR
    "OBCX requires GCC 16.1 or newer with C++26 static reflection; got "
    "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CheckCXXSourceCompiles)
set(_OBCX_SAVED_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
string(APPEND CMAKE_REQUIRED_FLAGS " -std=c++26 -freflection")
check_cxx_source_compiles([[
#include <meta>
#if !defined(__cpp_impl_reflection) || __cpp_impl_reflection < 202506L
#error OBCX requires __cpp_impl_reflection >= 202506L
#endif
#if !defined(__cpp_expansion_statements) || __cpp_expansion_statements < 202506L
#error OBCX requires __cpp_expansion_statements >= 202506L
#endif
namespace obcx_reflection_probe { struct Input {}; }
static_assert(std::meta::identifier_of(^^obcx_reflection_probe::Input) == "Input");
int main() { return 0; }
]] OBCX_CXX26_REFLECTION_SUPPORTED)
set(CMAKE_REQUIRED_FLAGS "${_OBCX_SAVED_REQUIRED_FLAGS}")
unset(_OBCX_SAVED_REQUIRED_FLAGS)
if(NOT OBCX_CXX26_REFLECTION_SUPPORTED)
  message(FATAL_ERROR
    "GCC cannot compile the OBCX C++26 reflection probe. Use GCC 16.1+ "
    "with -std=c++26 -freflection and libstdc++ <meta> support.")
endif()
add_compile_options(-freflection)

set(OBCX_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(OBCX_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(OBCX_VERSION_PATCH ${PROJECT_VERSION_PATCH})

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/obcx/version.hpp"
  @ONLY)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(VCPKG_TRIPLET "arm64-linux")
else()
  set(VCPKG_TRIPLET "x64-linux")
endif()

if(OBCX_IN_NIX_SHELL)
  set(_VCPKG_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/${VCPKG_TRIPLET}")
  if(_VCPKG_PREFIX IN_LIST CMAKE_PREFIX_PATH)
    list(REMOVE_ITEM CMAKE_PREFIX_PATH "${_VCPKG_PREFIX}")
    set(CMAKE_PREFIX_PATH
        "${CMAKE_PREFIX_PATH}"
        CACHE STRING "Package search prefixes" FORCE)
    message(STATUS "Removed vcpkg prefix from CMAKE_PREFIX_PATH in Nix shell")
  endif()
  unset(_VCPKG_PREFIX)
  message(STATUS "NIX shell activation detected, disable vcpkg")
else()
  set(CMAKE_PREFIX_PATH
      "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/${VCPKG_TRIPLET}")
endif()

option(ENABLE_DEBUG_TRACE "Enable logging with file and line numbers" OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_UNITY_BUILD ON)
  set(CMAKE_UNITY_BUILD_BATCH_SIZE 10)
endif()

if(ENABLE_DEBUG_TRACE)
  message(
    STATUS
      "Debug trace logging is enabled. Logs will include file and line numbers."
  )
  add_compile_definitions(OBCX_DEBUG_TRACE)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(__OBCX_DEBUG_COMPILATION)
endif()

if(OBCX_IN_NIX_SHELL)
  find_package(Boost CONFIG REQUIRED COMPONENTS thread)
  # Boost.System is header-only in the Nix Boost package; do not probe for the
  # vcpkg-style boost_system component config.
  set(BOOST_SYSTEM_TARGET Boost::headers)
else()
  find_package(
    Boost CONFIG REQUIRED
    COMPONENTS thread
    OPTIONAL_COMPONENTS system)
  if(TARGET Boost::system)
    set(BOOST_SYSTEM_TARGET Boost::system)
  else()
    set(BOOST_SYSTEM_TARGET Boost::headers)
  endif()
endif()
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL 8.0 REQUIRED)
find_package(ZLIB REQUIRED)
if(OBCX_IN_NIX_SHELL)
  find_package(SQLite3 REQUIRED)
  set(OBCX_SQLITE_TARGET SQLite::SQLite3)
else()
  find_package(unofficial-sqlite3 CONFIG QUIET)
  if(TARGET unofficial::sqlite3::sqlite3)
    set(OBCX_SQLITE_TARGET unofficial::sqlite3::sqlite3)
  else()
    find_package(SQLite3 REQUIRED)
    set(OBCX_SQLITE_TARGET SQLite::SQLite3)
  endif()
endif()
if(OBCX_IN_NIX_SHELL)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(BROTLIDEC REQUIRED IMPORTED_TARGET libbrotlidec)
  set(BROTLIDEC_TARGET PkgConfig::BROTLIDEC)
else()
  find_package(unofficial-brotli CONFIG QUIET)
  if(unofficial-brotli_FOUND)
    set(BROTLIDEC_TARGET unofficial::brotli::brotlidec)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(BROTLIDEC REQUIRED IMPORTED_TARGET libbrotlidec)
    set(BROTLIDEC_TARGET PkgConfig::BROTLIDEC)
  endif()
endif()

find_package(zstd CONFIG)
if(zstd_FOUND)
  if(TARGET zstd::libzstd)
    set(ZSTD_TARGET zstd::libzstd)
  elseif(TARGET zstd::libzstd_static)
    set(ZSTD_TARGET zstd::libzstd_static)
  else()
    set(ZSTD_TARGET zstd::libzstd_shared)
  endif()
else()
  if(NOT PkgConfig_FOUND)
    find_package(PkgConfig REQUIRED)
  endif()
  pkg_check_modules(LIBZSTD REQUIRED IMPORTED_TARGET libzstd)
  set(ZSTD_TARGET PkgConfig::LIBZSTD)
endif()

find_package(tomlplusplus CONFIG REQUIRED)
find_package(ftxui CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  option(OBCX_ENABLE_IO_URING
         "Use io_uring as Boost.Asio's reactor backend (requires liburing)"
         ON)
else()
  set(OBCX_ENABLE_IO_URING OFF CACHE BOOL
      "io_uring is Linux-only" FORCE)
endif()

set(OBCX_IO_URING_TARGET "")
if(OBCX_ENABLE_IO_URING)
  if(NOT PkgConfig_FOUND)
    find_package(PkgConfig REQUIRED)
  endif()
  pkg_check_modules(LIBURING IMPORTED_TARGET liburing)
  if(LIBURING_FOUND)
    set(OBCX_IO_URING_TARGET PkgConfig::LIBURING)
    message(STATUS "io_uring backend enabled for Boost.Asio "
                   "(liburing ${LIBURING_VERSION})")
  else()
    message(WARNING
      "OBCX_ENABLE_IO_URING=ON but liburing was not found via pkg-config; "
      "falling back to the default epoll reactor. Install liburing-dev "
      "(Debian/Ubuntu) or liburing (Arch/Fedora) to enable io_uring.")
    set(OBCX_ENABLE_IO_URING OFF)
  endif()
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_BINARY_DIR}/generated)

option(OBCX_BUILD_TESTS "Build tests" OFF)
option(OBCX_BUILD_BENCHMARKS "Build repeatable benchmark executables" OFF)

if(OBCX_BUILD_TESTS)
  enable_testing()
endif()


add_subdirectory(src)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(cmake/OBCXActorLoader.cmake)

if(EXISTS "${CMAKE_SOURCE_DIR}/actors.toml")
  obcx_load_actors("${CMAKE_SOURCE_DIR}/actors.toml")
endif()

if(OBCX_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(OBCX_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
