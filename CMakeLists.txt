cmake_minimum_required(VERSION 3.24)
project(concurrent_price_update_service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

add_library(price_update_service
    src/price_update_service.cpp
)
target_include_directories(price_update_service PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(price_update_service PUBLIC Threads::Threads)

add_executable(concurrent_price_update_service src/main.cpp)
target_link_libraries(concurrent_price_update_service PRIVATE price_update_service)

option(PUS_BUILD_TESTS "Build the test suite" ON)
if(PUS_BUILD_TESTS)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(price_update_service_tests
        tests/price_update_service_test.cpp
    )
    target_include_directories(price_update_service_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
    target_link_libraries(price_update_service_tests
        PRIVATE
            price_update_service
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(price_update_service_tests)
endif()
