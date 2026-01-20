cmake_minimum_required(VERSION 3.20)

find_package(GTest REQUIRED)
include(GoogleTest)

include_directories(${CMAKE_SOURCE_DIR}/include)

file(GLOB GTEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*test*.cpp")

foreach(source_file ${GTEST_SOURCES})
    get_filename_component(target_name ${source_file} NAME_WE)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE obcx_core GTest::gtest_main GTest::gmock_main)
    gtest_discover_tests(${target_name})
endforeach()