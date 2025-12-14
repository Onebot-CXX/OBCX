cmake_minimum_required(VERSION 3.20)

if (NOT CMAKE_CXX_COMPILER)
    find_program(CLANG_CXX_COMPILER clang++)
    find_program(GNU_CXX_COMPILER g++)

    if (CLANG_CXX_COMPILER)
        set(CMAKE_CXX_COMPILER ${CLANG_CXX_COMPILER})
        message(STATUS "Using LLVM Clang++ compiler: ${CLANG_CXX_COMPILER}")
    elseif (GNU_CXX_COMPILER)
        set(CMAKE_CXX_COMPILER ${GNU_CXX_COMPILER})
        message(STATUS "Using GNU G++ compiler: ${GNU_CXX_COMPILER}")
    endif ()
endif ()

if (NOT CMAKE_C_COMPILER)
    find_program(CLANG_C_COMPILER clang)
    find_program(GNU_C_COMPILER gcc)

    if (CLANG_C_COMPILER)
        set(CMAKE_C_COMPILER ${CLANG_C_COMPILER})
        message(STATUS "Using LLVM Clang compiler: ${CLANG_C_COMPILER}")
    elseif (GNU_C_COMPILER)
        set(CMAKE_C_COMPILER ${GNU_C_COMPILER})
        message(STATUS "Using GNU GCC compiler: ${GNU_C_COMPILER}")
    endif ()
endif ()

project(OBCX LANGUAGES CXX)

if (WIN32)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(VCPKG_TRIPLET "x64-windows")
    else ()
        set(VCPKG_TRIPLET "x86-windows")
    endif ()
elseif (APPLE)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(VCPKG_TRIPLET "arm64-osx")
    else ()
        set(VCPKG_TRIPLET "x64-osx")
    endif ()
elseif (UNIX)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(VCPKG_TRIPLET "x64-linux")
    else ()
        set(VCPKG_TRIPLET "x86-linux")
    endif ()
endif ()

set(CMAKE_PREFIX_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/${VCPKG_TRIPLET}")

option(ENABLE_DEBUG_TRACE "Enable logging with file and line numbers" OFF)
set(CMAKE_UNITY_BUILD ON)
set(CMAKE_UNITY_BUILD_BATCH_SIZE 10)

if (ENABLE_DEBUG_TRACE)
    message(
            STATUS
            "Debug trace logging is enabled. Logs will include file and line numbers."
    )
    add_compile_definitions(OBCX_DEBUG_TRACE)
endif ()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    # For homebrew clang
    if (APPLE)
        set(CMAKE_THREAD_LIBS_INIT "-lpthread")
        set(CMAKE_HAVE_THREADS_LIBRARY 1)
        set(CMAKE_USE_WIN32_THREADS_INIT 0)
        set(CMAKE_USE_PTHREADS_INIT 1)
        set(THREADS_PREFER_PTHREAD_FLAG ON)
    endif ()
    set(CMAKE_LINKER_TYPE LLD)
    message(STATUS "Using libc++ standard library with Clang")
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Boost CONFIG REQUIRED COMPONENTS system thread locale)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(tomlplusplus REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include)

enable_testing()

# Compile i18n .po files to .mo files
find_program(GETTEXT_MSGFMT_EXECUTABLE msgfmt)
if (GETTEXT_MSGFMT_EXECUTABLE)
    file(GLOB_RECURSE PO_FILES "${CMAKE_SOURCE_DIR}/locales/*/messages.po")

    foreach (PO_FILE ${PO_FILES})
        get_filename_component(PO_DIR ${PO_FILE} DIRECTORY)
        get_filename_component(LOCALE_NAME ${PO_DIR} NAME)

        set(MO_FILE "${CMAKE_BINARY_DIR}/locales/${LOCALE_NAME}/LC_MESSAGES/messages.mo")

        add_custom_command(
                OUTPUT ${MO_FILE}
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/locales/${LOCALE_NAME}/LC_MESSAGES"
                COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} -o ${MO_FILE} ${PO_FILE}
                DEPENDS ${PO_FILE}
                COMMENT "Compiling ${PO_FILE} to ${MO_FILE}"
        )

        list(APPEND MO_FILES ${MO_FILE})
    endforeach ()

    add_custom_target(i18n ALL DEPENDS ${MO_FILES})
else ()
    message(WARNING "msgfmt not found. i18n .mo files will not be compiled.")
endif ()

add_subdirectory(src)
add_subdirectory(examples)
add_subdirectory(tests)
