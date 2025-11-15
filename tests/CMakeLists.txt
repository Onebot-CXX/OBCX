cmake_minimum_required(VERSION 3.20)

find_package(GTest REQUIRED)
include(GoogleTest)
set(gtest_disable_pthreads ON)
include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(test_timeout_mechanism
        timeout_mechanism_test.cpp
)

target_link_libraries(test_timeout_mechanism
    PRIVATE
    GTest::gtest
    GTest::gtest_main
    obcx_core
)

target_compile_features(test_timeout_mechanism PRIVATE cxx_std_20)

gtest_discover_tests(test_timeout_mechanism)

add_executable(test_websocket_queue
        websocket_queue_test.cpp
)

target_link_libraries(test_websocket_queue
    PRIVATE
    obcx_core
)

